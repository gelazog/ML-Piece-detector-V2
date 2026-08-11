#include "vision/contour_analysis.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace pci::vision {

namespace {

// Rellena los datos derivados de un contorno ya aceptado.
PieceContour describe(const std::vector<cv::Point>& points, double area) {
    PieceContour result;
    result.points = points;
    const cv::Moments m = cv::moments(points);
    if (m.m00 > 0.0) {
        result.centroid = {static_cast<float>(m.m10 / m.m00),
                           static_cast<float>(m.m01 / m.m00)};
    }
    result.area = area;
    result.perimeter = cv::arcLength(points, true);
    result.rotatedRect = cv::minAreaRect(points);
    return result;
}

}  // namespace

std::vector<PieceContour> findPieceContours(const cv::Mat& mask, double minAreaFraction,
                                            double maxAreaFraction, int maxCount) {
    std::vector<PieceContour> pieces;
    if (mask.empty() || mask.type() != CV_8UC1 || maxCount <= 0) {
        return pieces;
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const double imageArea = static_cast<double>(mask.total());
    std::vector<std::pair<double, const std::vector<cv::Point>*>> accepted;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        // El mismo filtro que ya se aplicaba al contorno mayor, ahora a cada
        // uno: por debajo es ruido, por encima es una segmentacion degenerada.
        if (area < minAreaFraction * imageArea || area > maxAreaFraction * imageArea) {
            continue;
        }
        accepted.emplace_back(area, &contour);
    }
    std::sort(accepted.begin(), accepted.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    if (static_cast<int>(accepted.size()) > maxCount) {
        accepted.resize(static_cast<std::size_t>(maxCount));
    }
    pieces.reserve(accepted.size());
    for (const auto& [area, points] : accepted) {
        PieceContour piece = describe(*points, area);
        if (piece.area > 0.0) {
            pieces.push_back(std::move(piece));
        }
    }
    return pieces;
}

core::Result<PieceContour> findLargestContour(const cv::Mat& mask, double minAreaFraction,
                                              double maxAreaFraction) {
    if (mask.empty() || mask.type() != CV_8UC1) {
        return core::Result<PieceContour>::err("Máscara inválida (se espera CV_8UC1)");
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    double bestArea = 0.0;
    const std::vector<cv::Point>* best = nullptr;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area > bestArea) {
            bestArea = area;
            best = &contour;
        }
    }

    const double imageArea = static_cast<double>(mask.total());
    if (best == nullptr || bestArea < minAreaFraction * imageArea) {
        return core::Result<PieceContour>::err("No se encontró ninguna pieza en la imagen");
    }
    if (bestArea > maxAreaFraction * imageArea) {
        return core::Result<PieceContour>::err(
            "La segmentación cubre casi toda la imagen (revisa fondo/iluminación)");
    }

    const cv::Moments m = cv::moments(*best);
    if (m.m00 <= 0.0) {
        return core::Result<PieceContour>::err("Contorno degenerado");
    }

    PieceContour result;
    result.points = *best;
    result.centroid = {static_cast<float>(m.m10 / m.m00), static_cast<float>(m.m01 / m.m00)};
    result.area = bestArea;
    result.perimeter = cv::arcLength(*best, true);
    result.rotatedRect = cv::minAreaRect(*best);
    return core::Result<PieceContour>::ok(std::move(result));
}

}  // namespace pci::vision
