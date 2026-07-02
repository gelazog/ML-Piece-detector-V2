#pragma once

#include <opencv2/core.hpp>

#include "core/result.h"
#include "vision/types.h"

namespace pci::vision {

struct PipelineConfig {
    double minAreaFraction = 0.005;
    double maxAreaFraction = 0.9;
    int canonicalSize = 256;
};

// Punto de entrada único del módulo: segmentación -> contorno mayor ->
// fixture -> recorte normalizado. Todos los fallos regresan como Result.
core::Result<PieceAnalysis> analyzeFrame(const cv::Mat& image,
                                         const PipelineConfig& config = {});

}  // namespace pci::vision
