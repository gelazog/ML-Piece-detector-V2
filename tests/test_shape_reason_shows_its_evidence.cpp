// «POLÍGONO DE 9 LADOS», DICHO CON LA MISMA CARA QUE UN HEXÁGONO DE VERDAD.
//
// El clasificador decide con DOS evidencias y solo publicaba una:
//
//   - la DESVIACIÓN dice si ese polígono explica el contorno («el punto peor se
//     separa 4,77 px de ellos»), y salía escrita;
//   - la MESETA dice si esos son los lados que tiene la pieza —en cuántos de los
//     treinta epsilon del barrido gana ese recuento— y no salía.
//
// Son evidencias independientes: el propio código lo tiene escrito en
// `kPlateauRulesAbove`, donde una meseta ancha manda sobre la tolerancia porque
// «la tolerancia dice si el polígono explica el contorno y la meseta dice cuántos
// lados tiene la pieza». Con esa segunda mitad callada, el informe afirmaba con
// el mismo aplomo una tuerca hexagonal reconocida en 17 de 30 barridos y esto:
//
//     arandelas-2.png             «Polígono de 9 lados»    aguanta  2 de 30
//     producto-tuercas-prueba.jpg «Polígono de 7 lados»    aguanta  4 de 30
//     arandelas-3.jpg             «Polígono de 7 lados»    aguanta  9 de 30
//
// Dos de treinta no es un recuento, es una casualidad que cabía en la
// tolerancia. Y el operador no tenía cómo distinguirla de una buena, porque las
// dos se leen igual.
//
// Es el mismo principio que ya estaba escrito para el residuo —«una clasificación
// sin su número es una opinión»—, solo que los números eran dos.
//
// Esta prueba no fija cuánto aguanta cada foto: eso es atar el banco. Fija que el
// número se publica y que sirve para algo, o sea, que no sale siempre igual.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>

#include <cstdio>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

#include "vision/geometry_features.h"
#include "vision/segmentation.h"
#include "vision/shape_class.h"

using namespace pci;

TEST(ShapeReason, APolygonSaysHowManySweepsItsCountSurvives) {
    const std::filesystem::path bank{"C:/Users/furro/Pictures/IMG-MC"};
    std::error_code ec;
    if (!std::filesystem::exists(bank, ec)) {
        GTEST_SKIP() << "las fotos del usuario no están en esta máquina";
    }

    // «aguanta N de M barridos», tal y como lo lee el operador. Se busca el texto
    // y no un campo de la estructura a propósito: lo que hay que garantizar es
    // que la evidencia LLEGA a la pantalla, y un campo público que nadie escribe
    // no llega a ninguna parte.
    const std::regex says(R"(aguanta (\d+) de (\d+) barridos)");

    int polygons = 0;
    int weakest = 999;
    int firmest = 0;
    for (const auto& entry : std::filesystem::directory_iterator(bank, ec)) {
        const cv::Mat gray = cv::imread(entry.path().string(), cv::IMREAD_GRAYSCALE);
        if (gray.empty()) {
            continue;
        }
        vision::SegmentationOptions options;
        options.recoverHighlightsBy = 12;
        const auto segmented = vision::segmentPiece(gray, options);
        if (!segmented.isOk()) {
            continue;
        }
        const auto contour = vision::describeContour(segmented.value());
        if (!contour.valid) {
            continue;
        }
        const vision::ShapeClass shape =
            vision::classifyShape(contour.outer, segmented.value());
        if (shape.kind != vision::ShapeKind::Polygon &&
            shape.kind != vision::ShapeKind::Rounded) {
            continue;
        }
        ++polygons;
        std::smatch found;
        ASSERT_TRUE(std::regex_search(shape.reason, found, says))
            << entry.path().filename().string() << ": el motivo dice «" << shape.reason
            << "» y no dice en cuántos barridos aguanta el recuento. Sin ese número, "
               "nueve lados que salen dos veces de treinta se leen igual que seis que "
               "salen diecisiete";
        const int plateau = std::stoi(found[1].str());
        const int swept = std::stoi(found[2].str());
        std::printf("  [forma] %-30s %d lados, aguanta %d de %d\n",
                    entry.path().filename().string().c_str(), shape.sides, plateau, swept);
        EXPECT_GT(plateau, 0) << entry.path().filename().string()
                              << ": el recuento ganador no puede aguantar cero barridos";
        EXPECT_LE(plateau, swept) << entry.path().filename().string()
                                  << ": aguanta más barridos de los que se hicieron";
        weakest = std::min(weakest, plateau);
        firmest = std::max(firmest, plateau);
    }

    if (polygons == 0) {
        GTEST_SKIP() << "ninguna foto del banco sale polígono";
    }
    std::printf("  [forma] %d polígonos; el más flojo aguanta %d y el más firme %d\n",
                polygons, weakest, firmest);
    // Y QUE EL NÚMERO SIRVA PARA ALGO. Si todos salieran iguales, publicarlo
    // sería adornar el mensaje: la prueba pasaría en verde y el operador seguiría
    // sin poder distinguir una clasificación firme de una casualidad.
    EXPECT_LT(weakest, firmest)
        << "todas las piezas del banco aguantan lo mismo (" << weakest
        << "): el número no está diciendo nada y publicarlo no ayuda a nadie";
}
