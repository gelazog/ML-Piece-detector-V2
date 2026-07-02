#pragma once

#include <opencv2/core.hpp>

#include <vector>

namespace pci::vision {

struct PieceContour {
    std::vector<cv::Point> points;
    cv::Point2f centroid{0.0F, 0.0F};
    double area = 0.0;
    double perimeter = 0.0;
    cv::RotatedRect rotatedRect;
};

// Sistema de coordenadas de la pieza (Position Fixture): origen en el
// centroide y eje X sobre el eje principal. Las herramientas de inspección
// se guardan en estas coordenadas para moverse con la pieza.
struct Fixture {
    cv::Point2f origin{0.0F, 0.0F};
    double angleDeg = 0.0;  // ángulo del eje principal en coords de imagen (y hacia abajo)
};

struct PieceAnalysis {
    cv::Mat mask;        // binaria CV_8UC1, pieza = 255
    PieceContour contour;
    Fixture fixture;
    cv::Mat normalized;  // recorte canónico orientado, fondo eliminado
};

}  // namespace pci::vision
