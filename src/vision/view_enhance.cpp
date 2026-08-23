#include "vision/view_enhance.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace pci::vision {

namespace {

// Identidad: lo que se devuelve cuando no hay nada que estirar. Así el llamador
// puede aplicarla igualmente sin comprobar nada y obtener la imagen original.
ContrastStretch identity() {
    ContrastStretch stretch;
    for (int i = 0; i < 256; ++i) {
        stretch.lut[static_cast<std::size_t>(i)] = static_cast<unsigned char>(i);
    }
    return stretch;
}

}  // namespace

ContrastStretch autoContrastLut(const cv::Mat& image, double tailFraction) {
    ContrastStretch stretch = identity();
    if (image.empty() || image.depth() != CV_8U) {
        return stretch;
    }

    cv::Mat gray;
    if (image.channels() == 1) {
        gray = image;
    } else if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else if (image.channels() == 4) {
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
    } else {
        return stretch;
    }

    std::array<long long, 256> histogram{};
    for (int y = 0; y < gray.rows; ++y) {
        const auto* row = gray.ptr<unsigned char>(y);
        for (int x = 0; x < gray.cols; ++x) {
            ++histogram[row[x]];
        }
    }
    const long long total = static_cast<long long>(gray.total());
    if (total <= 0) {
        return stretch;
    }

    const auto tail = static_cast<long long>(
        std::llround(std::clamp(tailFraction, 0.0, 0.45) * static_cast<double>(total)));
    long long seen = 0;
    int low = 0;
    for (int i = 0; i < 256; ++i) {
        seen += histogram[static_cast<std::size_t>(i)];
        if (seen > tail) {
            low = i;
            break;
        }
    }
    seen = 0;
    int high = 255;
    for (int i = 255; i >= 0; --i) {
        seen += histogram[static_cast<std::size_t>(i)];
        if (seen > tail) {
            high = i;
            break;
        }
    }
    stretch.low = low;
    stretch.high = high;

    const int range = high - low;
    // Ya usa casi toda la escala, o no hay recorrido que estirar sin fabricar
    // bandas donde solo había ruido. En los dos casos, identidad y se dice.
    if (range >= kAlreadyWideRange || range <= kUnstretchableRange) {
        return stretch;
    }

    for (int i = 0; i < 256; ++i) {
        const double t = static_cast<double>(i - low) / static_cast<double>(range);
        stretch.lut[static_cast<std::size_t>(i)] =
            static_cast<unsigned char>(std::clamp(std::lround(t * 255.0), 0L, 255L));
    }
    stretch.useful = true;
    return stretch;
}

cv::Mat applyStretch(const cv::Mat& image, const ContrastStretch& stretch) {
    if (image.empty() || image.depth() != CV_8U) {
        return image;
    }
    cv::Mat table(1, 256, CV_8UC1);
    for (int i = 0; i < 256; ++i) {
        table.at<unsigned char>(0, i) = stretch.lut[static_cast<std::size_t>(i)];
    }
    cv::Mat out;
    cv::LUT(image, table, out);
    return out;
}

}  // namespace pci::vision
