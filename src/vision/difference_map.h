#pragma once

#include <opencv2/core.hpp>

namespace pci::vision {

// DÓNDE se diferencia esta pieza de la registrada, no sólo cuánto.
//
// Hoy un NG por anomalía es un número: la similitud cayó por debajo de la banda
// y punto. El operador tiene que buscar a ojo qué le pasa a la pieza, y a veces
// no le pasa nada —lo que ha cambiado es la luz, o la pieza está girada medio
// grado— pero eso no se puede saber mirando un número.
//
// Esto compara el recorte normalizado de ahora contra el de la referencia y
// devuelve un mapa de calor: claro donde se parecen, encendido donde no.
//
// EL PROBLEMA QUE HAY QUE RESOLVER PARA QUE SIRVA es que una resta a secas no
// vale. Dos recortes de la MISMA pieza nunca coinciden píxel a píxel: el
// centroide se calcula sobre un contorno que baila un píxel, y el ángulo del eje
// principal otro tanto. Una resta directa enciende todo el borde de la pieza y
// deja el defecto escondido entre el ruido — y el borde es justo donde más
// brilla, así que el mapa señalaría siempre el contorno.
//
// Por eso la comparación es TOLERANTE: cada píxel se compara con el rango de
// valores que la referencia toma en su vecindad, no con el valor de un único
// píxel. Si cae dentro de ese rango, no se enciende. Es la técnica de siempre en
// inspección óptica, y lo que hace es distinguir «esto está desplazado» de
// «esto no estaba».

struct DifferenceOptions {
    // Cuánto desalineamiento se perdona, en píxeles del recorte normalizado.
    //
    // 3 sale de lo que de verdad se mueve el recorte entre dos fotos de la misma
    // pieza: el centroide y el ángulo se estiman de un contorno que cambia unos
    // píxeles con el umbral. Subirlo esconde defectos finos; bajarlo enciende el
    // contorno entero.
    int tolerancePx = 3;
    // Suavizado del mapa antes de buscar el punto peor. Un defecto real ocupa
    // una zona; un píxel suelto encendido es ruido del sensor, y sin esto sería
    // lo que el mapa señalaría con más fuerza.
    int smoothPx = 5;
    // Por debajo de esta diferencia (en niveles de gris) no se enciende nada.
    // Es el suelo de ruido: sin él, el mapa de dos fotos idénticas ya tiene
    // relieve y el operador ve manchas donde no pasa nada.
    double noiseFloor = 12.0;
};

struct DifferenceMap {
    // Mapa de calor, del tamaño del recorte y en CV_8UC1: 0 donde se parecen,
    // 255 donde más se diferencian.
    cv::Mat heat;
    // Dónde está lo más distinto, y cuánto (0..1).
    cv::Point worst{-1, -1};
    double worstValue = 0.0;
    // Cuánta superficie de la pieza está encendida por encima de la mitad del
    // máximo (0..1). Distingue «un arañazo» de «esta pieza no es la misma».
    double litFraction = 0.0;
    bool ok = false;
    // Por qué no se pudo comparar, si no se pudo.
    std::string problem;
};

// `current` y `reference` son los recortes normalizados (mismo tamaño). Si no
// coinciden en tamaño se reescala la referencia, porque el recorte canónico
// puede haberse guardado con otra resolución en una versión anterior.
[[nodiscard]] DifferenceMap compareToReference(const cv::Mat& current,
                                               const cv::Mat& reference,
                                               const DifferenceOptions& options = {});

// El mapa pintado encima del recorte, para enseñarlo. Rojo lo más distinto,
// transparente donde se parecen.
//
// Se devuelve compuesto y no el mapa suelto porque enseñar el mapa a solas no
// dice nada: un borrón rojo sin la pieza debajo no señala ningún sitio.
[[nodiscard]] cv::Mat paintDifference(const cv::Mat& current, const DifferenceMap& map,
                                      double opacity = 0.55);

}  // namespace pci::vision
