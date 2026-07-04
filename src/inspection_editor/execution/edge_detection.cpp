#include "inspection_editor/execution/edge_detection.h"

#include <algorithm>
#include <cmath>

namespace pci::inspection {

namespace {

// Muestreo bilineal con borde replicado; la imagen ya se validó como CV_8UC1.
double sampleBilinear(const cv::Mat& gray, float x, float y) {
    x = std::clamp(x, 0.0F, static_cast<float>(gray.cols - 1));
    y = std::clamp(y, 0.0F, static_cast<float>(gray.rows - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, gray.cols - 1);
    const int y1 = std::min(y0 + 1, gray.rows - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    const double v00 = gray.at<uchar>(y0, x0);
    const double v01 = gray.at<uchar>(y0, x1);
    const double v10 = gray.at<uchar>(y1, x0);
    const double v11 = gray.at<uchar>(y1, x1);
    return v00 * (1 - fx) * (1 - fy) + v01 * fx * (1 - fy) + v10 * (1 - fx) * fy +
           v11 * fx * fy;
}

}  // namespace

std::vector<EdgeHit> detectEdges(const cv::Mat& gray, cv::Point2f p0, cv::Point2f p1,
                                 float thickness, int maxEdges, double minStrength) {
    if (gray.empty() || gray.type() != CV_8UC1 || maxEdges <= 0) {
        return {};
    }

    const cv::Point2f delta = p1 - p0;
    const float length = static_cast<float>(cv::norm(delta));
    if (length < 3.0F) {
        return {};
    }
    const cv::Point2f u = delta / length;
    const cv::Point2f n(-u.y, u.x);

    const int steps = static_cast<int>(std::lround(length));
    const int halfBand = std::max(0, static_cast<int>(std::lround(thickness / 2.0F)));

    // Perfil promediado a lo ancho de la banda.
    std::vector<double> profile(static_cast<std::size_t>(steps) + 1, 0.0);
    for (int i = 0; i <= steps; ++i) {
        const cv::Point2f base = p0 + u * static_cast<float>(i);
        double sum = 0.0;
        for (int t = -halfBand; t <= halfBand; ++t) {
            const cv::Point2f p = base + n * static_cast<float>(t);
            sum += sampleBilinear(gray, p.x, p.y);
        }
        profile[static_cast<std::size_t>(i)] = sum / (2 * halfBand + 1);
    }

    // Suavizado ligero (media móvil de 3) para estabilizar el gradiente.
    std::vector<double> smooth(profile.size());
    for (std::size_t i = 0; i < profile.size(); ++i) {
        const std::size_t a = i > 0 ? i - 1 : i;
        const std::size_t b = i + 1 < profile.size() ? i + 1 : i;
        smooth[i] = (profile[a] + profile[i] + profile[b]) / 3.0;
    }

    // Gradiente central completo y máximos locales por encima del umbral.
    std::vector<double> grad(smooth.size(), 0.0);
    for (std::size_t i = 1; i + 1 < smooth.size(); ++i) {
        grad[i] = (smooth[i + 1] - smooth[i - 1]) / 2.0;
    }

    std::vector<EdgeHit> hits;
    for (std::size_t i = 1; i + 1 < grad.size(); ++i) {
        const double a = std::abs(grad[i - 1]);
        const double b = std::abs(grad[i]);
        const double c = std::abs(grad[i + 1]);
        // Estrictamente mayor que el anterior evita duplicados en mesetas.
        if (b < minStrength || b <= a || b < c) {
            continue;
        }

        // Refinamiento subpíxel: parábola sobre |gradiente|.
        const double denom = a - 2.0 * b + c;
        const double offset =
            std::abs(denom) > 1e-9 ? std::clamp(0.5 * (a - c) / denom, -0.5, 0.5) : 0.0;

        EdgeHit hit;
        hit.position = static_cast<double>(i) + offset;
        hit.strength = grad[i];
        hit.point = p0 + u * static_cast<float>(hit.position);
        hits.push_back(hit);
    }

    std::sort(hits.begin(), hits.end(), [](const EdgeHit& a, const EdgeHit& b) {
        return std::abs(a.strength) > std::abs(b.strength);
    });
    if (static_cast<int>(hits.size()) > maxEdges) {
        hits.resize(static_cast<std::size_t>(maxEdges));
    }
    return hits;
}

}  // namespace pci::inspection
