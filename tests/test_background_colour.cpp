// SEPARAR POR EL COLOR DEL FONDO, NO POR LA CLARIDAD.
//
// Queja de uso, y tenía razón: «en arandelas-1 el fondo es rojo, y solo detecta
// las piezas de color gris o cromado, las demás no las toma en cuenta».
//
// Lo primero que hacía `segmentPiece` con una foto en color era tirar el color.
// Y ahí se pierde justo lo que separa una arandela de latón de un cartón rojo:
// el TONO. En claridad son casi lo mismo — el rojo de esa foto cae en gris 116,
// un gris medio.
//
// LO QUE SE MIDIÓ, y por qué la primera versión no valía:
//
//   - Cortar la distancia al fondo con Otsu a secas NO funciona. Otsu supone dos
//     poblaciones y en una bandeja de tuercas sobre fondo blanco hay tres: el
//     fondo, el cuerpo cromado (cerca del blanco) y el nylon azul del inserto
//     (lejos). El corte caía en medio y la máscara marcaba SOLO los aros azules.
//     El recuento de piezas decía 100 en los dos casos y no delataba nada: hubo
//     que dibujar las máscaras encima de la foto y mirarlas.
//   - Sacar el umbral del ruido del propio marco tampoco: en una bandeja llena,
//     las piezas LLEGAN al marco y ese ruido sale de las piezas, no del fondo.
//     Con eso, tres de las cinco fotos se quedaban en cero piezas.
//   - Lo que sí funciona es pasarle la imagen de distancias a la maquinaria de
//     siempre, con su recuperación por histéresis. Es el mismo problema que el
//     brillo —parte de la pieza está al nivel del fondo— y para eso se escribió.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "vision/segmentation.h"

using namespace pci;

namespace {

cv::Mat loadColour(const char* file) {
    const std::filesystem::path path =
        std::filesystem::path("C:/Users/furro/Pictures/IMG-MC") / file;
    std::error_code ec;
    return std::filesystem::exists(path, ec) ? cv::imread(path.string(), cv::IMREAD_COLOR)
                                             : cv::Mat();
}

int countPieces(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    int found = 0;
    for (const auto& c : contours) {
        if (cv::contourArea(c) >= 0.001 * static_cast<double>(mask.total())) {
            ++found;
        }
    }
    return found;
}

vision::SegmentationOptions byLightness() {
    vision::SegmentationOptions options;
    options.recoverHighlightsBy = 12;
    return options;
}

vision::SegmentationOptions byColour() {
    vision::SegmentationOptions options = byLightness();
    options.backgroundKey = vision::SegmentationOptions::BackgroundKey::Auto;
    return options;
}

}  // namespace

TEST(BackgroundColour, TheBorderMedianFindsTheBackgroundEvenWithPiecesTouchingIt) {
    // La mediana y no la media, y por un motivo medido: en la bandeja de cien
    // tuercas las piezas llegan al marco, así que cualquier estadístico que mire
    // la cola se contamina. La mediana aguanta mientras menos de la mitad del
    // borde sea pieza.
    struct Expected {
        const char* file;
        int b;
        int g;
        int r;
        int slack;
    };
    const Expected all[] = {
        {"arandelas-1.png", 77, 63, 238, 25},           // cartón rojo
        {"producto-tuercas-prueba.jpg", 248, 244, 243, 20},  // bandeja llena, fondo blanco
        {"engranaje-1.png", 255, 255, 255, 6},
    };
    for (const auto& one : all) {
        const cv::Mat bgr = loadColour(one.file);
        if (bgr.empty()) {
            continue;
        }
        const cv::Vec3b found = vision::estimateBackgroundColour(bgr);
        std::printf("  %-30s fondo BGR(%3d,%3d,%3d)\n", one.file, found[0], found[1], found[2]);
        EXPECT_NEAR(found[0], one.b, one.slack) << one.file;
        EXPECT_NEAR(found[1], one.g, one.slack) << one.file;
        EXPECT_NEAR(found[2], one.r, one.slack) << one.file;
    }
}

TEST(BackgroundColour, OnARedBackgroundItFindsThePiecesThatAreNotChrome) {
    // El caso de la queja. `arandelas-1.png` tiene una veintena de arandelas de
    // materiales muy distintos —acero, latón, cobre, caucho negro, fibra marrón,
    // fibra gris, plástico traslúcido— sobre cartón rojo.
    //
    // NO se le fija un número exacto de piezas: son unas veinte de tamaños muy
    // dispares y contarlas a ojo con fiabilidad no se puede. Lo que se fija es la
    // COMPARACIÓN, que sí es sólida: por color tienen que salir bastantes más.
    const cv::Mat bgr = loadColour("arandelas-1.png");
    if (bgr.empty()) {
        GTEST_SKIP() << "no está el banco de fotos";
    }
    const auto lightness = vision::segmentPiece(bgr, byLightness());
    const auto colour = vision::segmentPiece(bgr, byColour());
    ASSERT_TRUE(lightness.isOk());
    ASSERT_TRUE(colour.isOk());

    const int byGrey = countPieces(lightness.value());
    const int byHue = countPieces(colour.value());
    const double areaGrey = cv::countNonZero(lightness.value()) * 100.0 / bgr.total();
    const double areaHue = cv::countNonZero(colour.value()) * 100.0 / bgr.total();
    std::printf("  [fondo rojo] por claridad %2d piezas (%.1f %%), por color %2d piezas (%.1f %%)\n",
                byGrey, areaGrey, byHue, areaHue);

    EXPECT_GT(byHue, byGrey + 5)
        << "sobre el cartón rojo, separar por color ya no encuentra más piezas que separar "
           "por claridad. Las de latón, cobre, caucho y fibra tienen casi la misma "
           "claridad que el fondo: si esto no gana, la clave de fondo no sirve para nada";
    EXPECT_GT(areaHue, areaGrey * 1.4)
        << "encuentra más manchas pero no más pieza: puede estar troceando las mismas";
}

TEST(BackgroundColour, OnAWhiteBackgroundItDoesNotMakeThingsWorse) {
    // La otra mitad, y la que decide si esto puede existir. El montaje normal de
    // esta aplicación es metal sobre mesa clara, y ahí la claridad ya funciona:
    // lo único que hace falta es que el camino nuevo no lo estropee.
    //
    // Se compara el ÁREA y no solo el recuento, porque el recuento no delata
    // nada: cuando la primera versión marcaba únicamente los aros de nylon de las
    // tuercas, seguía diciendo «100 piezas».
    struct Shot {
        const char* file;
        int pieces;
    };
    const Shot all[] = {
        {"engranaje-1.png", 1},
        {"tornillo-ojo-3.png", 1},
        {"producto-tuercas-prueba.jpg", 100},
        {"arandelas-3.jpg", -1},  // varias arandelas: se publica, no se fija
    };
    for (const auto& shot : all) {
        const cv::Mat bgr = loadColour(shot.file);
        if (bgr.empty()) {
            continue;
        }
        const auto lightness = vision::segmentPiece(bgr, byLightness());
        const auto colour = vision::segmentPiece(bgr, byColour());
        ASSERT_TRUE(lightness.isOk());
        ASSERT_TRUE(colour.isOk());
        const int byGrey = countPieces(lightness.value());
        const int byHue = countPieces(colour.value());
        const double areaGrey = cv::countNonZero(lightness.value()) * 100.0 / bgr.total();
        const double areaHue = cv::countNonZero(colour.value()) * 100.0 / bgr.total();
        std::printf("  %-30s claridad %3d (%.1f %%)   color %3d (%.1f %%)\n", shot.file, byGrey,
                    areaGrey, byHue, areaHue);

        if (shot.pieces > 0) {
            EXPECT_EQ(byHue, shot.pieces)
                << shot.file << ": la clave de fondo cambia cuántas piezas se ven sobre un "
                   "fondo claro, que es donde la claridad ya acertaba";
        }
        // La mitad del área es el listón que habría cazado el fallo de los aros
        // de nylon, que dejaba fuera todo el cuerpo cromado de cien tuercas.
        EXPECT_GT(areaHue, areaGrey * 0.5)
            << shot.file << ": por color se encuentra menos de la mitad de pieza que por "
               "claridad. Así se veía el fallo de marcar solo el inserto azul de las "
               "tuercas y dejar el cromado del lado del fondo";
    }
}

TEST(BackgroundColour, ItIsOffUntilSomebodyTurnsItOn) {
    // Cambia lo que se mide, así que se elige. Una pieza ya registrada tiene sus
    // tolerancias ajustadas contra el borde de ANTES.
    const vision::SegmentationOptions defaults;
    EXPECT_EQ(defaults.backgroundKey, vision::SegmentationOptions::BackgroundKey::Off);

    const cv::Mat bgr = loadColour("engranaje-1.png");
    if (bgr.empty()) {
        GTEST_SKIP();
    }
    const auto plain = vision::segmentPiece(bgr, vision::SegmentationOptions{});
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    const auto asGrey = vision::segmentPiece(gray, vision::SegmentationOptions{});
    ASSERT_TRUE(plain.isOk());
    ASSERT_TRUE(asGrey.isOk());
    EXPECT_EQ(cv::countNonZero(plain.value() != asGrey.value()), 0)
        << "con la clave apagada, pasarle la foto en color y en gris ya no da lo mismo: "
           "alguien ha cambiado el camino de siempre sin querer";
}

TEST(BackgroundColour, TellingItTheColourWorksAsWellAsGuessingIt) {
    // La otra mitad de la petición: «poder decirle al programa de qué color es el
    // fondo». Quien monta la estación lo sabe, y decirlo evita que un montón de
    // piezas en el marco despiste a la estimación.
    const cv::Mat bgr = loadColour("arandelas-1.png");
    if (bgr.empty()) {
        GTEST_SKIP();
    }
    const cv::Vec3b guessed = vision::estimateBackgroundColour(bgr);

    vision::SegmentationOptions told = byLightness();
    told.backgroundKey = vision::SegmentationOptions::BackgroundKey::Fixed;
    told.background = guessed;

    const auto automatic = vision::segmentPiece(bgr, byColour());
    const auto declared = vision::segmentPiece(bgr, told);
    ASSERT_TRUE(automatic.isOk());
    ASSERT_TRUE(declared.isOk());
    EXPECT_EQ(cv::countNonZero(automatic.value() != declared.value()), 0)
        << "decirle el mismo color que habría adivinado da un resultado distinto: entonces "
           "uno de los dos caminos no usa el color que dice usar";
}
