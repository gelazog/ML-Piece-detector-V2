#pragma once

#include <opencv2/core.hpp>

#include "core/result.h"
#include "vision/segmentation.h"
#include "vision/types.h"

namespace pci::vision {

struct PipelineConfig {
    double minAreaFraction = 0.005;
    double maxAreaFraction = 0.9;
    int canonicalSize = 256;
    // Si es false, la pieza no se rota al eje principal: se deja vertical tal
    // como la ve la cámara (recorte upright). El eje principal de los momentos
    // es arbitrario e inestable en piezas poco alargadas, así que por defecto
    // no se sigue la rotación (más estable y sin inclinación espuria).
    bool autoOrient = false;
    SegmentationOptions segmentation;
    // Zona de detección: si no está vacía, el contorno automático solo se
    // busca dentro de este rectángulo (coords de imagen) — luces, sombras y
    // objetos fuera de la zona dejan de estorbar. Los resultados se devuelven
    // en coordenadas de la imagen completa.
    cv::Rect roi;
};

// Punto de entrada único del módulo: segmentación -> contorno mayor ->
// fixture -> recorte normalizado. Todos los fallos regresan como Result.
core::Result<PieceAnalysis> analyzeFrame(const cv::Mat& image,
                                         const PipelineConfig& config = {});

}  // namespace pci::vision
