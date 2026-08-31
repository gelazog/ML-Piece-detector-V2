#pragma once

#include <opencv2/core.hpp>

#include "core/result.h"

namespace pci::vision {

enum class SegmentationPolarity {
    Auto,        // el fondo domina el marco exterior de la imagen
    DarkPiece,   // pieza más oscura que el fondo
    LightPiece,  // pieza más clara que el fondo
};

// CÓMO se separa la pieza del fondo.
//
// `Level` es el de siempre: un corte de gris, con Otsu o a mano. Da por supuesto
// que la pieza cae entera de un lado del corte.
//
// `Edges` no mira el nivel sino el CANTO, y existe porque hay escenas donde
// aquel supuesto es falso — piezas metálicas sobre una mesa clara tienen
// reflejos por encima del fondo y sombras por debajo, así que ningún corte único
// las coge. Medido sobre siete tuercas surtidas: por nivel salen seis piezas con
// tres fundidas por puentes de sombra, y por borde salen las siete enteras.
//
// No es «mejor»: es para otra escena. En una bola oscura sobre fondo claro, el
// nivel acierta y el borde no. Ver `vision/edge_segmentation.h`, que además sabe
// decir cuál de los dos le conviene a la imagen que hay delante.
enum class SegmentationMethod {
    Level,
    Edges,
};

// Controles de detección para lidiar con luces y sombras difíciles.
struct SegmentationOptions {
    SegmentationMethod method = SegmentationMethod::Level;
    // SEPARAR LAS PIEZAS QUE SE TOCAN.
    //
    // `RETR_EXTERNAL` devuelve una sola mancha cuando dos piezas se rozan, y
    // entonces no hay nada que recorrer ni que enseñar por separado: el
    // operador ve dos piezas y el programa cuenta una.
    //
    // Con esto encendido, cada mancha se mira por dentro: se calcula su
    // transformada de distancia y se cuentan los «corazones» —las zonas más
    // alejadas del fondo—. Dos piezas pegadas tienen dos; una pieza sola tiene
    // uno. El umbral es relativo al radio de ESA mancha, así que funciona igual
    // con una tuerca pequeña que con un engranaje grande.
    //
    // NACE APAGADO, y no por prudencia genérica: está medido que no gana
    // siempre. Sobre las imágenes reales de prueba:
    //
    //     dos engranajes engranados   1 -> 2 piezas   ARREGLA
    //     tres tornillos en fila      3 -> 3          igual
    //     bandeja de cien tuercas   100 -> 100        igual
    //     un tornillo largo solo      1 -> 2          ROMPE (parte cabeza y vástago)
    //
    // Un tornillo largo tiene la cabeza y el vástago lo bastante distintos como
    // para parecer dos corazones. Por eso es una opción y no el comportamiento
    // por defecto: enciéndela cuando tengas piezas que se tocan, y déjala
    // apagada si tus piezas son alargadas con cabeza.
    bool splitTouchingPieces = false;
    // RECUPERAR LO QUE EL BRILLO SE LLEVA.
    //
    // Queja de uso: «tengo una tuerca, con reflejos, brillo, sombras, y eso
    // afecta a la medición y la forma en que toma los bordes».
    //
    // Un corte de gris supone que la pieza cae ENTERA de un lado. Sobre metal es
    // falso: el reflejo especular sube un trozo de la pieza hasta el nivel del
    // fondo, el corte lo deja fuera, y la pieza sale MORDIDA o partida en
    // trozos. Medido sobre las fotos reales: tres tornillos cincados salían
    // como CINCO manchas y un tornillo galvanizado como DOS.
    //
    // Con esto se corta dos veces. El corte de siempre da las SEMILLAS —lo que
    // es pieza con seguridad—, y un corte aflojado estos niveles da hasta dónde
    // PODRÍA llegar. Se queda lo aflojado que TOQUE una semilla, y se tira lo
    // demás.
    //
    // Por qué eso no mete fondo: el fondo aflojado tampoco toca ninguna semilla,
    // porque las semillas son pieza. Sube lo que estaba pegado a la pieza —el
    // brillo de su propia cara— y no lo que estaba pegado a la mesa. Es la misma
    // idea que la histéresis de Canny, aplicada al nivel en vez de al gradiente.
    //
    //     imagen                    verdad   antes   con esto
    //     tres tornillos cincados      3       5 MAL    3 ok
    //     un tornillo galvanizado      1       2 MAL    1 ok
    //     bandeja de cien tuercas    100     100 ok    100 ok
    //     un engranaje                 1       1 ok      1 ok
    //
    // 0 = apagado. NACE APAGADO porque cambia lo que se mide, y eso se elige.
    // Más de 20 niveles empieza a desbordar: con 30, la bandeja de cien tuercas
    // se funde en 64 y los tres tornillos en uno. 12 es lo medido como seguro.
    int recoverHighlightsBy = 0;

    // SEPARAR POR COLOR DE FONDO, NO POR CLARIDAD.
    //
    // Queja de uso, y tenía razón: «en arandelas-1 el fondo es rojo, y solo
    // detecta las piezas de color gris o cromado, las demás no las toma en
    // cuenta».
    //
    // Lo primero que hacía esta función era tirar el color —`cvtColor` a gris—,
    // y ahí se pierde justo lo que separa una arandela de latón de un cartón
    // rojo: el TONO. En claridad son casi la misma cosa.
    //
    // Medido sobre `arandelas-1.png` —DIECINUEVE arandelas surtidas sobre cartón
    // rojo, contadas dibujando las detecciones encima de la foto—, con el área
    // mínima de fábrica:
    //
    //     por claridad     4 manchas
    //     por color       11 manchas
    //
    // y bajando además el área mínima al 0,1 %, veinte manchas, de las que
    // DIECISIETE son arandelas enteras. Las que aparecen al encender el color
    // son las que en claridad se confunden con la mesa: la de caucho negro, la
    // de fibra marrón, la de fibra gris, el aro dentado de latón y las pequeñas
    // de latón.
    //
    // (Este comentario decía antes «7 y 20 piezas». Ninguno de los dos se
    // sostiene hoy —el pipeline ha cambiado— y sobre todo el «20» eran MANCHAS,
    // con la barra de escala y su rótulo dentro. Queda dicho para que no vuelva
    // a colarse un recuento de manchas como si fuera de piezas.)
    //
    // Cómo: se mide la distancia de cada píxel al color del fondo en Lab —que
    // separa la claridad del tono, que es el problema— y esa distancia se
    // segmenta con la MISMA maquinaria de siempre (Otsu, morfología, y sobre
    // todo la recuperación por histéresis). No hay un umbral nuevo que ajustar.
    //
    // Se probó antes a cortar la distancia con Otsu a secas y NO vale: Otsu
    // supone dos poblaciones, y en una bandeja de tuercas sobre fondo blanco hay
    // tres —fondo, cuerpo cromado (cerca del blanco) y nylon azul del inserto
    // (lejos)—. El corte caía en medio y la máscara marcaba solo los aros
    // azules, dejando el cromado del lado del fondo. El recuento de piezas decía
    // 100 en los dos casos: solo mirando la imagen se veía. La histéresis lo
    // arregla porque es el mismo problema que el brillo — parte de la pieza está
    // al nivel del fondo — y para eso se escribió.
    //
    // NACE APAGADO, como todo lo que mueve una medida.
    enum class BackgroundKey {
        Off,    // como siempre: se segmenta la claridad
        Auto,   // el color del fondo se toma de la mediana del marco
        Fixed,  // lo dice quien monta la estación
    };
    BackgroundKey backgroundKey = BackgroundKey::Off;
    // Solo con `Fixed`. En BGR, como todo lo que entra por OpenCV.
    cv::Vec3b background{0, 0, 0};

    int manualThreshold = -1;  // -1 = umbral automático (Otsu); 0-255 manual
    SegmentationPolarity polarity = SegmentationPolarity::Auto;
    int blurKernel = 5;   // suavizado previo (impar; <3 = sin suavizado)
    int morphKernel = 5;  // limpieza morfológica (impar; <3 = sin morfología)
};

// El color del fondo, estimado del MARCO de la imagen.
//
// La pieza está en medio y el borde es fondo casi siempre. Se usa la MEDIANA y
// no la media porque en una bandeja llena las piezas llegan al marco: medido en
// la bandeja de cien tuercas, la mediana del borde sale (248,244,243) —blanco,
// correcto— mientras que cualquier estadístico que mire la cola se contamina
// con las tuercas. Aguanta mientras menos de la mitad del borde sea pieza.
//
// Devuelve un color BGR. Con una imagen de un solo canal devuelve el gris
// repetido, que es lo coherente.
[[nodiscard]] cv::Vec3b estimateBackgroundColour(const cv::Mat& image);

// CUÁNTO COLOR TIENE EL FONDO, de 0 (gris puro) a 1.
//
// Existe para poder AVISAR. La clave de color nace apagada porque cambia lo que
// se mide, y eso está bien; el problema es que una opción apagada que nadie sabe
// que existe es una opción que no existe. Y quien la necesita es justo el que no
// va a ir a buscarla: está viendo que «no detecta bien» y no tiene por qué
// sospechar del color de su mesa.
//
// Es la saturación de HSV, calculada sobre UN píxel —el color del fondo ya
// estimado— y no convirtiendo la imagen entera: mirar un millón de píxeles para
// leer uno sería pagar de más en el camino que corre con el vídeo.
//
// Medido sobre el banco de fotos: el cartón rojo da 0,735 y las siete mesas
// blancas van de 0,000 a 0,020. Un factor de treinta y siete entre los dos
// grupos, así que el umbral no es una elección delicada.
[[nodiscard]] double backgroundColourfulness(const cv::Vec3b& background);

// Distancia de cada píxel al color del fondo, en Lab, como imagen de un canal.
//
// Lab y no BGR porque lo que hace falta es «cuánto se PARECE», y en BGR la
// distancia euclídea no significa eso. Saturada a 255: por encima ya da igual
// cuánto más lejos esté.
[[nodiscard]] cv::Mat distanceToBackground(const cv::Mat& bgr, const cv::Vec3b& background);

// LO QUE UN PARCHE DE FONDO DICE DE SÍ MISMO.
//
// Petición del taller: «lo del color de fondo, al momento de seleccionarlo, el
// usuario debería poder recortar o seleccionar un área del fondo por la
// textura, y la descarte, para poder tomar las piezas correctamente».
//
// Las dos formas que había de decir cuál es el fondo fallan por sitios
// distintos, y las dos fallan:
//
//     la mediana del marco   se contamina cuando la pieza llega al borde
//     el selector de color   pide un RGB que nadie sabe de su propia mesa
//
// Señalar un trozo de mesa es lo único que un operador puede hacer sin saber
// nada de ninguna de las dos cosas.
//
// Devuelve el color —la mediana del parche— y CUÁNTO VARÍA ese parche consigo
// mismo: el p95 de la distancia Lab de sus píxeles a esa mediana. El p95 y no
// el máximo porque una mota de suciedad no puede hablar por toda la mesa.
//
// LA DISPERSIÓN NO SE RESTA DE NADA, y esto se probó antes de decidirlo.
//
// La idea era descontarla de la distancia al fondo para que la veta de la mesa
// colapsara a cero. Medido sobre `arandelas-1` con el pipeline entero, cinco
// parches distintos:
//
//     parche             tolerancia   restando 0        restando la tolerancia
//     cuadro limpio          5        12 piezas 23,9 %   12 piezas 23,7 %
//     franja izquierda      13        11 piezas 22,5 %   11 piezas 22,1 %
//     franja de abajo        9        11 piezas 21,8 %   11 piezas 21,7 %
//     franja de arriba     110        12 piezas 23,8 %    1 pieza    2,2 %
//     el marco entero      104        11 piezas 22,9 %    1 pieza    4,5 %
//
// Cuando el parche es fondo de verdad la tolerancia es pequeña y no cambia
// nada; cuando no lo es, restarla borra la escena. Una pieza de maquinaria que
// en el mejor caso no hace nada y en el peor apaga la detección no se queda
// «por si acaso»: si algún día aparece una mesa con veta de verdad —madera, un
// tapete impreso—, se mide entonces y se decide con números.
//
// LO QUE SÍ HACE LA DISPERSIÓN ES AVISAR, que es donde estaba el valor.
// `looksUniform` dice si lo señalado parece fondo. Barriendo el banco de fotos
// entero con parches de 64x64:
//
//     mesa de estudio, blanca y plana       0,0
//     cartón rojo real, con su veta         4,2   <- el más bajo de esa foto
//     cualquier parche que pilla pieza     45 en adelante
//     bandeja de cien tuercas             143 EL MEJOR — no hay mesa que ver
//
// El hueco entre 4 y 45 es de diez veces, así que el corte en 25 no es una
// elección delicada. Y la última línea es la que hace falta enseñar: hay
// escenas donde no existe ningún parche de fondo, y entonces lo honrado es
// decirlo y no dejar que el operador señale tuercas creyendo que señala mesa.
inline constexpr double kBackgroundPatchIsUniform = 25.0;

struct BackgroundSample {
    cv::Vec3b colour{0, 0, 0};
    double spread = 0.0;        // p95 de la distancia Lab del parche a su mediana
    bool looksUniform = false;  // spread <= kBackgroundPatchIsUniform
    bool valid = false;         // había parche que mirar
};

[[nodiscard]] BackgroundSample sampleBackground(const cv::Mat& image, const cv::Rect& patch);

// Segmenta la pieza del fondo por umbral + morfología. Devuelve máscara
// binaria CV_8UC1 con pieza = 255.
core::Result<cv::Mat> segmentPiece(const cv::Mat& image,
                                   const SegmentationOptions& options = {});

}  // namespace pci::vision
