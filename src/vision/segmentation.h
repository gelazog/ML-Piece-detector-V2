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
    int manualThreshold = -1;  // -1 = umbral automático (Otsu); 0-255 manual
    SegmentationPolarity polarity = SegmentationPolarity::Auto;
    int blurKernel = 5;   // suavizado previo (impar; <3 = sin suavizado)
    int morphKernel = 5;  // limpieza morfológica (impar; <3 = sin morfología)
};

// Segmenta la pieza del fondo por umbral + morfología. Devuelve máscara
// binaria CV_8UC1 con pieza = 255.
core::Result<cv::Mat> segmentPiece(const cv::Mat& image,
                                   const SegmentationOptions& options = {});

}  // namespace pci::vision
