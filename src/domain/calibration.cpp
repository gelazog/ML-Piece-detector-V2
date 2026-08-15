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
    if (!std::isfinite(measuredPx) || !std::isfinite(knownMm) || measuredPx <= 0.0 ||
        knownMm <= 0.0) {
        return calibration;
    }
    calibration.mmPerPixel = knownMm / measuredPx;
    // Una escala que no es un número real es PEOR que no tener escala: `valid()`
    // solo mira que sea mayor que cero, e infinito lo es, así que la aplicación
    // se daría por calibrada y todas las medidas saldrían `inf` sin que nada
    // avise. Es la forma que tiene esta función de hacer daño: una escala mala
    // no falla, da números creíbles y equivocados.
    if (!std::isfinite(calibration.mmPerPixel) || calibration.mmPerPixel <= 0.0) {
        return {};
    }
    calibration.horizontalFovDeg = horizontalFovDeg;
    calibration.cameraDistanceMm =
        estimateCameraDistanceMm(calibration.mmPerPixel, horizontalFovDeg, imageWidthPx);
    return calibration;
}

ScaleCalibration calibrationFromCameraDistance(double cameraDistanceMm,
                                               double horizontalFovDeg, int imageWidthPx) {
    ScaleCalibration calibration;
    // Un campo de visión de 180 grados o más no existe: la tangente de su mitad
    // se dispara hacia el infinito y a partir de ahí cambia de signo, así que
    // sale una escala enorme —o negativa— que `valid()` daría por buena.
    if (cameraDistanceMm <= 0.0 || !std::isfinite(cameraDistanceMm) ||
        horizontalFovDeg <= 0.0 || horizontalFovDeg >= 180.0 || imageWidthPx <= 0) {
        return calibration;
    }
    // Ancho visible del plano a esa distancia: 2·Z·tan(FOV/2).
    const double visibleWidthMm = 2.0 * cameraDistanceMm * halfFovTan(horizontalFovDeg);
    calibration.mmPerPixel = visibleWidthMm / imageWidthPx;
    if (!std::isfinite(calibration.mmPerPixel) || calibration.mmPerPixel <= 0.0) {
        return {};
    }
    calibration.horizontalFovDeg = horizontalFovDeg;
    calibration.cameraDistanceMm = cameraDistanceMm;
    return calibration;
}

double estimateCameraDistanceMm(double mmPerPixel, double horizontalFovDeg,
                                int imageWidthPx) {
    if (mmPerPixel <= 0.0 || !std::isfinite(mmPerPixel) || horizontalFovDeg <= 0.0 ||
        horizontalFovDeg >= 180.0 || imageWidthPx <= 0) {
        return 0.0;
    }
    return mmPerPixel * imageWidthPx / (2.0 * halfFovTan(horizontalFovDeg));
}

}  // namespace pci::domain
