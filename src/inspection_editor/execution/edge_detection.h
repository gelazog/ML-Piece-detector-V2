#pragma once

#include <opencv2/core.hpp>

#include <vector>

namespace pci::inspection {

struct EdgeHit {
    double position = 0.0;   // distancia a lo largo del segmento (px, subpíxel)
    double strength = 0.0;   // gradiente con signo (+ = oscuro->claro en el sentido p0->p1)
    cv::Point2f point;       // posición en coordenadas de imagen
};

// Muestrea el perfil de intensidad a lo largo de p0->p1 promediando `thickness`
// píxeles perpendiculares (bilineal), deriva el perfil y devuelve los bordes
// más fuertes con refinamiento subpíxel parabólico, ordenados por |gradiente|
// descendente. Requiere imagen CV_8UC1.
std::vector<EdgeHit> detectEdges(const cv::Mat& gray, cv::Point2f p0, cv::Point2f p1,
                                 float thickness, int maxEdges = 4,
                                 double minStrength = 8.0);

}  // namespace pci::inspection
