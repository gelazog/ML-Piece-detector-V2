#pragma once

#include <vector>

#include <opencv2/core.hpp>

namespace pci::vision {

// Afinado SUBPÍXEL del contorno de la pieza.
//
// El contorno que devuelve la segmentación viene de una máscara binaria, así
// que sus puntos caen en coordenadas enteras: el borde está donde el umbral
// dijo que estaba, ni antes ni después. Eso basta para saber DÓNDE está la
// pieza y no basta para medirla.
//
// Medido sobre una fotografía real —una bola de acero sobre fondo claro— el
// borde no es un escalón: la intensidad pasa de 33 a 240 a lo largo de **15
// píxeles**. Con una rampa así, un umbral global coloca el borde en cualquier
// punto de esos quince según la iluminación de la escena, y el radio del
// contorno resultante variaba entre 118,6 y 129,0 px sobre la misma bola. De
// ahí venía que tres medidas del mismo diámetro —largo, ancho y circunferencia
// ajustada— no coincidieran entre sí por un 3,2 %.
//
// Lo que hace esto es lo que hace un calibre óptico industrial: para cada punto
// del contorno mira el perfil de intensidad A LO LARGO DE SU NORMAL, encuentra
// el nivel de dentro y el de fuera EN ESE PUNTO, y coloca el borde donde el
// perfil cruza la mitad entre los dos, interpolando entre píxeles.
//
// Que los niveles sean LOCALES es la mitad del asunto: una pieza con una cara
// iluminada y otra en sombra tiene dos umbrales correctos distintos, y ningún
// valor global puede ser los dos a la vez.

struct SubpixelOptions {
    // Cuánto se mira a cada lado del borde, en píxeles. Tiene que cubrir la
    // rampa entera: si se queda corto, los niveles de dentro y de fuera se
    // toman todavía dentro de la transición y el cruce sale desplazado.
    int reach = 12;
    // Muestras por píxel al recorrer la normal. Más de dos no añade nada: la
    // imagen no tiene más información que la que tiene.
    int samplesPerPixel = 2;
    // Contraste mínimo entre dentro y fuera para fiarse del punto. Por debajo
    // de esto el perfil es plano —una zona sin borde real— y el punto se deja
    // como estaba en vez de moverlo a un sitio inventado.
    double minContrast = 12.0;
    // Suavizado a lo largo del contorno, en puntos a cada lado.
    //
    // Hace falta por dos motivos distintos que apuntan al mismo sitio. El
    // primero es el afinado mismo: cada punto se mueve por su cuenta a lo largo
    // de su normal, así que dos vecinos pueden quedar en zigzag. El área apenas
    // lo nota —es una integral— pero el perímetro suma cada zigzag y sale
    // largo.
    //
    // El segundo es más viejo y más grande: el efecto ESCALERA. Un contorno de
    // píxeles enteros avanza en pasos de 1 o de raíz de dos, y sumar esos pasos
    // sobreestima la longitud de una curva suave. Medido sobre una bola real,
    // el radio deducido del perímetro salía un 6,75 % mayor que el deducido del
    // área, que para la misma pieza es una contradicción.
    //
    // Un borde real es suave: el zigzag y la escalera son del muestreo, no de
    // la pieza. Cero desactiva el suavizado.
    int smoothSpan = 2;
    // Cuánto puede corregir el suavizado a un punto, en píxeles.
    //
    // Lo destapó una tuerca hexagonal de verdad: sin este tope, el suavizado
    // movía un punto hasta 19,6 px — muchísimo más de lo que el propio afinado
    // tiene permitido. No estaba quitando ruido, estaba REDONDEANDO LAS
    // ESQUINAS del hexágono, que son la pieza y no el muestreo.
    //
    // La regla que lo separa es sencilla y no necesita detectar esquinas: el
    // ruido es pequeño por definición. Si el suavizado quiere mover un punto más
    // que esto, no está viendo ruido, está viendo un rasgo — y un rasgo se
    // respeta.
    double maxSmoothCorrection = 1.0;
    // Cuánto se permite que se mueva un punto respecto a donde lo puso la
    // máscara. Un punto que se va mucho más lejos que la rampa no ha encontrado
    // el borde de la pieza: ha encontrado otra cosa.
    double maxShift = 6.0;
};

// Resultado del afinado, con lo necesario para poder JUZGARLO.
//
// Devolver solo los puntos dejaría al que llama sin forma de saber si el
// afinado hizo algo o se rindió en silencio, que es la diferencia entre una
// medida mejor y una medida igual con más pasos.
struct SubpixelContour {
    std::vector<cv::Point2f> points;
    int refined = 0;    // puntos que encontraron su cruce
    int kept = 0;       // puntos que se dejaron como estaban
    double meanShift = 0.0;  // cuánto se movieron de media, en píxeles
};

// Afina un contorno sobre la imagen en escala de grises de la que salió.
//
// `contour` en coordenadas de `gray`. Si el contorno tiene menos de tres puntos
// o la imagen no es de un canal, se devuelve tal cual: no afinar es una
// respuesta válida, inventarse un borde no.
[[nodiscard]] SubpixelContour refineContourSubpixel(const cv::Mat& gray,
                                                    const std::vector<cv::Point>& contour,
                                                    const SubpixelOptions& options = {});

// Área y perímetro de un contorno subpíxel.
//
// Van aquí y no se calculan con `cv::contourArea` sobre puntos redondeados por
// una razón que es todo el sentido de esto: redondear al entero justo después
// de haber medido en décimas tira la precisión recién ganada.
[[nodiscard]] double subpixelArea(const std::vector<cv::Point2f>& points);
[[nodiscard]] double subpixelPerimeter(const std::vector<cv::Point2f>& points);

}  // namespace pci::vision
