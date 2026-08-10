#include "vision/fitting.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pci::vision {

namespace {

// Momentos centrados y ponderados de la nube. Se calculan una vez y de ellos
// sale todo el ajuste: Taubin es cerrado salvo una raíz que se busca con
// Newton.
struct Moments {
    double meanX = 0.0;
    double meanY = 0.0;
    double mxx = 0.0;
    double myy = 0.0;
    double mxy = 0.0;
    double mxz = 0.0;
    double myz = 0.0;
    double mzz = 0.0;
    double weight = 0.0;
};

Moments computeMoments(const std::vector<cv::Point2f>& points,
                       const std::vector<double>& weights) {
    Moments m;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double w = weights.empty() ? 1.0 : weights[i];
        m.weight += w;
        m.meanX += w * points[i].x;
        m.meanY += w * points[i].y;
    }
    if (m.weight <= 0.0) {
        return m;
    }
    m.meanX /= m.weight;
    m.meanY /= m.weight;

    for (std::size_t i = 0; i < points.size(); ++i) {
        const double w = weights.empty() ? 1.0 : weights[i];
        const double u = points[i].x - m.meanX;
        const double v = points[i].y - m.meanY;
        const double z = u * u + v * v;
        m.mxx += w * u * u;
        m.myy += w * v * v;
        m.mxy += w * u * v;
        m.mxz += w * u * z;
        m.myz += w * v * z;
        m.mzz += w * z * z;
    }
    m.mxx /= m.weight;
    m.myy /= m.weight;
    m.mxy /= m.weight;
    m.mxz /= m.weight;
    m.myz /= m.weight;
    m.mzz /= m.weight;
    return m;
}

double rmsResidual(const std::vector<cv::Point2f>& points, const cv::Point2f& center,
                   double radius) {
    if (points.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const auto& p : points) {
        const double d = std::hypot(static_cast<double>(p.x) - center.x,
                                    static_cast<double>(p.y) - center.y) -
                         radius;
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<double>(points.size()));
}

CircleFit fitWeighted(const std::vector<cv::Point2f>& points,
                      const std::vector<double>& weights) {
    CircleFit fit;
    if (points.size() < 3) {
        return fit;
    }
    const Moments m = computeMoments(points, weights);
    if (m.weight <= 0.0) {
        return fit;
    }

    // Polinomio característico de Taubin. La raíz buscada es la menor no
    // negativa, y siempre está cerca de cero, así que Newton desde x=0
    // converge en pocas iteraciones sin necesidad de acotar el intervalo.
    const double mz = m.mxx + m.myy;
    const double covXY = m.mxx * m.myy - m.mxy * m.mxy;
    const double varZ = m.mzz - mz * mz;

    const double a3 = 4.0 * mz;
    const double a2 = -3.0 * mz * mz - m.mzz;
    const double a1 = varZ * mz + 4.0 * covXY * mz - m.mxz * m.mxz - m.myz * m.myz;
    const double a0 = m.mxz * (m.mxz * m.myy - m.myz * m.mxy) +
                      m.myz * (m.myz * m.mxx - m.mxz * m.mxy) - varZ * covXY;
    const double a22 = a2 + a2;
    const double a33 = a3 + a3 + a3;

    double x = 0.0;
    double y = a0;
    for (int iter = 0; iter < 100; ++iter) {
        const double dy = a1 + x * (a22 + a33 * x);
        if (std::abs(dy) < 1e-300) {
            break;
        }
        const double xNext = x - y / dy;
        if (!std::isfinite(xNext) || std::abs(xNext - x) < 1e-15) {
            break;
        }
        const double yNext = a0 + xNext * (a1 + xNext * (a2 + xNext * a3));
        if (std::abs(yNext) >= std::abs(y)) {
            break;  // Newton dejó de mejorar: la raíz ya está
        }
        x = xNext;
        y = yNext;
    }

    const double det = x * x - x * mz + covXY;
    if (!std::isfinite(det) || std::abs(det) < 1e-12) {
        return fit;  // puntos alineados: no hay círculo que los explique
    }
    const double cx = (m.mxz * (m.myy - x) - m.myz * m.mxy) / det / 2.0;
    const double cy = (m.myz * (m.mxx - x) - m.mxz * m.mxy) / det / 2.0;
    const double radius = std::sqrt(cx * cx + cy * cy + mz);
    if (!std::isfinite(radius) || radius <= 0.0) {
        return fit;
    }

    fit.center = cv::Point2f(static_cast<float>(cx + m.meanX),
                             static_cast<float>(cy + m.meanY));
    fit.radius = radius;
    fit.rmsResidual = rmsResidual(points, fit.center, radius);
    fit.inlierCount = static_cast<int>(points.size());
    fit.valid = true;
    return fit;
}

double medianOf(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid),
                     values.end());
    return values[mid];
}

}  // namespace

CircleFit fitCircleTaubin(const std::vector<cv::Point2f>& points) {
    return fitWeighted(points, {});
}

CircleFit fitCircleRobust(const std::vector<cv::Point2f>& points, int iterations) {
    CircleFit fit = fitCircleTaubin(points);
    if (!fit.valid) {
        return fit;
    }

    std::vector<double> weights(points.size(), 1.0);
    std::vector<double> residuals(points.size(), 0.0);

    for (int iter = 0; iter < std::max(0, iterations); ++iter) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            residuals[i] = std::hypot(static_cast<double>(points[i].x) - fit.center.x,
                                      static_cast<double>(points[i].y) - fit.center.y) -
                           fit.radius;
        }
        // Escala robusta: MAD reescalada al equivalente de una sigma gaussiana.
        std::vector<double> absResiduals(residuals.size());
        std::transform(residuals.begin(), residuals.end(), absResiduals.begin(),
                       [](double r) { return std::abs(r); });
        const double scale = 1.4826 * medianOf(absResiduals);
        if (!(scale > 1e-9)) {
            break;  // ajuste ya exacto: reponderar no aporta nada
        }

        // Biponderada de Tukey: peso 0 más allá de ~4,7 sigmas, así que un
        // atípico deja de contar en vez de tirar del ajuste.
        constexpr double kTukey = 4.685;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const double u = residuals[i] / (kTukey * scale);
            weights[i] = std::abs(u) < 1.0 ? std::pow(1.0 - u * u, 2.0) : 0.0;
        }

        const CircleFit next = fitWeighted(points, weights);
        if (!next.valid) {
            break;
        }
        const double shift = std::hypot(static_cast<double>(next.center.x) - fit.center.x,
                                        static_cast<double>(next.center.y) - fit.center.y) +
                             std::abs(next.radius - fit.radius);
        fit = next;
        if (shift < 1e-4) {
            break;  // convergió
        }
    }

    // El residuo y el recuento se reportan SOLO sobre los puntos que contaron:
    // incluir los descartados devolvería un residuo enorme y escondería que el
    // ajuste sobre los buenos es excelente.
    std::vector<cv::Point2f> inliers;
    inliers.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (weights[i] > 0.0) {
            inliers.push_back(points[i]);
        }
    }
    fit.inlierCount = static_cast<int>(inliers.size());
    fit.rmsResidual = rmsResidual(inliers, fit.center, fit.radius);
    return fit;
}

}  // namespace pci::vision
