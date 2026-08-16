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
    // Cuánto se separa la pieza de su fondo, en niveles de gris (0-255). Solo
    // tiene valor cuando se encontró pieza.
    //
    // Es lo que de verdad decide si una imagen sirve para medir, y hacía falta
    // porque el brillo medio rechazaba dos montajes legítimos y opuestos: un
    // CONTRALUZ (pieza clara sobre fondo oscuro) daba un brillo de frame de 37 y
    // se rechazaba por «demasiado oscura», y una pieza oscura sobre mesa blanca
    // daría 30 si se midiera sobre la pieza. Los dos se miden perfectamente.
    // Lo que hace inservible una imagen no es que sea oscura ni clara: es que la
    // pieza no se distinga del fondo.
    double pieceContrast = 0.0;
};

struct QualityCriteria {
    double minSharpness = 40.0;
    double minBrightness = 40.0;
    double maxBrightness = 215.0;
    double maxClippedFraction = 0.10;
    // Por encima de esto, la pieza se distingue de sobra y el nivel medio de la
    // imagen deja de importar. Medido: un contraluz da 190 niveles de
    // separación y una pieza oscura sobre mesa blanca 153; una escena mal
    // expuesta de verdad se queda muy por debajo. 60 cae con holgura en medio.
    double contrastThatExcusesBrightness = 60.0;
};

// Ok si la captura sirve para registro/inspección; err con el motivo en
// español listo para mostrar al usuario ("Imagen borrosa", ...).
core::Result<void> validateQuality(const QualityMetrics& metrics,
                                   const QualityCriteria& criteria = {});

}  // namespace pci::domain
