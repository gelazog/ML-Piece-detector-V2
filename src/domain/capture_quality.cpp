#include "domain/capture_quality.h"

#include <cstdio>

namespace pci::domain {

namespace {

std::string fmt(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return buffer;
}

}  // namespace

core::Result<void> validateQuality(const QualityMetrics& metrics,
                                   const QualityCriteria& criteria) {
    using R = core::Result<void>;

    if (!metrics.pieceFound) {
        return R::err("No se detectó ninguna pieza en el encuadre");
    }
    if (metrics.pieceTouchesBorder) {
        return R::err("La pieza está cortada por el borde del encuadre");
    }
    if (metrics.sharpness < criteria.minSharpness) {
        return R::err("Imagen borrosa (nitidez " + fmt(metrics.sharpness) + " < " +
                      fmt(criteria.minSharpness) + ")");
    }
    if (metrics.meanBrightness < criteria.minBrightness) {
        return R::err("Imagen demasiado oscura (brillo " + fmt(metrics.meanBrightness) + ")");
    }
    if (metrics.meanBrightness > criteria.maxBrightness) {
        return R::err("Imagen demasiado clara (brillo " + fmt(metrics.meanBrightness) + ")");
    }
    if (metrics.clippedFraction > criteria.maxClippedFraction) {
        return R::err("Exposición saturada (" + fmt(metrics.clippedFraction * 100.0) +
                      "% de píxeles recortados)");
    }
    return R::ok();
}

}  // namespace pci::domain
