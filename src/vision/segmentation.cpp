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

core::Result<cv::Mat> segmentPiece(const cv::Mat& image, const SegmentationOptions& options) {
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
    const int blur = options.blurKernel | 1;  // los kernels deben ser impares
    if (blur >= 3) {
        cv::GaussianBlur(gray, blurred, cv::Size(blur, blur), 0.0);
    } else {
        blurred = gray;
    }

    // Umbral: automático (Otsu) o fijo elegido por el usuario cuando la
    // iluminación engaña al automático.
    cv::Mat binary;
    if (options.manualThreshold >= 0) {
        cv::threshold(blurred, binary, static_cast<double>(options.manualThreshold), 255.0,
                      cv::THRESH_BINARY);
    } else {
        cv::threshold(blurred, binary, 0.0, 255.0, cv::THRESH_BINARY | cv::THRESH_OTSU);
    }

    // Polaridad: dejar pieza = 255. THRESH_BINARY marca en blanco lo claro.
    switch (options.polarity) {
        case SegmentationPolarity::Auto:
            // El fondo domina el marco exterior: si quedó blanco, invertir.
            if (borderMean(binary) > 127.0) {
                cv::bitwise_not(binary, binary);
            }
            break;
        case SegmentationPolarity::DarkPiece:
            cv::bitwise_not(binary, binary);
            break;
        case SegmentationPolarity::LightPiece:
            break;
    }

    // Apertura elimina ruido suelto; cierre rellena huecos pequeños de la pieza.
    const int morph = options.morphKernel | 1;
    if (morph >= 3) {
        const cv::Mat kernel =
            cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(morph, morph));
        cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);
        cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);
    }

    return core::Result<cv::Mat>::ok(std::move(binary));
}

}  // namespace pci::vision
