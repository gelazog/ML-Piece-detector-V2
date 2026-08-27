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
    // rojo: el TONO. En claridad son casi la misma cosa. Medido sobre esa foto,
    // con el rojo del fondo en gris 116:
    //
    //     por claridad     7 piezas, 11,4 % del cuadro
    //     por color       20 piezas, 22,9 % del cuadro
    //
    // Y las trece que aparecen son exactamente las que faltaban: la de caucho
    // negro, la de fibra marrón, la de fibra gris, el aro dentado de latón, el
    // anillo de cobre y las pequeñas de latón.
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

// Segmenta la pieza del fondo por umbral + morfología. Devuelve máscara
// binaria CV_8UC1 con pieza = 255.
core::Result<cv::Mat> segmentPiece(const cv::Mat& image,
                                   const SegmentationOptions& options = {});

}  // namespace pci::vision
