#pragma once

#include <opencv2/core.hpp>

#include <optional>

namespace pci::vision {

// Escala derivada de un marcador ArUco de tamaño conocido presente en la
// escena. La homografía lleva coordenadas de imagen al plano real en mm; la
// escala local (mm/px) sale de esa homografía en el punto pedido, así se
// ajusta sola si la cámara/pieza se acerca o aleja (marcador en el mismo
// plano). Depth 3D real (puntos fuera del plano) no es recuperable con una
// sola cámara 2D — queda fuera.
struct MarkerScale {
    double mmPerPixel = 0.0;  // escala local (en el centro del marcador)
    cv::Mat imageToMm;        // homografía 3x3 imagen -> mm en el plano
    // Calidad de la calibración (0..1): el marcador es un cuadrado, así que la
    // uniformidad de sus 4 lados y 2 diagonales en píxeles mide cuán perpendicular
    // está la cámara al plano. 1.0 = vista fronto-paralela ideal; valores bajos =
    // perspectiva/inclinación fuerte, donde una escala px->mm única es poco fiable
    // lejos del marcador.
    double quality = 0.0;
};

// Detecta el primer marcador ArUco (diccionario 4x4_50) de lado markerSideMm.
// nullopt si no hay marcador visible o el tamaño es inválido.
std::optional<MarkerScale> detectMarkerScale(const cv::Mat& image, double markerSideMm);

// Distancia real en mm entre dos puntos de imagen usando la homografía del
// plano (corrige perspectiva). Requiere una imageToMm válida.
double planeDistanceMm(const cv::Mat& imageToMm, const cv::Point2f& a, const cv::Point2f& b);

}  // namespace pci::vision
