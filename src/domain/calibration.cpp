#include "domain/calibration.h"

#include <cmath>
#include <cstdio>

namespace pci::domain {

namespace {

constexpr double kPi = 3.14159265358979323846;

double halfFovTan(double horizontalFovDeg) {
    return std::tan(horizontalFovDeg * kPi / 360.0);  // tan(FOV/2)
}

std::string fmt(double value, const char* suffix) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, suffix);
    return buffer;
}

}  // namespace

std::string ScaleCalibration::formatLength(double px) const {
    if (!valid()) {
        return fmt(px, "px");
    }
    char buffer[64];
    const double mm = toMm(px);
    if (mm >= 100.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2f cm (%.1f px)", mm / 10.0, px);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f mm (%.1f px)", mm, px);
    }
    return buffer;
}

ScaleCalibration calibrationFromKnownLength(double measuredPx, double knownMm,
                                            int imageWidthPx, double horizontalFovDeg) {
    ScaleCalibration calibration;
    if (measuredPx <= 0.0 || knownMm <= 0.0) {
        return calibration;
    }
    calibration.mmPerPixel = knownMm / measuredPx;
    calibration.horizontalFovDeg = horizontalFovDeg;
    calibration.cameraDistanceMm =
        estimateCameraDistanceMm(calibration.mmPerPixel, horizontalFovDeg, imageWidthPx);
    return calibration;
}

ScaleCalibration calibrationFromCameraDistance(double cameraDistanceMm,
                                               double horizontalFovDeg, int imageWidthPx) {
    ScaleCalibration calibration;
    if (cameraDistanceMm <= 0.0 || horizontalFovDeg <= 0.0 || imageWidthPx <= 0) {
        return calibration;
    }
    // Ancho visible del plano a esa distancia: 2·Z·tan(FOV/2).
    const double visibleWidthMm = 2.0 * cameraDistanceMm * halfFovTan(horizontalFovDeg);
    calibration.mmPerPixel = visibleWidthMm / imageWidthPx;
    calibration.horizontalFovDeg = horizontalFovDeg;
    calibration.cameraDistanceMm = cameraDistanceMm;
    return calibration;
}

double estimateCameraDistanceMm(double mmPerPixel, double horizontalFovDeg,
                                int imageWidthPx) {
    if (mmPerPixel <= 0.0 || horizontalFovDeg <= 0.0 || imageWidthPx <= 0) {
        return 0.0;
    }
    return mmPerPixel * imageWidthPx / (2.0 * halfFovTan(horizontalFovDeg));
}

}  // namespace pci::domain
