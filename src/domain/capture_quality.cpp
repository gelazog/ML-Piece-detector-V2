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
    // El nivel medio solo se juzga cuando la pieza NO se separa del fondo.
    //
    // Si se separa, la imagen sirve para medir por oscura o clara que sea, y
    // rechazarla sería prohibir dos montajes que son estándar: el contraluz
    // —pieza clara sobre fondo negro, que es como se miden las siluetas— da un
    // brillo medio de 37 sobre un mínimo de 40, y una pieza oscura sobre mesa
    // blanca se va por el otro extremo. Los dos se miden perfectamente.
    //
    // Lo que hace inservible una imagen no es que sea oscura ni que sea clara:
    // es que la pieza no se distinga. Y cuando no hay pieza que medir, el nivel
    // medio vuelve a ser lo único que hay.
    const bool standsOut = metrics.pieceFound &&
                           metrics.pieceContrast >= criteria.contrastThatExcusesBrightness;
    if (!standsOut) {
        if (metrics.meanBrightness < criteria.minBrightness) {
            return R::err("Imagen demasiado oscura (brillo " + fmt(metrics.meanBrightness) +
                          ") y la pieza no se separa del fondo");
        }
        if (metrics.meanBrightness > criteria.maxBrightness) {
            return R::err("Imagen demasiado clara (brillo " + fmt(metrics.meanBrightness) +
                          ") y la pieza no se separa del fondo");
        }
    }
    if (metrics.clippedFraction > criteria.maxClippedFraction) {
        return R::err("Exposición saturada (" + fmt(metrics.clippedFraction * 100.0) +
                      "% de píxeles recortados)");
    }
    return R::ok();
}

}  // namespace pci::domain
