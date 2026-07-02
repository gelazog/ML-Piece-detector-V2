#include "vision/segmentation.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdint>

namespace pci::vision {

namespace {

// Media de los píxeles del marco exterior de una máscara binaria.
double borderMean(const cv::Mat& binary) {
    const int border = std::max(1, std::min(binary.rows, binary.cols) / 50);
    double sum = 0.0;
    std::int64_t count = 0;

    auto accumulate = [&](const cv::Mat& region) {
        sum += cv::sum(region)[0];
        count += static_cast<std::int64_t>(region.total());
    };

    accumulate(binary.rowRange(0, border));
    accumulate(binary.rowRange(binary.rows - border, binary.rows));
    accumulate(binary.colRange(0, border));
    accumulate(binary.colRange(binary.cols - border, binary.cols));

    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

}  // namespace

core::Result<cv::Mat> segmentPiece(const cv::Mat& image) {
    if (image.empty()) {
        return core::Result<cv::Mat>::err("Imagen vacía");
    }

    cv::Mat gray;
    switch (image.channels()) {
        case 1:
            gray = image;
            break;
        case 3:
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
            break;
        default:
            return core::Result<cv::Mat>::err("Formato de imagen no soportado: " +
                                              std::to_string(image.channels()) + " canales");
    }

    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0.0);

    cv::Mat binary;
    cv::threshold(blurred, binary, 0.0, 255.0, cv::THRESH_BINARY | cv::THRESH_OTSU);

    // El fondo domina el marco exterior: si el marco quedó blanco, la pieza
    // quedó en negro y hay que invertir para dejar pieza = 255.
    if (borderMean(binary) > 127.0) {
        cv::bitwise_not(binary, binary);
    }

    // Apertura elimina ruido suelto; cierre rellena huecos pequeños de la pieza.
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);

    return core::Result<cv::Mat>::ok(std::move(binary));
}

}  // namespace pci::vision
