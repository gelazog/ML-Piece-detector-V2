#pragma once

#include <opencv2/core.hpp>

#include "core/result.h"
#include "vision/types.h"

namespace pci::vision {

// Rasgo distintivo elegido por el usuario: un punto visualmente único de la
// pieza (agujero, marca, esquina oscura). Resuelve la ambigüedad de 180° de
// la orientación por momentos — imprescindible en piezas simétricas — para
// que la detección no dependa de cómo llegue rotada la pieza.
struct OrientationAnchor {
    cv::Point2f piecePoint;  // en coordenadas de pieza (fixture canónico)
    double intensity = 0.0;  // intensidad media esperada alrededor del rasgo
};

// Intensidad media en una ventana de (2*radius+1)² alrededor de un punto en
// coordenadas de imagen (recortada a los bordes). Acepta BGR o gris.
double sampleIntensity(const cv::Mat& image, const cv::Point2f& imagePoint, int radius = 4);

// Devuelve el fixture original o el girado 180°: el que deje el rasgo sobre
// la intensidad esperada.
Fixture resolveWithAnchor(const cv::Mat& image, const Fixture& fixture,
                          const OrientationAnchor& anchor);

// Aplica el ancla a un análisis completo: corrige el fixture y recalcula el
// recorte normalizado si hubo giro de 180°. `image` es el frame original.
core::Result<void> applyAnchor(const cv::Mat& image, const OrientationAnchor& anchor,
                               PieceAnalysis& analysis);

// Ajuste manual de orientación: gira el fixture `offsetDeg` grados (dejando
// el eje donde el usuario quiera) y recalcula el recorte normalizado.
core::Result<void> applyOrientationOffset(const cv::Mat& image, double offsetDeg,
                                          PieceAnalysis& analysis);

}  // namespace pci::vision
