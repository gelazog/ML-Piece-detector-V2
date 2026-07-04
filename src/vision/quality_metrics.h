#pragma once

#include <opencv2/core.hpp>

#include "domain/capture_quality.h"
#include "vision/types.h"

namespace pci::vision {

// Calcula las métricas de calidad de una captura. `analysis` es el resultado
// de analyzeFrame sobre el mismo frame (nullptr si no se encontró pieza).
domain::QualityMetrics computeQualityMetrics(const cv::Mat& image,
                                             const PieceAnalysis* analysis);

}  // namespace pci::vision
