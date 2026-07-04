#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>

namespace pci::testhelpers {

constexpr double kPi = 3.14159265358979323846;

// Pieza en "L" (asimétrica, orientación no ambigua). El polígono se centra en
// su centroide analítico (1.5, 1.0) para que el centroide rasterizado coincida
// con `center`. Unidades de la L: brazo largo x∈[0,4] y∈[0,1]; brazo corto
// x∈[0,1] y∈[1,3].
inline cv::Point2f lPointToImage(cv::Point2f p, cv::Point2f center, double angleDeg,
                                 float scale) {
    const cv::Point2f centroid(1.5F, 1.0F);
    const double rad = angleDeg * kPi / 180.0;
    const float c = static_cast<float>(std::cos(rad));
    const float s = static_cast<float>(std::sin(rad));
    const cv::Point2f q = (p - centroid) * scale;
    return {center.x + q.x * c - q.y * s, center.y + q.x * s + q.y * c};
}

inline cv::Mat drawLPiece(cv::Size size, cv::Point2f center, double angleDeg, float scale,
                          uchar pieceValue, uchar backgroundValue) {
    cv::Mat image(size, CV_8UC1, cv::Scalar(backgroundValue));

    const std::vector<cv::Point2f> base = {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 1.0F},
                                           {1.0F, 1.0F}, {1.0F, 3.0F}, {0.0F, 3.0F}};
    std::vector<cv::Point> polygon;
    polygon.reserve(base.size());
    for (const auto& p : base) {
        const cv::Point2f q = lPointToImage(p, center, angleDeg, scale);
        polygon.emplace_back(cvRound(q.x), cvRound(q.y));
    }

    const std::vector<std::vector<cv::Point>> polygons{polygon};
    cv::fillPoly(image, polygons, cv::Scalar(pieceValue));
    return image;
}

inline double lPieceArea(float scale) {
    return 6.0 * static_cast<double>(scale) * static_cast<double>(scale);
}

}  // namespace pci::testhelpers
