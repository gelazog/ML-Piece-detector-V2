#include "vision/contour_analysis.h"

#include <opencv2/imgproc.hpp>

#include <utility>
#include <vector>

namespace pci::vision {

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
