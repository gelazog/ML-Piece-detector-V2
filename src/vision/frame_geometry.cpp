#include "vision/frame_geometry.h"

#include <algorithm>
#include <cmath>

namespace pci::vision {

namespace {

bool usable(const cv::Size& size) {
    return size.width > 0 && size.height > 0;
}

}  // namespace

cv::Rect rescaleRect(const cv::Rect& rect, const cv::Size& from, const cv::Size& to) {
    if (!usable(from) || !usable(to) || rect.area() <= 0) {
        return rect;
    }
    const double sx = static_cast<double>(to.width) / from.width;
    const double sy = static_cast<double>(to.height) / from.height;
    cv::Rect scaled(static_cast<int>(std::lround(rect.x * sx)),
                    static_cast<int>(std::lround(rect.y * sy)),
                    static_cast<int>(std::lround(rect.width * sx)),
                    static_cast<int>(std::lround(rect.height * sy)));
    // Nunca devolver un rectángulo fuera del frame nuevo ni degenerado: si el
    // redondeo lo deja en cero, se queda con un píxel de ancho antes que
    // desaparecer sin avisar.
    scaled.width = std::max(1, scaled.width);
    scaled.height = std::max(1, scaled.height);
    return scaled & cv::Rect(0, 0, to.width, to.height);
}

cv::Point2f rescalePoint(const cv::Point2f& point, const cv::Size& from, const cv::Size& to) {
    if (!usable(from) || !usable(to)) {
        return point;
    }
    return {point.x * static_cast<float>(to.width) / static_cast<float>(from.width),
            point.y * static_cast<float>(to.height) / static_cast<float>(from.height)};
}

}  // namespace pci::vision
