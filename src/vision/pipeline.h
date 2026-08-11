#pragma once

#include <opencv2/core.hpp>

#include <vector>

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
    // Cuántas piezas se esperan en la imagen (C5). No cambia la detección: la
    // usa quien juzga, para poder decir "esperaba 6, veo 5". 0 = no vigilar.
    int expectedPieces = 1;
};

// Punto de entrada único del módulo: segmentación -> contorno mayor ->
// fixture -> recorte normalizado. Todos los fallos regresan como Result.
core::Result<PieceAnalysis> analyzeFrame(const cv::Mat& image,
                                         const PipelineConfig& config = {});

// Todas las piezas de la imagen, **ordenadas de mayor a menor**.
//
// Va aparte de `analyzeFrame` y no al revés (analyzeFrame = la primera de
// estas) por una razón de coste: el camino de una sola pieza es el que corre en
// cada frame del vídeo, y hacerle analizar también las manchas de ruido que
// pasan el filtro de área sería pagar de más en el sitio más caliente.
[[nodiscard]] core::Result<std::vector<PieceAnalysis>> analyzeFrames(
    const cv::Mat& image, const PipelineConfig& config = {});

}  // namespace pci::vision
