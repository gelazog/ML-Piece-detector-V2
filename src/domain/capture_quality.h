#pragma once

#include "core/result.h"

namespace pci::domain {

// Métricas de una captura candidata (las calcula vision/, aquí solo se juzgan
// los números: domain/ no toca OpenCV).
struct QualityMetrics {
    double sharpness = 0.0;        // varianza del Laplaciano
    double meanBrightness = 0.0;   // 0-255
    double clippedFraction = 0.0;  // fracción de píxeles casi negros o saturados
    bool pieceFound = false;
    bool pieceTouchesBorder = false;
};

struct QualityCriteria {
    double minSharpness = 40.0;
    double minBrightness = 40.0;
    double maxBrightness = 215.0;
    double maxClippedFraction = 0.10;
};

// Ok si la captura sirve para registro/inspección; err con el motivo en
// español listo para mostrar al usuario ("Imagen borrosa", ...).
core::Result<void> validateQuality(const QualityMetrics& metrics,
                                   const QualityCriteria& criteria = {});

}  // namespace pci::domain
