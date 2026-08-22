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
    // El mismo contorno con el borde afinado a subpíxel, si se pidió y se pudo.
    // Vacío en cualquier otro caso, y esa es la señal de "no hay": quien mida
    // sobre él sabe que está midiendo con más resolución que la rejilla, y quien
    // no lo mire sigue viendo exactamente lo de siempre en `points`.
    std::vector<cv::Point2f> subpixel;
};

// Sistema de coordenadas de la pieza (Position Fixture): origen en el
// centroide y eje X sobre el eje principal. Las herramientas de inspección
// se guardan en estas coordenadas para moverse con la pieza.
struct Fixture {
    cv::Point2f origin{0.0F, 0.0F};
    double angleDeg = 0.0;  // ángulo del eje principal en coords de imagen (y hacia abajo)
    // Anisotropía [0,1]: 0 = pieza redonda (eje principal indefinido, el
    // ángulo no es de fiar), 1 = muy alargada (eje bien definido). El
    // estabilizador la usa para no perseguir el ruido angular de piezas
    // casi circulares.
    double anisotropy = 1.0;
};

struct PieceAnalysis {
    cv::Mat mask;        // binaria CV_8UC1, pieza = 255
    PieceContour contour;
    Fixture fixture;
    cv::Mat normalized;  // recorte canónico orientado, fondo eliminado
};

}  // namespace pci::vision
