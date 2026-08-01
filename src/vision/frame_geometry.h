#pragma once

#include <opencv2/core.hpp>

namespace pci::vision {

// Al cambiar la resolución de la cámara, todo lo que el operador definió en
// PÍXELES DE IMAGEN deja de apuntar al mismo sitio del mundo: la zona de
// detección y el cero fijado del tablero. Estas dos funciones lo reescalan a la
// nueva resolución para que siga señalando lo mismo, en vez de dejarlo
// desplazado en silencio.
//
// (Las herramientas NO necesitan esto: viven en coordenadas de pieza.)

[[nodiscard]] cv::Rect rescaleRect(const cv::Rect& rect, const cv::Size& from,
                                   const cv::Size& to);

[[nodiscard]] cv::Point2f rescalePoint(const cv::Point2f& point, const cv::Size& from,
                                       const cv::Size& to);

}  // namespace pci::vision
