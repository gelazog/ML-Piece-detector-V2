#pragma once

#include <opencv2/core.hpp>

#include <vector>

#include "core/result.h"
#include "vision/types.h"

namespace pci::vision {

// Busca el contorno externo de mayor área en una máscara binaria.
// minAreaFraction descarta ruido/escena vacía; maxAreaFraction descarta
// segmentaciones degeneradas (p. ej. iluminación fallida que marca todo).
core::Result<PieceContour> findLargestContour(const cv::Mat& mask,
                                              double minAreaFraction = 0.005,
                                              double maxAreaFraction = 0.9);

// Todos los contornos externos que pasan el filtro de área, **ordenados de
// mayor a menor**. Es lo que hace falta para contar piezas: hasta ahora la
// aplicacion se quedaba con el mayor y borraba el resto en silencio, asi que
// una bandeja con cinco tornillos y otra con seis daban el mismo resultado.
//
// `maxCount` acota el destrozo cuando la segmentacion se va de las manos: con
// una iluminacion mala pueden salir cientos de manchas, y analizarlas todas
// costaria mas que decir que la escena no sirve.
[[nodiscard]] std::vector<PieceContour> findPieceContours(const cv::Mat& mask,
                                                          double minAreaFraction = 0.005,
                                                          double maxAreaFraction = 0.9,
                                                          int maxCount = 64);

}  // namespace pci::vision
