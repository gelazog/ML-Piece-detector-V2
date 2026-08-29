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

    // LOS VÉRTICES CON LOS QUE SE DECIDIÓ, en coordenadas de imagen. Se rellenan
    // para Polygon.
    //
    // Se exponen porque sin ellos, quien quiera proponer una cota por lado no
    // tiene de dónde sacarlos y acaba usando la descomposición en rectas y
    // arcos, que es OTRO algoritmo y da otra respuesta. Eso pasaba: el
    // clasificador decía «hexágono de 6 lados» —contando con `approxPolyDP`— y
    // la medición automática ofrecía «2 lados y 3 redondeos», sacados de la
    // descomposición. Medido sobre el banco: de 106 piezas que salen polígono,
    // en UNA coincidía el número.
    //
    // Un aviso a quien venga: el comentario que había en el proponedor afirmaba
    // que los dos miraban lo mismo porque compartían `decomposeOptionsFor`.
    // Compartir las opciones del remuestreo no es mirar lo mismo cuando uno
    // cuenta vértices y el otro parte en primitivas.
    std::vector<cv::Point2f> vertices;

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
// EL RECUENTO DE LADOS QUE AGUANTA UN BARRIDO DE EPSILON.
//
// Existe para que la herramienta Polígono y el clasificador respondan lo mismo.
// No es un detalle de estilo: la última vez que dos partes leyeron el mismo
// contorno con dos criterios distintos, la aplicación decía «hexágono de 6
// lados» y proponía dos lados y tres redondeos.
//
// La herramienta se autocomprobaba mirando TRES epsilon —el elegido, su mitad y
// su doble— y exigiendo que los tres dieran el mismo recuento. Sobre el banco de
// fotos eso pasa en **0 de 106** piezas: un salto de 4× es enorme al lado de la
// meseta real, y en un borde de foto siempre hay algún epsilon del camino que
// mete o quita un vértice. Consecuencia: la propuesta «Lados (n)» —el recuento
// como cota con tolerancia— no llegaba NUNCA, así que la aplicación reconocía un
// hexágono y no ofrecía comprobar que siguiera teniendo seis caras, que es
// justamente la avería que ese recuento vigila.
//
// Con el criterio de meseta —el mismo que decide la clase— pasan **88 de 106**,
// y **86** con el mismo recuento que la clase ya enseña en pantalla.
// CUÁNTO TIENE QUE AGUANTAR UN RECUENTO PARA FIARSE DE ÉL.
//
// No vale reusar `kPlateauRulesAbove`: esa media barrida responde a otra
// pregunta —cuándo un recuento está tan claro que se le perdona un borde
// sucio— y aplicada aquí deja fuera a los polígonos de muchos lados. No es un
// defecto suyo: cuantos más lados, más estrecha es la ventana de epsilon donde
// sobreviven todos. Un dodecágono limpio aguanta 10 de 30 y un polígono de 16,
// 6 de 30, mientras un hexágono aguanta 29.
//
// El corte sale de un hueco MEDIDO, mirando solo los recuentos que además
// explican el contorno:
//
//   | pieza                      | meseta del ganador |
//   |----------------------------|--------------------|
//   | polígonos limpios de 3 a 8 |      22-29 / 30    |
//   | dodecágono limpio          |         10 / 30    |
//   | polígonos de 14 y 16       |          6 / 30    |
//   |----------------------------|--------------------|
//   | disco                      |          3 / 30    |
//   | cáncamo                    |          3 / 30    |
//   | tornillo                   |          1 / 30    |
//
// Entre 3 y 6 hay hueco, y 0,15 (4,5 de 30) cae en medio. Se deja ahí y no en
// 0,20 a propósito: 6/30 es exactamente 0,20 y sentarse sobre el borde de lo
// medido es como se acaba con un umbral que falla en la foto siguiente.
inline constexpr double kCountIsTrustworthyAbove = 0.15;

struct StableSideCount {
    int sides = 0;              // el recuento que gana el barrido
    int plateau = 0;            // en cuántos epsilon aguanta
    int swept = 0;              // cuántos epsilon se barrieron
    // Las DOS mitades del criterio, por separado. Juntarlas en un solo `stable`
    // hacía que el mensaje de error nombrara la causa equivocada: un disco se
    // rechazaba por no explicar el contorno y el texto decía que la meseta era
    // corta, mandando al operador a tocar el epsilon, que no era el problema.
    bool plateauIsWide = false;   // el recuento aguanta al menos media barrida
    bool explainsContour = false;  // y ese polígono se ciñe al contorno
    // Y LA TERCERA: que no lo explique mejor una circunferencia.
    //
    // Las dos de arriba se cumplen a la vez en una arandela pequeña, y no por
    // un fallo: un octógono se ciñe a un disco de 50 px de diámetro con 2 px de
    // error, que cabe de sobra en la tolerancia. Lo que dice que ahí no hay
    // lados no es cuánto se aparta el polígono, sino que el círculo se aparta
    // MENOS.
    bool roundIsABetterFit = false;
    bool stable = false;           // las tres cosas
    double deviation = 0.0;     // lo que se separa el contorno de ese polígono
    double admissible = 0.0;    // hasta cuánto se le admite, para poder decirlo
    double circleDeviation = 0.0;  // y lo que se separaría de una circunferencia
    std::vector<cv::Point> vertices;  // el mejor ajuste con ese recuento
};

// Un polígono deja de contar lados cuando una circunferencia explica el
// contorno bastante mejor que él. El factor sale de un hueco medido sobre el
// banco, no de elegirlo:
//
//   - las 123 piezas con recuento «estable» que la clase llama redonda
//     (arandelas y discos) dan círculo/polígono entre **0,08 y 0,64**;
//   - los 106 polígonos de verdad, entre **1,03 y 2,82** — y el peor caso son
//     las tuercas de la bandeja, con el borde en sombra dentado 2-3 px.
//
// 0,8 cae en el hueco con holgura por los dos lados. Por encima de eso el
// círculo no gana lo suficiente como para quitarle los lados a una pieza que sí
// los tiene.
inline constexpr double kRoundExplainsItBetterBelow = 0.8;

[[nodiscard]] StableSideCount stableSideCountOf(const std::vector<cv::Point>& contour);

// Los vértices de un polígono, AFINADOS contra el contorno.
//
// `approxPolyDP` elige como vértice un PUNTO DEL CONTORNO, y ese punto casi
// nunca es la esquina: en un borde rasterizado cae uno o dos píxeles hacia
// fuera. Medido sobre un hexágono sintético de lado 100 px, tomar los vértices
// tal cual da lados de 103,6 — un **3,6 % de más**, y el error crece con la
// pieza porque un vértice mal puesto inclina el lado entero.
//
// Así que cada cara se ajusta por mínimos cuadrados totales a los puntos del
// contorno que le pertenecen —descartando un margen junto a las esquinas, que
// es donde el rasterizado y el chaflán ensucian— y la esquina sale de CORTAR
// las dos rectas vecinas. Ahí sí está el vértice, aunque no haya ningún píxel.
//
// Es la misma idea que ya usa la herramienta Chaflán para construir su «esquina
// virtual»: el plano acota desde donde se cortarían las dos caras si no hubiera
// redondeo, y ahí no hay ningún punto de la pieza.
//
// Devuelve los de entrada sin tocar si no consigue afinarlos, que es lo honesto:
// un vértice afinado a medias es peor que el original.
[[nodiscard]] std::vector<cv::Point2f> refinePolygonVertices(
    const std::vector<cv::Point>& contour, const std::vector<cv::Point>& vertices);

[[nodiscard]] ShapeClass classifyShape(const std::vector<cv::Point>& contour,
                                       const cv::Mat& mask = cv::Mat(),
                                       const ClassifyOptions& options = {},
                                       const std::vector<cv::Point2f>* subpixel = nullptr);

}  // namespace pci::vision
