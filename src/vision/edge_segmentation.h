#pragma once

#include <opencv2/core.hpp>

#include <string>

#include "core/result.h"

namespace pci::vision {

// SEGMENTAR POR EL BORDE, no por el nivel.
//
// Otsu pregunta «¿este píxel es más claro o más oscuro que el corte?». Eso da
// por supuesto que la pieza entera cae de un lado del corte y el fondo del otro,
// y hay una familia de escenas donde eso es falso: piezas metálicas sobre una
// mesa clara.
//
// MEDIDO sobre una foto de siete tuercas surtidas (`tests/test_edge_segmentation`):
//
//     fondo de la mesa            176, con una desviación de 7
//     interiores de las piezas    medias de 96 a 172, con valores de 12 a 252
//     corte que elige Otsu        134
//
// Las piezas tienen reflejos POR ENCIMA del fondo y sombras POR DEBAJO, así que
// el corte que recoge una deja fuera a otra. El resultado en esa foto: seis
// contornos en vez de siete, tres piezas distintas fundidas en uno por puentes
// de sombra, y todos ellos trazando solo los trozos oscuros de cada pieza.
//
// Pero el BORDE sí las separa, y por mucho:
//
//     gradiente del fondo          4
//     gradiente en el canto        hasta 469
//
// Así que en vez de preguntar «¿esto es pieza o fondo?», este método pregunta
// «¿se puede llegar aquí desde fuera del encuadre sin cruzar un borde?». Lo que
// no se alcanza es pieza, con su interior entero, brille lo que brille.
//
// NO SUSTITUYE A OTSU, Y ESO ESTÁ MEDIDO. En la misma tanda de pruebas: con las
// tuercas encuentra las siete y Otsu seis; con una bola oscura sobre fondo claro
// encuentra tres donde hay una, y Otsu acierta. Son dos herramientas para dos
// escenas, y por eso esto es una opción y no un cambio de algoritmo.
//
// Cuándo elegirlo, dicho en una frase: cuando las piezas tengan a la vez brillos
// más claros y sombras más oscuras que la mesa. `edgeSegmentationLooksBetter`
// contesta esa pregunta con la imagen delante.

struct EdgeSegmentationOptions {
    // Cuántas veces por encima del ruido de gradiente del fondo tiene que estar
    // un píxel para contar como borde.
    //
    // 6 sale de barrer la tanda: con 4 el ruido de la mesa se cuela y parte las
    // piezas; con 8 los cantos de poco contraste se pierden y el relleno se
    // escapa por el hueco, que se lleva la pieza entera.
    double edgeSigmas = 6.0;
    // Cuánto se engorda el trazo del borde antes de inundar, en píxeles.
    //
    // Un borde real tiene huecos donde el contraste baja, y UN SOLO hueco deja
    // escapar el relleno: no se pierde un trozo de la pieza, se pierde la pieza.
    // Engordar los tapa. El precio está medido y es el que fija este valor en 1:
    // con 2, dos piezas separadas por menos de ocho píxeles se fusionan, y en la
    // foto de las tuercas eso hacía pasar de siete piezas a seis.
    int closeRadiusPx = 1;
    // Área mínima de pieza, en fracción del encuadre. La misma que el resto del
    // programa, para que no haya dos criterios de «esto es demasiado pequeño».
    double minAreaFraction = 0.005;
};

// Máscara binaria CV_8UC1 con pieza = 255, o el motivo por el que no se pudo.
[[nodiscard]] core::Result<cv::Mat> segmentByEdges(const cv::Mat& image,
                                                   const EdgeSegmentationOptions& options = {});

// Lo que hace falta saber para elegir método, medido sobre la imagen que hay
// delante.
struct SceneReading {
    // Nivel de gris del fondo y su ruido, estimados en el marco exterior.
    double backgroundLevel = 0.0;
    double backgroundNoise = 0.0;
    // Qué fracción de la imagen queda MÁS CLARA que el fondo y qué fracción más
    // oscura, contando solo lo que se aparta de él de forma apreciable.
    double brighterThanBackground = 0.0;
    double darkerThanBackground = 0.0;
    // Si las piezas caen a los dos lados del fondo. Es la condición que rompe a
    // Otsu, y la que hace falta para que segmentar por el borde compense.
    bool piecesStraddleTheBackground = false;
    // En castellano, para poder enseñárselo al operador.
    std::string summary;
};

[[nodiscard]] SceneReading readScene(const cv::Mat& image);

// Si conviene segmentar por el borde en ESTA imagen. Es `readScene` resumido a
// un sí o un no, para poder ofrecerlo sin que el operador tenga que interpretar
// nada.
[[nodiscard]] bool edgeSegmentationLooksBetter(const cv::Mat& image);

}  // namespace pci::vision
