#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

#include "vision/segmentation.h"

namespace pci::vision {

// RODEAR UNA PIEZA A MANO CUANDO LA DETECCIÓN NO LA VE.
//
// Petición de uso: «añadir pieza dibujando un contorno manualmente, y que
// detecte o intente detectar la pieza, por si en un lote no la detecta».
//
// La tentación es tomar el trazo como si fuera la pieza. No se hace, y el motivo
// es de metrología: **un contorno dibujado a pulso no se puede medir**. El ratón
// no sigue el borde con precisión de píxel, así que el diámetro que saliera de
// ahí sería el pulso del operador, no la pieza — y llegaría a la plantilla con
// su tolerancia, indistinguible de una medida de verdad.
//
// Lo que hace el trazo es DECIR DÓNDE MIRAR. Dentro de esa zona se vuelve a
// segmentar, sola, con el fondo que haya ahí: una pieza que se pierde en el
// umbral global —porque la bandeja entera tiene otro nivel, porque la sombra se
// la come— casi siempre aparece cuando se la mira de cerca, que es la misma
// razón por la que existe la zona de trabajo.
//
// Y CUANDO AHÍ DENTRO NO HAY NADA QUE DETECTAR, se dice. La alternativa —callar
// y devolver el trazo como si fuera un borde medido— es la que hay que evitar:
// el operador aceptaría cotas de su propio pulso sin enterarse. Se devuelve el
// trazo igualmente, porque marcar la pieza a mano sigue valiendo para contar y
// para no perderla, pero `detected` viene en falso y `why` dice por qué.

struct OutlinedPiece {
    // Lo que se ha de forzar como pieza (máscara del tamaño del frame). Vacía si
    // el trazo no encerraba nada.
    cv::Mat mask;
    // Si el borde de esa máscara sale de la IMAGEN (se detectó dentro del trazo)
    // o del TRAZO (no había nada que detectar). Es la diferencia entre una cota
    // medible y el pulso de quien dibujó.
    bool detected = false;
    // Qué pasó, en una frase, para poder decirlo en pantalla.
    std::string why;
    // Qué fracción del área del trazo ocupa lo detectado. Se publica para poder
    // explicar el rechazo con su número en vez de con un «no se pudo».
    double fillFraction = 0.0;
};

// Lo que ocupa la pieza dentro del trazo tiene que ser una parte razonable de
// él. Los dos límites salen de lo que significan:
//
//   - por abajo, un ajuste que ocupa menos del 5 % del trazo no es la pieza que
//     se rodeó: es una mota, un reflejo o el ruido del borde;
//   - por arriba, uno que ocupa más del 95 % es el propio trazo devuelto —el
//     umbral no ha separado nada dentro— y darlo por detectado sería llamar
//     medida al pulso.
inline constexpr double kOutlineMinFill = 0.05;
inline constexpr double kOutlineMaxFill = 0.95;

// Busca la pieza dentro del trazo. `frame` es la imagen completa (color o gris)
// y `polygon` el trazo en coordenadas de imagen.
[[nodiscard]] OutlinedPiece pieceInsideOutline(const cv::Mat& frame,
                                               const std::vector<cv::Point>& polygon,
                                               const SegmentationOptions& options = {});

}  // namespace pci::vision
