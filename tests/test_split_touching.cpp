// «SI LAS PIEZAS ESTÁN MUY PEGADAS, LAS DETECTA COMO UNA SOLA».
//
// Queja literal, y era exacta: `RETR_EXTERNAL` devuelve una sola mancha cuando
// dos piezas se rozan. Dos engranajes engranados salían como UNA pieza — y con
// una pieza no hay nada que recorrer con las flechas ni que enseñar en el
// mosaico, así que el operador se queda sin forma de mirarlas por separado.
//
// La técnica: mirar cada mancha POR DENTRO. Se calcula su transformada de
// distancia y se buscan los «corazones», las zonas más alejadas del fondo. Dos
// piezas pegadas tienen dos corazones separados por un cuello estrecho; una
// pieza sola tiene uno.
//
// DOS COSAS QUE COSTARON UNA VUELTA CADA UNA:
//
//  1. Un umbral GLOBAL sobre la imagen entera no vale. Medido: el valor que
//     separa los engranajes (0,5 del radio máximo de la imagen) deja la bandeja
//     de cien tuercas en 104 piezas, y el que arregla los tornillos (0,7) la
//     deja en CERO. El umbral tiene que ser relativo al radio de CADA mancha.
//
//  2. El watershed sobre la máscara BINARIA no corta. Dentro de la pieza todo
//     vale lo mismo, así que no hay relieve que seguir: dibujaba una línea
//     —quitaba 1 713 píxeles— y los dos engranajes seguían saliendo como un
//     solo contorno. Sobre la distancia INVERTIDA sí, porque ahí el cuello es
//     una cresta.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <filesystem>

#include "synthetic_scenes.h"
#include "vision/contour_analysis.h"
#include "vision/pipeline.h"

using namespace pci;
using namespace pci::testing_support;

namespace {

std::filesystem::path ownImages() {
    const std::filesystem::path dir("C:/Users/furro/Pictures/IMG-MC");
    std::error_code ec;
    return std::filesystem::exists(dir, ec) ? dir : std::filesystem::path();
}

int piecesIn(const cv::Mat& gray, bool split) {
    vision::PipelineConfig config;
    config.segmentation.splitTouchingPieces = split;
    auto all = vision::analyzeFrames(gray, config);
    return all.isOk() ? static_cast<int>(all.value().size()) : -1;
}

}  // namespace

TEST(SplitTouching, TwoDiscsThatTouchAreCountedAsTwo) {
    // Dos discos de 260 px de diámetro que se solapan 20 px: eso es «tocarse».
    //
    // La primera versión los ponía solapando 80 px y fallaba — con razón: eso no
    // es tocarse, es superponerse, y por ahí ya no pasa el cuello. El límite
    // está medido en la prueba de más abajo en vez de descubrirse a golpes.
    cv::Mat scene(400, 800, CV_8UC1, cv::Scalar(kSceneBackground));
    cv::circle(scene, {280, 200}, 130, cv::Scalar(kScenePiece), cv::FILLED, cv::LINE_8);
    cv::circle(scene, {520, 200}, 130, cv::Scalar(kScenePiece), cv::FILLED, cv::LINE_8);

    const int plain = piecesIn(scene, false);
    const int split = piecesIn(scene, true);
    std::printf("  [separar] dos discos que se solapan: normal %d | separando %d\n", plain,
                split);
    EXPECT_EQ(plain, 1) << "la escena ya no reproduce el caso: los discos no se tocan";
    EXPECT_EQ(split, 2) << "no separa dos discos que se tocan, que es el caso más simple";
}

TEST(SplitTouching, ASinglePieceIsNeverCutInTwo) {
    // LA MITAD QUE DECIDE SI SIRVE. Una técnica que separa dos engranajes pero
    // parte cada tuerca en tres es peor que no tener nada.
    cv::Mat scene(400, 400, CV_8UC1, cv::Scalar(kSceneBackground));
    cv::circle(scene, {200, 200}, 150, cv::Scalar(kScenePiece), cv::FILLED, cv::LINE_8);
    EXPECT_EQ(piecesIn(scene, true), 1) << "parte un disco solo por la mitad";

    const GearScene gear = testing_support::gear();
    const int gearPieces = piecesIn(gear.gray, true);
    std::printf("  [separar] un engranaje solo: %d piezas\n", gearPieces);
    EXPECT_EQ(gearPieces, 1)
        << "parte un engranaje solo: sus dientes o su agujero pasan por piezas";
}

TEST(SplitTouching, TheTrayOfAHundredNutsSurvives) {
    // El caso que mató al umbral global: cien piezas pequeñas. Con un umbral
    // relativo a la imagen entera la bandeja pasaba de 100 piezas a 0.
    if (ownImages().empty()) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }
    const cv::Mat tray =
        cv::imread((ownImages() / "producto-tuercas-prueba.jpg").string(),
                   cv::IMREAD_GRAYSCALE);
    if (tray.empty()) {
        GTEST_SKIP() << "no se pudo leer la bandeja";
    }
    const int plain = piecesIn(tray, false);
    const int split = piecesIn(tray, true);
    std::printf("  [separar] bandeja de cien: normal %d | separando %d\n", plain, split);
    EXPECT_EQ(plain, 100);
    EXPECT_EQ(split, 100)
        << "separar destroza la bandeja de cien tuercas, que es lo que le pasaba al "
           "umbral global";
}

TEST(SplitTouching, TheTwoMeshedGearsAreFinallyTwo) {
    // La imagen del usuario, que es de donde salió la queja.
    if (ownImages().empty()) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }
    const cv::Mat gears =
        cv::imread((ownImages() / "engranajes-1.jpg").string(), cv::IMREAD_GRAYSCALE);
    if (gears.empty()) {
        GTEST_SKIP() << "no se pudo leer los engranajes";
    }
    const int plain = piecesIn(gears, false);
    const int split = piecesIn(gears, true);
    std::printf("  [separar] dos engranajes engranados: normal %d | separando %d\n", plain,
                split);
    EXPECT_EQ(plain, 1) << "la imagen ya no reproduce el caso de piezas fundidas";
    EXPECT_EQ(split, 2)
        << "sigue contando como una los dos engranajes engranados, que es la queja "
           "que trajo todo esto";
}

TEST(SplitTouching, ItIsOffByDefaultBecauseItDoesNotAlwaysWin) {
    // Nace apagado, y no por prudencia genérica: está medido que rompe un caso.
    // Un tornillo largo tiene la cabeza y el vástago lo bastante distintos como
    // para parecer dos corazones.
    //
    // Que sea opción es la misma decisión que con «por el canto», y por la
    // misma razón: gana en unas escenas y pierde en otras, así que la elige
    // quien conoce sus piezas.
    const vision::SegmentationOptions factory;
    EXPECT_FALSE(factory.splitTouchingPieces)
        << "viene encendido de fábrica, y está medido que parte un tornillo largo "
           "en dos";
}

// HASTA DÓNDE LLEGA, medido y no supuesto.
//
// El operador necesita saber cuándo esperar que funcione. La respuesta no es
// «si se tocan» sino «cuánto se solapan», y eso se mide: con discos de 260 px de
// diámetro, separa hasta 35 píxeles de solape (un 13 % del diámetro) y se rinde
// a partir de 50.
//
// Rendirse es lo correcto ahí: con medio disco dentro del otro, el cuello es
// tan ancho como las propias piezas y ninguna técnica basada en la forma puede
// saber dónde acaba una. Lo que importa es que se rinda DEVOLVIENDO UNA pieza y
// no inventando tres.
TEST(SplitTouching, TheReachIsMeasuredAndItGivesUpCleanly) {
    struct Caso {
        int overlap;
        int expected;
        const char* nota;
    };
    const Caso casos[] = {
        {0, 2, "se rozan sin solapar"},
        {20, 2, "solape pequeño"},
        {35, 2, "solape del 13 % del diámetro: el límite"},
        {50, 1, "medio dentro: se rinde, y devuelve UNA"},
        {80, 1, "muy dentro: se rinde"},
    };
    for (const auto& caso : casos) {
        const int gap = 260 - caso.overlap;
        cv::Mat scene(400, 800, CV_8UC1, cv::Scalar(kSceneBackground));
        cv::circle(scene, {400 - gap / 2, 200}, 130, cv::Scalar(kScenePiece), cv::FILLED,
                   cv::LINE_8);
        cv::circle(scene, {400 + gap / 2, 200}, 130, cv::Scalar(kScenePiece), cv::FILLED,
                   cv::LINE_8);
        const int got = piecesIn(scene, true);
        std::printf("  [separar] solape %2d px -> %d piezas (%s)\n",
                    caso.overlap, got, caso.nota);
        EXPECT_EQ(got, caso.expected) << caso.nota;
    }
}
