// SEGMENTAR POR EL BORDE: qué arregla y qué no.
//
// Viene de una queja de uso con tuercas: ni el recuento ni el contorno salían
// bien. Al mirarlo, la causa no era un ajuste mal puesto — era que la pregunta
// estaba mal hecha.
//
// Otsu pregunta «¿este píxel es más claro o más oscuro que el corte?», y eso da
// por supuesto que la pieza cae entera de un lado. Sobre una foto de siete
// tuercas surtidas en una mesa clara, medido:
//
//     fondo                    176, desviación 7
//     interiores de las piezas medias de 96 a 172, valores de 12 a 252
//     corte de Otsu            134
//
// Las piezas tienen reflejos por encima del fondo y sombras por debajo. El corte
// que recoge una deja fuera a otra, y lo que sale son seis contornos en vez de
// siete, con tres piezas fundidas por puentes de sombra.
//
// Este fichero comprueba las DOS mitades: que segmentar por el borde arregla esa
// escena, y que NO sirve para las demás. La segunda mitad es la que impide que
// esto se convierta en un cambio de algoritmo en vez de una opción.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "vision/contour_analysis.h"
#include "vision/edge_segmentation.h"
#include "vision/segmentation.h"

using pci::vision::edgeSegmentationLooksBetter;
using pci::vision::readScene;
using pci::vision::segmentByEdges;

namespace {

std::filesystem::path corpus() {
    for (const auto* candidate : {"testdata/real", "../testdata/real", "../../testdata/real",
                                  "../../../testdata/real"}) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return std::filesystem::path(candidate);
        }
    }
    return {};
}

cv::Mat photo(const std::string& name) {
    const auto dir = corpus();
    return dir.empty() ? cv::Mat()
                       : cv::imread((dir / name).string(), cv::IMREAD_COLOR);
}

int piecesIn(const cv::Mat& mask) {
    return static_cast<int>(pci::vision::findPieceContours(mask).size());
}

// Lo que encuentra el camino de siempre, para tener con qué comparar.
int piecesWithOtsu(const cv::Mat& image) {
    const auto mask = pci::vision::segmentPiece(image);
    return mask.isOk() ? piecesIn(mask.value()) : 0;
}

}  // namespace

// LO QUE ARREGLA.
TEST(EdgeSegmentation, TheNutsSceneNeedsItAndOtsuCannotDoIt) {
    const cv::Mat nuts = photo("tuerca_dominio_publico.jpg");
    if (nuts.empty()) {
        GTEST_SKIP() << "corpus no descargado: python3 testdata/fetch_real_images.py";
    }

    // Primero, POR QUÉ Otsu no puede: las piezas caen a los dos lados del fondo.
    const auto reading = readScene(nuts);
    std::printf("  [borde] fondo %.0f (ruido %.1f); más claro que él %.1f %%, más oscuro "
                "%.1f %%\n",
                reading.backgroundLevel, reading.backgroundNoise,
                100.0 * reading.brighterThanBackground,
                100.0 * reading.darkerThanBackground);
    std::printf("  [borde] lectura: %s\n", reading.summary.c_str());
    EXPECT_TRUE(reading.piecesStraddleTheBackground)
        << "la escena de las tuercas no se reconoce como el caso que rompe a Otsu: "
           "entonces esta prueba no está midiendo lo que cree";

    const int byLevel = piecesWithOtsu(nuts);
    const auto byEdge = segmentByEdges(nuts);
    ASSERT_TRUE(byEdge.isOk()) << byEdge.error().message;
    const int byEdgeCount = piecesIn(byEdge.value());

    std::printf("  [borde] siete tuercas -> por nivel %d piezas, por borde %d\n", byLevel,
                byEdgeCount);
    EXPECT_EQ(byEdgeCount, 7)
        << "segmentando por el borde no salen las siete tuercas, que es lo único para lo "
           "que esto se ha metido";
    EXPECT_LT(byLevel, 7) << "Otsu ya encontraba las siete: entonces no hay problema que "
                             "resolver y este módulo sobra";
}

// LO QUE NO ARREGLA, y por eso es una opción y no un cambio de algoritmo.
//
// Sin esta mitad, «segmentar por el borde» parecería mejor a secas y acabaría
// puesto por defecto — y rompería las escenas que hoy funcionan.
TEST(EdgeSegmentation, OnTheEasyScenesItIsWorseAndTheReadingSaysSo) {
    if (corpus().empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }

    struct Case {
        const char* file;
        int expected;
    };
    const Case easy[] = {
        {"bola_oscura_sobre_claro_20mm.jpg", 1},
        {"bola_oscura_sobre_claro_10mm.jpg", 1},
    };

    int worseSomewhere = 0;
    for (const auto& one : easy) {
        const cv::Mat image = photo(one.file);
        if (image.empty()) {
            continue;
        }
        const int byLevel = piecesWithOtsu(image);
        const auto byEdge = segmentByEdges(image);
        const int byEdgeCount = byEdge.isOk() ? piecesIn(byEdge.value()) : 0;
        const bool suggested = edgeSegmentationLooksBetter(image);
        std::printf("  [borde] %-34s espera %d -> nivel %d, borde %d  (¿se ofrece? %s)\n",
                    one.file, one.expected, byLevel, byEdgeCount, suggested ? "sí" : "no");
        if (std::abs(byEdgeCount - one.expected) > std::abs(byLevel - one.expected)) {
            ++worseSomewhere;
        }
        // LA PARTE QUE IMPORTA: en estas escenas el programa NO lo ofrece.
        EXPECT_FALSE(suggested)
            << one.file
            << ": se ofrece segmentar por el borde en una escena donde el umbral por "
               "nivel funciona mejor. Ofrecer el método equivocado es peor que no "
               "ofrecer ninguno, porque el operador se fía";
    }
    EXPECT_GT(worseSomewhere, 0)
        << "el borde no es peor en ninguna escena fácil: si de verdad fuese igual de "
           "bueno en todas, debería ir por defecto y no ser una opción";
}

// Cuando el borde no cierra, se dice — no se devuelve una máscara vacía.
//
// Un «no se detecta pieza» sin motivo deja al operador buscando el fallo en su
// iluminación cuando lo que pasa es que el método no era el suyo.
TEST(EdgeSegmentation, WhenTheEdgeDoesNotCloseItSaysWhy) {
    // Una imagen sin ningún borde: gris liso con algo de grano.
    cv::Mat flat(400, 600, CV_8UC1, cv::Scalar(128));
    cv::Mat noise(flat.size(), CV_8UC1);
    cv::randn(noise, 0, 2);
    flat += noise;

    const auto result = segmentByEdges(flat);
    ASSERT_FALSE(result.isOk());
    std::printf("  [borde] sin cantos dice: %s\n", result.error().message.c_str());
    EXPECT_NE(result.error().message.find("nivel"), std::string::npos)
        << "no se sugiere el otro método, que es lo único que el operador puede hacer";

    // Y una imagen imposible tampoco revienta.
    EXPECT_FALSE(segmentByEdges({}).isOk());
    EXPECT_FALSE(segmentByEdges(cv::Mat(8, 8, CV_8UC1, cv::Scalar(0))).isOk());
}

// La lectura de la escena distingue los dos casos, que es de lo que depende todo
// lo demás.
TEST(EdgeSegmentation, TheSceneReadingTellsTheTwoCasesApart) {
    // Caso fácil: pieza oscura sobre fondo claro, todo de un lado.
    cv::Mat oneSided(400, 600, CV_8UC1, cv::Scalar(210));
    cv::rectangle(oneSided, cv::Rect(200, 120, 200, 160), cv::Scalar(40), cv::FILLED);
    const auto easy = readScene(oneSided);
    std::printf("  [borde] un solo lado: claro %.1f %%, oscuro %.1f %% -> %s\n",
                100.0 * easy.brighterThanBackground, 100.0 * easy.darkerThanBackground,
                easy.piecesStraddleTheBackground ? "a los dos lados" : "de un lado");
    EXPECT_FALSE(easy.piecesStraddleTheBackground);
    EXPECT_NEAR(easy.backgroundLevel, 210.0, 3.0);

    // Caso difícil: una pieza con reflejo y sombra, a los dos lados del fondo.
    cv::Mat twoSided(400, 600, CV_8UC1, cv::Scalar(150));
    cv::rectangle(twoSided, cv::Rect(150, 100, 140, 200), cv::Scalar(60), cv::FILLED);
    cv::rectangle(twoSided, cv::Rect(320, 100, 140, 200), cv::Scalar(245), cv::FILLED);
    const auto hard = readScene(twoSided);
    std::printf("  [borde] dos lados:    claro %.1f %%, oscuro %.1f %% -> %s\n",
                100.0 * hard.brighterThanBackground, 100.0 * hard.darkerThanBackground,
                hard.piecesStraddleTheBackground ? "a los dos lados" : "de un lado");
    EXPECT_TRUE(hard.piecesStraddleTheBackground);
    EXPECT_FALSE(hard.summary.empty());
}
