#pragma once

#include <opencv2/core.hpp>

#include "core/result.h"

namespace pci::vision {

// Segmenta la pieza del fondo por umbral de Otsu + morfología. La polaridad
// (pieza clara u oscura) se detecta sola: el fondo domina el marco exterior
// de la imagen. Requiere fondo razonablemente contrastante y uniforme.
// Devuelve máscara binaria CV_8UC1 con pieza = 255.
core::Result<cv::Mat> segmentPiece(const cv::Mat& image);

}  // namespace pci::vision
