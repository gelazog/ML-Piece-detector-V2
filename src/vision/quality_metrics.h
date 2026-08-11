#pragma once

#include <opencv2/core.hpp>

#include "domain/capture_quality.h"
#include "vision/types.h"

namespace pci::vision {

// Calcula las métricas de calidad de una captura. `analysis` es el resultado
// de analyzeFrame sobre el mismo frame (nullptr si no se encontró pieza).
domain::QualityMetrics computeQualityMetrics(const cv::Mat& image,
                                             const PieceAnalysis* analysis);

// Nitidez (varianza del Laplaciano) de una región. `roi` vacío o fuera de la
// imagen = la imagen entera. Más alto = más nítido; no tiene tope, así que el
// número solo sirve **comparado consigo mismo** — que es justo lo que se hace
// al enfocar: buscar el máximo.
//
// Existe aparte de `computeQualityMetrics` a propósito. Aquella mide sobre el
// frame COMPLETO y su umbral de aceptación (`QualityCriteria::minSharpness`)
// está ajustado contra ese número; moverle la medida debajo cambiaría en
// silencio qué capturas se aceptan al registrar.
[[nodiscard]] double sharpnessOf(const cv::Mat& image, const cv::Rect& roi = {});

}  // namespace pci::vision
