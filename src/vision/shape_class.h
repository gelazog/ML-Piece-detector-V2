#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

#include "vision/geometry_features.h"

namespace pci::vision {

// Opciones de descomposición ajustadas al TAMAÑO de la pieza.
//
// El paso de remuestreo por defecto son 2 px fijos, y eso hace que la respuesta
// dependa de lo grande que se vea la pieza. Medido sobre el mismo hexágono: con
// perímetro 1013 px salen sus 6 lados, y con perímetro 257 px salen «4 rectas y
// 2 arcos» — solo 128 muestras para seis lados, y la partición no llega a
// resolver las esquinas. Un hexágono es un hexágono a cualquier distancia de la
// cámara.
//
// Así que lo que se mantiene constante es el NÚMERO DE MUESTRAS (~500), no el
// paso. El tope de 2 px deja intacto el comportamiento de las piezas grandes,
// que ya funcionaba; el suelo de 0,8 px es lo que rescata a las pequeñas, y por
// debajo de eso se estaría remuestreando más fino que el propio píxel.
//
// La usan el clasificador y el generador de propuestas, y tienen que usar la
// MISMA: si vieran contornos distintos, uno diría «hexágono» y el otro
// propondría cuatro lados.
[[nodiscard]] DecomposeOptions decomposeOptionsFor(const std::vector<cv::Point>& contour);

// Qué FIGURA es la pieza, mirando su contorno.
//
// Hace falta porque «qué medir» depende de la forma y no del tamaño. A un disco
// se le mide el diámetro y la redondez; a un hexágono, sus lados y sus ángulos.
// Sin esta pregunta, la medición automática proponía a un disco dos reglas
// cruzadas —el largo y el ancho de su rectángulo envolvente— que sobre una
// pieza redonda no significan nada, y a un hexágono no le proponía ni un lado.
//
// Vive en `vision` y no en el editor porque es una propiedad de la pieza, no de
// una pantalla, y porque así se puede probar sin abrir una ventana.

enum class ShapeKind {
    Circle,     // un disco: una sola circunferencia lo describe entero
    Ring,       // una arandela: disco con un agujero central grande
    Polygon,    // n lados rectos con esquinas vivas
    Rounded,    // n lados rectos con las esquinas redondeadas
    Irregular,  // ninguna de las anteriores: se mide como se venía midiendo
};

[[nodiscard]] const char* shapeKindName(ShapeKind kind);

struct ShapeClass {
    ShapeKind kind = ShapeKind::Irregular;

    // Solo con sentido en Polygon y Rounded.
    int sides = 0;

    // Solo con sentido en Circle y Ring. En píxeles, como todo lo que sale de
    // `vision`: los milímetros los pone quien tiene la calibración.
    cv::Point2f center{0.0F, 0.0F};
    double outerDiameter = 0.0;
    double innerDiameter = 0.0;

    // Separación radial por ZONA MÍNIMA del contorno exterior, que es la
    // redondez de la norma. Se rellena para Circle y Ring.
    double roundness = 0.0;

    // Cuánto se desvía el contorno del modelo elegido, en píxeles. Es el número
    // con el que se decidió, y se expone porque una clasificación sin su
    // residuo es una opinión.
    double deviation = 0.0;

    // En castellano y con el número dentro: es lo que el operador lee para
    // decidir si se fía de la propuesta.
    std::string reason;
};

// CUÁNDO UNA MESETA ES TAN ANCHA QUE MANDA SOBRE LA TOLERANCIA.
//
// El barrido de epsilon responde a DOS preguntas distintas y el código solo
// usaba una. La tolerancia responde «¿este polígono explica el contorno?»; la
// anchura de la meseta responde «¿son estos los lados que tiene la pieza?». Son
// evidencias independientes, y descartar por la primera tiraba respuestas que la
// segunda daba por seguras.
//
// El caso que lo destapó: cien tuercas hexagonales fotografiadas juntas. El
// ajuste de 6 lados aguanta 17 de los 30 epsilon barridos —la siguiente
// explicación aguanta 3— y se descartaba por 0,24 px (6,24 contra el suelo de
// 6,00). Salían con 7, 8, 10 y 11 lados: once aciertos de cien.
//
// Ese suelo de 6 px supone que el dentado del borde es el del rasterizado, ~1 px.
// En una foto real el borde en sombra viene dentado 2-3 px, y sobre una pieza de
// 90 px eso basta para pasarse. Cuando el recuento está fuera de duda, un borde
// sucio no puede convertir un hexágono en «una cosa de once lados».
//
// Las dos constantes salen de un hueco MEDIDO, no de ajustar hasta que pase:
//
//   | pieza                       | meseta | desviación / tope |
//   |-----------------------------|--------|-------------------|
//   | tuerca real (sí es hexágono)| 17/30  | 1,04x  <- la que hay que admitir
//   | arandela, ajuste de 4 lados | 14/30  | 2,13x  <- hay que seguir tirándola
//   | polígono de 16, ajuste de 8 | 16/30  | 2,08x  <- ídem
//   | redondeo 40, ajuste de 4    | 21/30  | 2,90x  <- ídem
//   | cáncamo / tornillo          | 1-3/30 | —      <- ni se acercan
//
// Entre 1,04 y 2,08 hay sitio de sobra, y la mitad del barrido cae entre el 14/30
// que hay que tirar y el 17/30 que hay que admitir. Ninguna de las dos
// condiciones sostiene sola el resultado: la arandela tiene meseta ancha y la
// tira la holgura; el cáncamo cabe en la holgura y lo tira la meseta.
inline constexpr double kPlateauRulesAbove = 0.5;
inline constexpr double kNoisyEdgeAllowance = 1.5;

// CUÁNDO UN ARCO ES UNA ESQUINA Y NO UN LADO MAL LEÍDO.
//
// En un polígono redondeado los arcos son las esquinas, y una esquina es más
// corta que el lado al que pertenece. Si el arco mide más que el lado, la
// descomposición ha leído curvo algo que es recto.
//
// Medido: los rectángulos redondeados de verdad dan un cociente arco/lado de
// 0,07 a 0,60 —y eso incluye un redondeo de 60 px sobre un lado de 200—
// mientras que un polígono de doce lados mal leído da 3,57 y las tuercas de la
// bandeja 0,97. El corte en 0,75 cae en un hueco ancho.
inline constexpr double kArcIsACornerBelow = 0.75;

struct ClassifyOptions {
    // Un contorno se acepta como recto/circular si NINGÚN punto se separa del
    // modelo más de esto. Va en píxeles y no en fracción del perímetro porque
    // lo que limita es el ruido del borde, que no crece con la pieza.
    //
    // El valor sale de medir, no de elegirlo bonito. Un hexágono girado se
    // separa hasta 3,9 px de sus propios lados solo por el rasterizado del
    // borde inclinado (0,9 px sin girar); un rectángulo con redondeos de 40 px
    // se separa 16,6 px de su polígono de esquina viva. Seis px cae en medio de
    // ese hueco con holgura por los dos lados. Con 2,5 —el primer valor que
    // puse— un hexágono girado 10° salía de siete lados.
    double maxDeviationPx = 6.0;
    // Lados por encima de esto ya no son lados: son la discretización de una
    // curva. Un polígono de 20 lados se mide como lo que es, un círculo.
    int maxSides = 12;
    // Un agujero central tiene que ocupar al menos esta fracción del diámetro
    // exterior para convertir un disco en arandela. Por debajo es un agujero
    // más, y ya lo propone la vía de agujeros.
    double minRingHoleFraction = 0.15;
    // Y su centro no puede estar más lejos del centro exterior que esta
    // fracción del radio: si no, no es una arandela, es un disco con un agujero
    // descentrado y llamarlo arandela mentiría sobre lo que se está midiendo.
    double ringConcentricFraction = 0.25;
};

// Clasifica el contorno EXTERIOR. `mask` es opcional y solo se usa para
// distinguir una arandela de un disco: sin ella, una arandela sale como disco,
// que no es falso, solo incompleto.
// `subpixel`, si se pasa y tiene los mismos puntos que `contour`, es el mismo
// contorno con el borde afinado (ver `vision/subpixel_edge.h`). Se usa para todo
// lo que mide DISTANCIAS —el ajuste de circunferencia, la redondez, cuánto se
// separa el contorno de su modelo— mientras que el ajuste de polígono sigue
// yendo por el contorno entero, porque sus vértices son puntos del contorno y no
// posiciones interpoladas.
//
// Nulo o de otro tamaño = se mide como siempre. Que el afinado sea opcional aquí
// no es indecisión: es lo que permite encenderlo sin mover las cotas de nadie
// hasta que su dueño lo decida.
[[nodiscard]] ShapeClass classifyShape(const std::vector<cv::Point>& contour,
                                       const cv::Mat& mask = cv::Mat(),
                                       const ClassifyOptions& options = {},
                                       const std::vector<cv::Point2f>* subpixel = nullptr);

}  // namespace pci::vision
