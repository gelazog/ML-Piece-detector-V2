#pragma once

#include <opencv2/core.hpp>

#include <vector>

namespace pci::vision {

// Ajustes geométricos sobre nubes de puntos de borde. Viven aquí, separados de
// las herramientas, porque los comparten el círculo, el arco, el eje, la rosca
// y el engranaje: todos acaban preguntando lo mismo — "qué circunferencia (o
// qué recta) explica estos puntos".

struct CircleFit {
    cv::Point2f center{0.0F, 0.0F};
    double radius = 0.0;
    // Residuo cuadrático medio a la circunferencia, en píxeles. Es la medida
    // honesta de cuánto se parece la nube a un círculo.
    double rmsResidual = 0.0;
    // Puntos que el ajuste consideró buenos. En el ajuste simple son todos.
    int inlierCount = 0;
    bool valid = false;
};

// Ajuste algebraico de Taubin.
//
// Por qué Taubin y no Kasa (el que se usaba): Kasa minimiza el residuo
// algebraico sin normalizar, lo que equivale a dar más peso a los puntos
// lejanos al centro. Sobre una circunferencia completa apenas se nota, pero
// sobre un ARCO PARCIAL —el radio de una esquina, un sector de engranaje— el
// radio sale sistemáticamente corto, y cuanto más corto es el arco, peor.
// Taubin normaliza por el gradiente y es prácticamente insesgado, con el mismo
// coste. Hay un test que mide esa diferencia en arcos de 30° a 360°.
//
// Necesita al menos 3 puntos no alineados; si no, devuelve valid=false.
[[nodiscard]] CircleFit fitCircleTaubin(const std::vector<cv::Point2f>& points);

// Taubin con reponderación iterativa (IRLS con biponderada de Tukey).
//
// El borde de una pieza real trae puntos que no pertenecen al círculo: una
// rebaba, un reflejo, una viruta pegada, un rayo que enganchó el borde
// equivocado. Un ajuste por mínimos cuadrados los promedia y desplaza el
// resultado; aquí se les baja el peso hasta anularlos, midiendo la dispersión
// con la MAD (mediana de desviaciones absolutas), que no se deja arrastrar por
// los propios atípicos.
//
// `inlierCount` dice cuántos puntos acabaron contando: si baja mucho respecto
// al total, la nube no era un círculo y el resultado no es de fiar.
[[nodiscard]] CircleFit fitCircleRobust(const std::vector<cv::Point2f>& points,
                                        int iterations = 5);

}  // namespace pci::vision
