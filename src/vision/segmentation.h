#pragma once

#include <opencv2/core.hpp>

#include "core/result.h"

namespace pci::vision {

enum class SegmentationPolarity {
    Auto,        // el fondo domina el marco exterior de la imagen
    DarkPiece,   // pieza más oscura que el fondo
    LightPiece,  // pieza más clara que el fondo
};

// Controles de detección para lidiar con luces y sombras difíciles.
struct SegmentationOptions {
    int manualThreshold = -1;  // -1 = umbral automático (Otsu); 0-255 manual
    SegmentationPolarity polarity = SegmentationPolarity::Auto;
    int blurKernel = 5;   // suavizado previo (impar; <3 = sin suavizado)
    int morphKernel = 5;  // limpieza morfológica (impar; <3 = sin morfología)
};

// Segmenta la pieza del fondo por umbral + morfología. Devuelve máscara
// binaria CV_8UC1 con pieza = 255.
core::Result<cv::Mat> segmentPiece(const cv::Mat& image,
                                   const SegmentationOptions& options = {});

}  // namespace pci::vision
