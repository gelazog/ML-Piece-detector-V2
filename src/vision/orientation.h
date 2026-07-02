#pragma once

#include <opencv2/core.hpp>

#include "core/result.h"

namespace pci::vision {

// Ángulo del eje principal de la máscara en grados [-180, 180), en coordenadas
// de imagen (x a la derecha, y hacia abajo). Se calcula con momentos centrales
// de segundo orden; la ambigüedad de 180° se resuelve con el signo del momento
// de tercer orden a lo largo del eje (asimetría de la pieza).
// Limitación conocida: inestable para piezas casi circulares o con simetría
// de rotación perfecta — irrelevante para comparación por embeddings.
core::Result<double> principalAngleDeg(const cv::Mat& mask);

}  // namespace pci::vision
