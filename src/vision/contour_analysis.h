#pragma once

#include <opencv2/core.hpp>

#include "core/result.h"
#include "vision/types.h"

namespace pci::vision {

// Busca el contorno externo de mayor área en una máscara binaria.
// minAreaFraction descarta ruido/escena vacía; maxAreaFraction descarta
// segmentaciones degeneradas (p. ej. iluminación fallida que marca todo).
core::Result<PieceContour> findLargestContour(const cv::Mat& mask,
                                              double minAreaFraction = 0.005,
                                              double maxAreaFraction = 0.9);

}  // namespace pci::vision
