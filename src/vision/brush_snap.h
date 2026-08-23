#pragma once

#include <opencv2/core.hpp>

namespace pci::vision {

// CEÑIR LA PINCELADA AL BORDE DE VERDAD.
//
// El pincel de corregir el borde pinta una banda de ancho constante por donde
// pasa la mano. Eso arregla la máscara, pero deja un contorno con la forma del
// PULSO del operador: una banda uniforme de ancho fijo y camino tembloroso, que
// es justo lo contrario de lo que se quiere de un borde medido.
//
// Lo que hace esto es quedarse solo con la mitad de la banda que se parece a lo
// que el operador señaló, en vez de rellenarla entera. El resultado deja de
// tener el ancho del pincel y pasa a tener la forma del contraste real: menos
// uniforme y más pegado a la pieza.
//
// **La mitad que se queda es la que se parece al punto donde EMPEZÓ el trazo.**
//
// Esa regla se eligió por encima de usar la polaridad global de la detección
// («la pieza es la oscura») por dos motivos. El primero es que no necesita que
// nadie le pase nada: no hay un ajuste más que pueda quedarse desincronizado. El
// segundo es que es más correcta donde importa — bajo una sombra o un reflejo,
// la polaridad LOCAL de la banda puede ser la contraria de la global, y una
// regla global se equivocaría exactamente en el sitio donde el operador está
// corrigiendo a mano porque la detección ya se equivocó.
//
// Y es una regla que se explica en una frase: empieza el trazo encima de lo que
// quieres marcar.

struct BrushSnapResult {
    // Máscara CV_8UC1 del tamaño de `area`: 255 donde la banda se queda.
    cv::Mat kept;
    // false = no se pudo ceñir y `kept` es la banda ENTERA, sin tocar.
    //
    // Devolver la banda entera y no una vacía es la decisión importante: si no
    // hay contraste que seguir, la corrección tiene que hacer lo de siempre. Un
    // pincel que a veces no pinta nada es peor que un pincel que a veces pinta
    // recto, porque el operador no sabe si falló él o falló el programa.
    bool snapped = false;
    // Separación entre las medias de las dos poblaciones, en niveles de gris.
    // Es la cifra que dice POR QUÉ no se pudo ceñir, y se enseña.
    double contrast = 0.0;
    // Cuántos píxeles de la banda se quedan y cuántos se descartan.
    int keptPixels = 0;
    int bandPixels = 0;
};

// Por debajo de este contraste dentro de la banda no hay dos poblaciones, hay
// una con ruido, y partirla por la mitad inventaría un borde donde no lo hay.
//
// El valor es el mismo que usa el afinado subpíxel (`subpixel_edge.h`) y por la
// misma razón medida: un borde real sobre una pieza de verdad salta de 33 a 240
// en 15 px, y el ruido de un sensor decente se queda muy por debajo de 12.
inline constexpr double kMinBandContrast = 12.0;

// Con menos píxeles que esto la banda no da para estimar dos poblaciones.
inline constexpr int kMinBandPixels = 24;

// `gray`   imagen en gris del frame entero.
// `band`   máscara CV_8UC1 del frame entero: 255 donde este trazo ha pintado.
// `area`   la envolvente del trazo; se trabaja solo dentro.
// `seedGray` gris medio del entorno del punto donde empezó el trazo.
//
// La máscara devuelta está en coordenadas de `area` (no del frame), porque el
// llamador ya tiene el recorte y copiarla al frame entero costaría el tamaño
// del frame por pincelada.
[[nodiscard]] BrushSnapResult snapBrushBand(const cv::Mat& gray, const cv::Mat& band,
                                            const cv::Rect& area, double seedGray,
                                            double minContrast = kMinBandContrast);

// Gris medio de un disco alrededor de un punto: la semilla del trazo.
//
// Un solo píxel serviría y sería frágil — un píxel de ruido o un reflejo
// especular en el sitio donde se hizo clic invertiría la decisión entera.
[[nodiscard]] double seedIntensity(const cv::Mat& gray, const cv::Point& centre, int radius);

}  // namespace pci::vision
