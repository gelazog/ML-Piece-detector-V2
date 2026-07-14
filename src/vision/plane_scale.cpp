#include "vision/plane_scale.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

#include <array>
#include <vector>

namespace pci::vision {

namespace {

cv::Point2f mapPoint(const cv::Mat& h, const cv::Point2f& p) {
    std::vector<cv::Point2f> in{p};
    std::vector<cv::Point2f> out;
    cv::perspectiveTransform(in, out, h);
    return out.front();
}

}  // namespace

std::optional<MarkerScale> detectMarkerScale(const cv::Mat& image, double markerSideMm) {
    if (image.empty() || markerSideMm <= 0.0) {
        return std::nullopt;
    }

    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = image;
    }

    // Diccionario pequeño y robusto; una sola detección por frame analizado
    // (que ya está limitado a uno en vuelo) — no satura.
    static const cv::aruco::Dictionary dict =
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
    const cv::aruco::ArucoDetector detector(dict);

    std::vector<std::vector<cv::Point2f>> corners;
    std::vector<int> ids;
    detector.detectMarkers(gray, corners, ids);
    if (corners.empty()) {
        return std::nullopt;
    }

    // Primer marcador. Sus 4 esquinas (orden horario desde arriba-izquierda)
    // corresponden a un cuadrado real de lado markerSideMm.
    const auto& c = corners.front();
    const std::array<cv::Point2f, 4> imagePts = {c[0], c[1], c[2], c[3]};
    const float s = static_cast<float>(markerSideMm);
    const std::array<cv::Point2f, 4> mmPts = {
        cv::Point2f(0.0F, 0.0F), cv::Point2f(s, 0.0F), cv::Point2f(s, s),
        cv::Point2f(0.0F, s)};

    MarkerScale result;
    result.imageToMm = cv::getPerspectiveTransform(imagePts.data(), mmPts.data());

    // Escala local en el centro del marcador: cuánto mm cubre 1 px allí.
    const cv::Point2f center = (c[0] + c[1] + c[2] + c[3]) / 4.0F;
    const double dx = cv::norm(mapPoint(result.imageToMm, center + cv::Point2f(1.0F, 0.0F)) -
                              mapPoint(result.imageToMm, center));
    const double dy = cv::norm(mapPoint(result.imageToMm, center + cv::Point2f(0.0F, 1.0F)) -
                              mapPoint(result.imageToMm, center));
    result.mmPerPixel = (dx + dy) / 2.0;
    if (result.mmPerPixel <= 0.0) {
        return std::nullopt;
    }
    return result;
}

double planeDistanceMm(const cv::Mat& imageToMm, const cv::Point2f& a, const cv::Point2f& b) {
    if (imageToMm.empty()) {
        return 0.0;
    }
    return cv::norm(mapPoint(imageToMm, b) - mapPoint(imageToMm, a));
}

}  // namespace pci::vision
