#include "vision/fitting.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <limits>
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

// --------------------------------------------------------------------------
// Arcos por tres puntos
// --------------------------------------------------------------------------

namespace {

constexpr double kRadToDeg = 57.29577951308232;

// Normaliza a [0, 360).
double wrap360(double degrees) {
    double d = std::fmod(degrees, 360.0);
    if (d < 0.0) {
        d += 360.0;
    }
    return d;
}

}  // namespace

ArcSpan circleThroughThreePoints(const cv::Point2f& start, const cv::Point2f& mid,
                                 const cv::Point2f& end) {
    ArcSpan arc;
    // Circuncentro: intersección de las mediatrices, resuelta por determinantes.
    const double ax = start.x;
    const double ay = start.y;
    const double bx = mid.x;
    const double by = mid.y;
    const double cx = end.x;
    const double cy = end.y;

    const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(d) < 1e-9) {
        return arc;  // alineados o repetidos: no hay circunferencia
    }
    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;
    const double ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
    const double uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;

    arc.center = cv::Point2f(static_cast<float>(ux), static_cast<float>(uy));
    arc.radius = std::hypot(ax - ux, ay - uy);
    if (!(arc.radius > 1e-6) || !std::isfinite(arc.radius)) {
        return arc;
    }

    const double angleStart = std::atan2(ay - uy, ax - ux) * kRadToDeg;
    const double angleMid = std::atan2(by - uy, bx - ux) * kRadToDeg;
    const double angleEnd = std::atan2(cy - uy, cx - ux) * kRadToDeg;

    // El punto intermedio decide el sentido: si al avanzar en positivo se
    // encuentra antes que el final, el arco va en positivo; si no, al revés.
    const double toMid = wrap360(angleMid - angleStart);
    const double toEnd = wrap360(angleEnd - angleStart);
    arc.startAngleDeg = wrap360(angleStart);
    arc.sweepDeg = toMid <= toEnd ? toEnd : toEnd - 360.0;
    arc.valid = true;
    return arc;
}

bool angleWithinSweep(double angleDeg, double startAngleDeg, double sweepDeg) {
    const double delta = wrap360(angleDeg - startAngleDeg);
    if (sweepDeg >= 0.0) {
        return delta <= sweepDeg;
    }
    return delta - 360.0 >= sweepDeg;
}

// --------------------------------------------------------------------------
// Rectas
// --------------------------------------------------------------------------

double LineFit::signedDistance(const cv::Point2f& p) const {
    // Componente perpendicular del vector punto-recta: el producto cruzado con
    // la dirección unitaria ya da la distancia con signo.
    const double dx = static_cast<double>(p.x) - point.x;
    const double dy = static_cast<double>(p.y) - point.y;
    return dx * direction.y - dy * direction.x;
}

double LineFit::angleDeg() const {
    constexpr double kRadToDeg = 57.29577951308232;
    double angle = std::atan2(static_cast<double>(direction.y),
                              static_cast<double>(direction.x)) *
                   kRadToDeg;
    // La dirección es canónica, así que ya cae en [-90, 90]; se normaliza el
    // borde para que -90 y 90 no sean dos respuestas distintas a lo mismo.
    if (angle <= -90.0) {
        angle += 180.0;
    }
    return angle;
}

namespace {

LineFit fitLineWeighted(const std::vector<cv::Point2f>& points,
                        const std::vector<double>& weights) {
    LineFit fit;
    if (points.size() < 2) {
        return fit;
    }

    double totalWeight = 0.0;
    double meanX = 0.0;
    double meanY = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double w = weights.empty() ? 1.0 : weights[i];
        totalWeight += w;
        meanX += w * points[i].x;
        meanY += w * points[i].y;
    }
    if (totalWeight <= 0.0) {
        return fit;
    }
    meanX /= totalWeight;
    meanY /= totalWeight;

    double sxx = 0.0;
    double syy = 0.0;
    double sxy = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double w = weights.empty() ? 1.0 : weights[i];
        const double dx = points[i].x - meanX;
        const double dy = points[i].y - meanY;
        sxx += w * dx * dx;
        syy += w * dy * dy;
        sxy += w * dx * dy;
    }
    sxx /= totalWeight;
    syy /= totalWeight;
    sxy /= totalWeight;

    // Autovalores de la covarianza 2x2 en forma cerrada. El mayor mide la
    // dispersión a lo largo de la recta; el menor, el grosor de la nube.
    const double trace = sxx + syy;
    const double diff = std::sqrt(std::max(0.0, (sxx - syy) * (sxx - syy) + 4.0 * sxy * sxy));
    const double lambdaMax = (trace + diff) / 2.0;
    const double lambdaMin = (trace - diff) / 2.0;
    if (lambdaMax < 1e-12) {
        return fit;  // todos los puntos en el mismo sitio
    }
    // Misma fórmula que Fixture::anisotropy, para que "0 = redondo, 1 = línea"
    // signifique lo mismo en todo el proyecto.
    fit.anisotropy = 1.0 - std::sqrt(std::max(0.0, lambdaMin) / lambdaMax);

    // Eje principal. atan2(2·Sxy, Sxx−Syy)/2 es el ángulo del autovector mayor
    // y no se rompe en ninguna orientación, incluida la vertical.
    const double angle = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
    double dirX = std::cos(angle);
    double dirY = std::sin(angle);
    // Forma canónica: mismo conjunto de puntos, misma dirección siempre.
    if (dirX < 0.0 || (std::abs(dirX) < 1e-12 && dirY < 0.0)) {
        dirX = -dirX;
        dirY = -dirY;
    }

    fit.point = cv::Point2f(static_cast<float>(meanX), static_cast<float>(meanY));
    fit.direction = cv::Point2f(static_cast<float>(dirX), static_cast<float>(dirY));
    fit.valid = true;

    double sum = 0.0;
    for (const auto& p : points) {
        const double d = fit.signedDistance(p);
        sum += d * d;
    }
    fit.rmsResidual = std::sqrt(sum / static_cast<double>(points.size()));
    fit.inlierCount = static_cast<int>(points.size());
    return fit;
}

}  // namespace

LineFit fitLineTotal(const std::vector<cv::Point2f>& points) {
    return fitLineWeighted(points, {});
}

MinimumZone minimumZoneBand(const std::vector<cv::Point2f>& points) {
    MinimumZone zone;
    if (points.size() < 2) {
        return zone;
    }

    std::vector<cv::Point2f> hull;
    cv::convexHull(points, hull);
    if (hull.size() < 2) {
        return zone;
    }
    if (hull.size() == 2) {
        // Todos los puntos alineados: la banda tiene anchura cero.
        const cv::Point2f delta = hull[1] - hull[0];
        const double length = cv::norm(delta);
        if (length < 1e-9) {
            return zone;
        }
        zone.width = 0.0;
        zone.direction = delta / static_cast<float>(length);
        zone.point = (hull[0] + hull[1]) * 0.5F;
        zone.valid = true;
        return zone;
    }

    // Calibres rotantes: una orientación por arista del casco. El mínimo de la
    // anchura se alcanza siempre en una de ellas, así que esto no es una
    // aproximación por muestreo — es el mínimo exacto.
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < hull.size(); ++i) {
        const cv::Point2f a = hull[i];
        const cv::Point2f b = hull[(i + 1) % hull.size()];
        const cv::Point2f edge = b - a;
        const double length = cv::norm(edge);
        if (length < 1e-9) {
            continue;
        }
        const cv::Point2f direction = edge / static_cast<float>(length);
        const cv::Point2f normal(-direction.y, direction.x);
        // Todo el casco cae del mismo lado de su propia arista, así que la
        // anchura es simplemente el punto más lejano.
        double farthest = 0.0;
        for (const auto& p : hull) {
            farthest = std::max(farthest, std::abs(static_cast<double>((p - a).dot(normal))));
        }
        if (farthest < best) {
            best = farthest;
            zone.width = farthest;
            zone.direction = direction;
            zone.point = a + normal * static_cast<float>(farthest / 2.0);
            zone.valid = true;
        }
    }
    return zone;
}

LineFit fitLineRobust(const std::vector<cv::Point2f>& points, int iterations) {
    LineFit fit = fitLineTotal(points);
    if (!fit.valid) {
        return fit;
    }

    std::vector<double> weights(points.size(), 1.0);
    std::vector<double> residuals(points.size(), 0.0);

    for (int iter = 0; iter < std::max(0, iterations); ++iter) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            residuals[i] = fit.signedDistance(points[i]);
        }
        std::vector<double> absResiduals(residuals.size());
        std::transform(residuals.begin(), residuals.end(), absResiduals.begin(),
                       [](double r) { return std::abs(r); });
        const double scale = 1.4826 * medianOf(absResiduals);
        if (!(scale > 1e-9)) {
            break;  // ajuste ya exacto
        }

        constexpr double kTukey = 4.685;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const double u = residuals[i] / (kTukey * scale);
            weights[i] = std::abs(u) < 1.0 ? std::pow(1.0 - u * u, 2.0) : 0.0;
        }

        const LineFit next = fitLineWeighted(points, weights);
        if (!next.valid) {
            break;
        }
        const double shift =
            std::hypot(static_cast<double>(next.point.x) - fit.point.x,
                       static_cast<double>(next.point.y) - fit.point.y) +
            std::abs(next.angleDeg() - fit.angleDeg());
        fit = next;
        if (shift < 1e-4) {
            break;
        }
    }

    // Residuo y recuento sobre los puntos que contaron, igual que en el círculo.
    double sum = 0.0;
    int inliers = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (weights[i] > 0.0) {
            const double d = fit.signedDistance(points[i]);
            sum += d * d;
            ++inliers;
        }
    }
    fit.inlierCount = inliers;
    fit.rmsResidual = inliers > 0 ? std::sqrt(sum / inliers) : 0.0;
    return fit;
}

}  // namespace pci::vision
