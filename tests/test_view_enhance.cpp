// REALZAR LO QUE SE VE SIN TOCAR LO QUE SE MIDE.
//
// La queja: «si la pieza es negra, y el demás cuadro es negro no se alcanza a
// ver correctamente». Una pieza mate oscura sobre fondo oscuro ocupa treinta
// niveles de gris de los 256 que hay: en pantalla es una mancha negra dentro de
// otra mancha negra, y la detección puede estar funcionando perfectamente sin
// que el operador tenga forma de saberlo.
//
// Lo que se comprueba aquí son las dos mitades del trato:
//
//   1. Que el realce SIRVA en la escena que lo pide, y que resista lo que hay en
//      una mesa de verdad (un píxel muerto, un reflejo quemado).
//   2. Que NO toque la imagen original. Ya hay una forma de subir el brillo en
//      el programa —los controles de la cámara— y esa sí cambia el fotograma que
//      se analiza. Confundir las dos haría que las cotas se movieran por mirar.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cstdio>

#include "vision/view_enhance.h"

using pci::vision::applyStretch;
using pci::vision::autoContrastLut;
using pci::vision::kAlreadyWideRange;

namespace {

// La escena del problema: pieza casi negra sobre fondo casi negro.
cv::Mat blackOnBlack() {
    cv::Mat image(300, 400, CV_8UC1, cv::Scalar(18));
    cv::rectangle(image, cv::Rect(120, 90, 160, 120), cv::Scalar(46), cv::FILLED);
    return image;
}

double meanIn(const cv::Mat& image, const cv::Rect& area) {
    return cv::mean(image(area))[0];
}

}  // namespace

TEST(ViewEnhance, ADarkPieceOnADarkBackgroundBecomesVisible) {
    const cv::Mat scene = blackOnBlack();
    const cv::Rect piece(140, 110, 100, 80);
    const cv::Rect background(20, 20, 80, 50);

    const double before = meanIn(scene, piece) - meanIn(scene, background);
    const auto stretch = autoContrastLut(scene);
    ASSERT_TRUE(stretch.useful) << "no vio nada que estirar en una escena de 18 a 46";
    const cv::Mat shown = applyStretch(scene, stretch);
    const double after = meanIn(shown, piece) - meanIn(shown, background);

    std::printf("  [ver] rango util %d..%d; separacion pieza/fondo %.0f -> %.0f niveles\n",
                stretch.low, stretch.high, before, after);
    EXPECT_LT(before, 35.0) << "la escena de la prueba no era tan oscura como se creía";
    EXPECT_GT(after, 200.0)
        << "tras realzar, la pieza sigue confundiéndose con el fondo: el realce no "
           "está sirviendo para lo único que existe";
}

// Y LA OTRA MITAD: la imagen original no se toca.
//
// Es la comprobación importante de este fichero. Si el realce llegara al
// fotograma que se analiza, movería el umbral de Otsu y con él todas las cotas:
// las medidas cambiarían por haber subido el brillo para poder ver.
TEST(ViewEnhance, TheOriginalImageIsNeverTouched) {
    const cv::Mat scene = blackOnBlack();
    const cv::Mat copy = scene.clone();

    const auto stretch = autoContrastLut(scene);
    const cv::Mat shown = applyStretch(scene, stretch);

    ASSERT_EQ(scene.size(), copy.size());
    EXPECT_EQ(cv::countNonZero(scene != copy), 0)
        << "realzar modificó la imagen de entrada: lo que se mide dejaría de ser lo "
           "que llegó de la cámara";
    // Y lo que sale es OTRA matriz, no una vista de la misma memoria.
    EXPECT_NE(shown.data, scene.data);
    EXPECT_GT(cv::countNonZero(shown != scene), 0) << "no realzó nada";
}

// Una imagen que ya usa la escala no se toca: aplicar una tabla identidad a cada
// fotograma cuesta una copia del frame y no se ve.
TEST(ViewEnhance, AnImageThatAlreadyUsesTheRangeIsLeftAlone) {
    cv::Mat wide(200, 200, CV_8UC1, cv::Scalar(10));
    wide(cv::Rect(50, 50, 100, 100)).setTo(cv::Scalar(245));

    const auto stretch = autoContrastLut(wide);
    std::printf("  [ver] imagen normal: rango %d..%d, hace falta realce = %s\n", stretch.low,
                stretch.high, stretch.useful ? "sí" : "no");
    EXPECT_FALSE(stretch.useful)
        << "dice que hace falta realzar una imagen que va de 10 a 245: se pagaría una "
           "copia por fotograma para no ver ninguna diferencia";
    EXPECT_GE(stretch.high - stretch.low, kAlreadyWideRange);
    // Y aplicarla igualmente devuelve lo mismo que entró.
    const cv::Mat shown = applyStretch(wide, stretch);
    EXPECT_EQ(cv::countNonZero(shown != wide), 0);
}

// PERCENTILES Y NO MÍNIMO/MÁXIMO, que es lo que hace que esto sirva de algo.
//
// Un píxel muerto en negro y un reflejo especular quemado en blanco son cosas
// normales en una mesa de inspección. Con mínimo y máximo, cualquiera de los dos
// lleva el rango a 0..255 y el realce deja de hacer nada — justo en las escenas
// difíciles, que es cuando se enciende.
TEST(ViewEnhance, ADeadPixelAndAHotHighlightDoNotDefeatIt) {
    cv::Mat scene = blackOnBlack();
    scene.at<unsigned char>(5, 5) = 0;      // píxel muerto
    scene.at<unsigned char>(7, 7) = 255;    // reflejo quemado
    // Y unos cuantos más, que un reflejo real no es un solo píxel.
    cv::circle(scene, {350, 40}, 2, cv::Scalar(255), cv::FILLED);

    const auto stretch = autoContrastLut(scene);
    std::printf("  [ver] con pixel muerto y reflejo: rango util %d..%d (la imagen va de "
                "0 a 255)\n",
                stretch.low, stretch.high);
    EXPECT_TRUE(stretch.useful)
        << "un puñado de píxeles extremos apagó el realce: con mínimo y máximo esto "
           "no funcionaría nunca en una mesa real";
    EXPECT_GT(stretch.low, 0) << "el píxel muerto arrastró el extremo bajo";
    EXPECT_LT(stretch.high, 255) << "el reflejo arrastró el extremo alto";
}

// Sin recorrido no se estira: con ocho niveles útiles, cada uno pasaría a ser un
// salto de 32 y lo que sale es un mapa de manchas, no una pieza.
TEST(ViewEnhance, AFlatImageIsNotStretchedIntoBands) {
    cv::Mat flat(100, 100, CV_8UC1, cv::Scalar(64));
    cv::Mat noise(flat.size(), CV_8UC1);
    cv::randn(noise, 0, 1);
    flat += noise;

    const auto stretch = autoContrastLut(flat);
    std::printf("  [ver] imagen plana: rango %d..%d, realce = %s\n", stretch.low,
                stretch.high, stretch.useful ? "sí" : "no");
    EXPECT_FALSE(stretch.useful) << "estiró el ruido del sensor hasta convertirlo en "
                                    "bandas: lo que se vería no es la pieza";
}

// Sobre color, la MISMA tabla a los tres canales: una por canal equilibraría los
// blancos y de paso cambiaría el color de la pieza, que es una de las cosas por
// las que el operador la reconoce.
TEST(ViewEnhance, ColourIsBrightenedWithoutBeingRecoloured) {
    // Una pieza rojiza oscura sobre fondo oscuro: el rojo tiene que seguir siendo
    // el canal dominante después de realzar.
    cv::Mat scene(200, 200, CV_8UC3, cv::Scalar(14, 14, 20));
    cv::rectangle(scene, cv::Rect(60, 60, 80, 80), cv::Scalar(20, 24, 52), cv::FILLED);

    const auto stretch = autoContrastLut(scene);
    ASSERT_TRUE(stretch.useful);
    const cv::Mat shown = applyStretch(scene, stretch);
    ASSERT_EQ(shown.channels(), 3);

    const cv::Scalar piece = cv::mean(shown(cv::Rect(70, 70, 60, 60)));
    std::printf("  [ver] pieza rojiza tras realzar: B=%.0f G=%.0f R=%.0f\n", piece[0],
                piece[1], piece[2]);
    EXPECT_GT(piece[2], piece[1]) << "el rojo dejó de dominar: el realce recoloreó la pieza";
    EXPECT_GT(piece[2], piece[0]);
    EXPECT_GT(piece[2], 100.0) << "la pieza sigue siendo casi negra tras realzar";
}

// Entradas imposibles devuelven la identidad y no revientan.
TEST(ViewEnhance, ImpossibleInputsGiveTheIdentity) {
    const auto empty = autoContrastLut({});
    EXPECT_FALSE(empty.useful);
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(empty.map(static_cast<unsigned char>(i)), i);
    }
    const cv::Mat floats(10, 10, CV_32FC1, cv::Scalar(0.5));
    EXPECT_FALSE(autoContrastLut(floats).useful);
    EXPECT_TRUE(applyStretch({}, empty).empty());
}
