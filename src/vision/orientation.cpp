#include "vision/orientation.h"

#include <opencv2/imgproc.hpp>

#include <cmath>

namespace pci::vision {

core::Result<double> principalAngleDeg(const cv::Mat& mask) {
    if (mask.empty() || mask.type() != CV_8UC1) {
        return core::Result<double>::err("Máscara inválida (se espera CV_8UC1)");
    }

    const cv::Moments m = cv::moments(mask, true);
    if (m.m00 <= 0.0) {
        return core::Result<double>::err("Máscara vacía: no hay pieza");
    }

    double angle = 0.5 * std::atan2(2.0 * m.mu11, m.mu20 - m.mu02);

    // Momento de tercer orden proyectado sobre el eje principal: su signo
    // distingue "hacia dónde apunta" la asimetría y fija el sentido del eje.
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double skew = m.mu30 * c * c * c + 3.0 * m.mu21 * c * c * s +
                        3.0 * m.mu12 * c * s * s + m.mu03 * s * s * s;
    if (skew < 0.0) {
        angle += CV_PI;
    }

    double degrees = angle * 180.0 / CV_PI;
    if (degrees >= 180.0) {
        degrees -= 360.0;
    }
    if (degrees < -180.0) {
        degrees += 360.0;
    }
    return core::Result<double>::ok(degrees);
}

}  // namespace pci::vision
