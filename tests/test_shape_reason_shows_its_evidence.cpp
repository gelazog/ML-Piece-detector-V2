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
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

#include "inspection_editor/piece_report.h"
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

// Y LA RESERVA VIAJA CON EL NOMBRE, no solo con el motivo.
//
// El motivo lleva los dos números —desviación y meseta— y se pinta debajo del
// titular del informe. Pero el NOMBRE de la figura sale además por sitios donde
// no hay motivo al lado:
//
//   - el mensaje de una receta que no va con la pieza: «esta pieza se ha
//     reconocido como “Polígono de 7 lados”, elige otra receta»;
//   - el titular del informe, que es lo único que viaja al exportar.
//
// En esos dos, un recuento de 2 de 30 se lee exactamente igual que uno de 17 de
// 30. Así que el nombre lleva su propia reserva cuando la meseta no llega al
// listón con el que se decidió, que es el mismo `kPlateauRulesAbove` de siempre
// — escrito una sola vez, en `vision::sideCountIsFirm`, para que la pantalla y
// el clasificador no puedan discrepar.
//
// Lo que esta prueba protege de verdad: que la reserva NO salga cuando no se ha
// medido. El polígono redondeado cuenta sus lados por otro camino, sin barrido y
// sin meseta, y ahí decir «poco firme» sería insinuar una duda que nadie ha
// medido — la otra forma de mentir con un número.
TEST(ShapeReason, TheNameCarriesItsOwnReservation) {
    vision::ShapeClass firm;
    firm.kind = vision::ShapeKind::Polygon;
    firm.sides = 6;
    firm.sideCountPlateau = 17;
    firm.sideCountSweeps = 30;
    EXPECT_TRUE(vision::sideCountIsFirm(firm));
    EXPECT_EQ(inspection::describeShape(firm), "Polígono de 6 lados");

    vision::ShapeClass flimsy = firm;
    flimsy.sides = 9;
    flimsy.sideCountPlateau = 2;  // lo que da `arandelas-2.png`
    EXPECT_FALSE(vision::sideCountIsFirm(flimsy));
    EXPECT_EQ(inspection::describeShape(flimsy), "Polígono de 9 lados (recuento poco firme)");

    // Sin barrido no hay dato, y «no medido» no es «flojo».
    vision::ShapeClass rounded;
    rounded.kind = vision::ShapeKind::Rounded;
    rounded.sides = 4;
    EXPECT_TRUE(vision::sideCountIsFirm(rounded))
        << "un polígono redondeado no barre epsilon: no tiene meseta que juzgar";
    EXPECT_EQ(inspection::describeShape(rounded), "Polígono redondeado de 4 lados");

    // Y una figura que no cuenta lados no se ve afectada.
    vision::ShapeClass ring;
    ring.kind = vision::ShapeKind::Ring;
    EXPECT_EQ(inspection::describeShape(ring), "Arandela");
}

// UN DODECÁGONO EXACTO NO ES UN «RECUENTO POCO FIRME».
//
// La reserva del nombre se decidía con `kPlateauRulesAbove`, la media barrida, y
// esa constante lleva escrito al lado, con todas las letras, que NO vale para
// esta pregunta: contesta «¿está este recuento tan claro que se le perdona un
// borde sucio?», y aplicada aquí deja fuera a los polígonos de muchos lados,
// porque cuantos más lados más estrecha es la ventana de epsilon donde
// sobreviven todos.
//
// Medido sobre polígonos limpios dibujados a propósito:
//
//     lados     3    5    6    8   10   12
//     meseta   30   30   30   22   14    9   (de 30)
//
// Con la media barrida, el decágono y el dodecágono —dibujos EXACTOS, sin un
// píxel de ruido— salían rotulados «(recuento poco firme)». La vara de esta
// pregunta es `kCountIsTrustworthyAbove`, que sale de un hueco medido entre los
// polígonos de muchos lados (6/30) y las piezas redondas (3/30).
//
// Sobre el banco de fotos el cambio casi no se nota —de 106 polígonos, 18
// llevaban descargo y ahora 17— y eso es justo lo que lo hacía difícil de ver
// mirando fotos: el fallo estaba en las piezas que el banco no tiene. Por eso
// esta prueba las dibuja.
TEST(ShapeReason, ACleanPolygonOfManySidesCarriesNoReservation) {
    for (int sides : {3, 5, 6, 8, 10, 12}) {
        cv::Mat image(600, 600, CV_8UC1, cv::Scalar(230));
        std::vector<cv::Point> corners;
        corners.reserve(static_cast<std::size_t>(sides));
        for (int i = 0; i < sides; ++i) {
            const double angle = 2.0 * CV_PI * i / sides;
            corners.emplace_back(static_cast<int>(300 + 240 * std::cos(angle)),
                                 static_cast<int>(300 + 240 * std::sin(angle)));
        }
        std::vector<std::vector<cv::Point>> polygon{corners};
        cv::fillPoly(image, polygon, cv::Scalar(40), cv::LINE_AA);
        cv::Mat mask;
        cv::threshold(image, mask, 128, 255, cv::THRESH_BINARY_INV);
        std::vector<std::vector<cv::Point>> outer;
        cv::findContours(mask, outer, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        ASSERT_FALSE(outer.empty()) << sides;

        const vision::ShapeClass shape = vision::classifyShape(outer.front(), mask);
        ASSERT_EQ(shape.kind, vision::ShapeKind::Polygon)
            << "el dibujo exacto de " << sides << " lados ya no se lee como polígono";
        EXPECT_EQ(shape.sides, sides);
        std::printf("  [limpio] %2d lados: meseta %2d de %2d, se llama «%s»\n", sides,
                    shape.sideCountPlateau, shape.sideCountSweeps,
                    inspection::describeShape(shape).c_str());
        EXPECT_TRUE(vision::sideCountIsFirm(shape))
            << "un polígono de " << sides
            << " lados dibujado exacto sale con el recuento en duda (meseta "
            << shape.sideCountPlateau << " de " << shape.sideCountSweeps << ")";
        EXPECT_EQ(inspection::describeShape(shape).find("poco firme"), std::string::npos)
            << "«" << inspection::describeShape(shape)
            << "» sobre un dibujo exacto: la reserva está midiendo con la vara de otra "
               "pregunta";
    }
}
