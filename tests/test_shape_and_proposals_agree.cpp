// LA APLICACIÓN DECÍA «HEXÁGONO DE 6 LADOS» Y OFRECÍA «2 LADOS Y 3 REDONDEOS».
//
// Dos partes leían el mismo contorno con dos algoritmos distintos:
//
//   - El clasificador cuenta lados con `approxPolyDP` y una meseta de epsilon.
//   - La medición automática partía el contorno en rectas y arcos
//     (`decomposeContour`) y proponía una cota por cada tramo.
//
// El comentario del proponedor afirmaba que los dos miraban lo mismo «porque
// comparten `decomposeOptionsFor`». Compartir el paso del remuestreo no es
// mirar lo mismo cuando uno cuenta vértices y el otro parte en primitivas, y
// esa frase es la razón de que nadie lo comprobara.
//
// Medido sobre el banco entero: de **106** piezas que el clasificador llama
// polígono, en **UNA** coincidía el número de lados propuestos. A una tuerca
// hexagonal le decía «6 lados» y le ofrecía dos lados y tres «Radio» —de 28, 22
// y 20 px— que eran sus propias caras planas leídas como arco.
//
// Lo caro no es la cota que falta: es la que sobra. Un «Radio 3 = 28 px» sobre
// una pieza que no tiene ningún redondeo es una cota que el operador acepta,
// guarda en la plantilla y luego no cuadra con el plano, sin que nada le diga
// de dónde salió.
//
// Ahora, cuando el clasificador ha decidido POLÍGONO, los lados y los ángulos
// salen de sus vértices —los mismos con los que se decidió que lo es— y los
// redondeos no se proponen, porque un polígono no tiene ninguno: si los tuviera,
// la clase sería «polígono redondeado».
//
// De 1 a 85 de 106.
//
// Y los lados que salen de ahí son MÁS EXACTOS, no solo más: `approxPolyDP`
// elige como vértice un punto del contorno, y ese punto casi nunca es la
// esquina. Sobre un hexágono sintético de lado 100 px, los vértices tal cual dan
// 103,6 —un 3,6 % de más—, así que cada cara se ajusta por mínimos cuadrados
// totales y la esquina sale de cortar las dos rectas vecinas. El error baja a
// 0,70 %, y la dispersión del mismo hexágono entre cuatro resoluciones, de
// 2,80 % a 0,51 %.
//
// Los 21 que no coinciden no son un fallo: tienen algún lado más corto que
// `minFeatureLength`, y ahí saltárselo es lo correcto —una cota sobre un tramo
// de 20 px no se puede medir con repetibilidad—. Por eso el trinquete va sobre
// el número medido y no sobre el 100 %.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "inspection_editor/auto_measure.h"
#include "vision/contour_analysis.h"
#include "vision/pipeline.h"
#include "vision/shape_class.h"

using namespace pci;

namespace {

const std::vector<std::string>& bank() {
    static const std::vector<std::string> photos = {
        "producto-tuercas-prueba.jpg", "Producto_Tuerca_Liv_02.jpg", "arandelas-1.png",
        "arandelas-2.png", "arandelas-3.jpg", "arandelas-4.png", "arandelas-5.png",
        "engranaje-1.png", "engranajes-1.jpg", "rosca-1.png", "tornillo-1.png",
        "tornillo-2.png", "tornillo-ojo-3.png", "tornillo-ojo-4.png",
        "tornillo-ojo-5.png", "tornillos-1.png"};
    return photos;
}

struct Tally {
    int polygons = 0;
    int sidesAgree = 0;
    int withFakeRadii = 0;
    int shortSided = 0;
};

// Se mide con el tope SUBIDO a propósito. Con el tope real de doce, una tuerca
// genera diecisiete cotas que valen y el recorte se queda con doce: entonces
// «propone 2 lados» sería el recorte haciendo su trabajo, no un desacuerdo. Lo
// que esta prueba comprueba es qué GENERA el proponedor, que es donde estaba el
// fallo.
Tally sweepTheBank() {
    Tally tally;
    for (const auto& photo : bank()) {
        const cv::Mat image =
            cv::imread("C:/Users/furro/Pictures/IMG-MC/" + photo, cv::IMREAD_COLOR);
        if (image.empty()) {
            continue;
        }
        vision::PipelineConfig config;
        config.segmentation.recoverHighlightsBy = 12;
        auto all = vision::analyzeFrames(image, config);
        if (!all.isOk()) {
            continue;
        }
        for (const auto& piece : all.value()) {
            const cv::Mat mask =
                vision::pieceMaskWithHoles(image, piece.mask, config.segmentation);
            const auto shape = vision::classifyShape(piece.contour.points, mask);
            if (shape.kind != vision::ShapeKind::Polygon || shape.sides < 3) {
                continue;
            }
            ++tally.polygons;

            inspection::ProposeOptions wide;
            wide.maxProposals = 100;
            int sides = 0;
            int radii = 0;
            for (const auto& proposal :
                 inspection::proposeTools(image, mask, piece.fixture, wide, 0.0, nullptr)) {
                if (proposal.config.name.rfind("Lado ", 0) == 0) {
                    ++sides;
                } else if (proposal.config.name.rfind("Radio ", 0) == 0) {
                    ++radii;
                }
            }
            if (sides == shape.sides) {
                ++tally.sidesAgree;
            } else {
                ++tally.shortSided;
            }
            if (radii > 0) {
                ++tally.withFakeRadii;
            }
        }
    }
    return tally;
}

}  // namespace

TEST(ShapeAndProposalsAgree, ThePiecesSidesAreTheOnesItWasRecognisedBy) {
    const Tally tally = sweepTheBank();
    if (tally.polygons == 0) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    std::printf("  [acuerdo] %d polígonos: %d con los lados que dicen, %d con algún lado "
                "por debajo del mínimo\n",
                tally.polygons, tally.sidesAgree, tally.shortSided);

    // Que el barrido esté mirando de verdad.
    ASSERT_GT(tally.polygons, 80)
        << "el banco apenas da polígonos: esta prueba no comprueba lo que cree";

    // Trinquete sobre lo MEDIDO hoy (85 de 106), no sobre el 100 %: los que
    // faltan tienen algún lado más corto que `minFeatureLength` y saltárselo es
    // lo correcto. Puede subir; si baja, alguien ha vuelto a separar las dos
    // lecturas del contorno.
    EXPECT_GE(tally.sidesAgree, 80)
        << "la aplicación vuelve a decir un número de lados y a proponer otro. El "
           "operador lee «hexágono de 6 lados» y recibe dos cotas de lado, sin que nada "
           "le explique dónde están las otras cuatro";
}

TEST(ShapeAndProposalsAgree, APolygonIsNeverOfferedARoundingItDoesNotHave) {
    // La cota que SOBRA, que es la cara cara del fallo. A una tuerca hexagonal
    // se le ofrecían tres «Radio» —28, 22 y 20 px— que eran sus caras planas
    // leídas como arco por la descomposición. Un redondeo que no existe se
    // acepta, se guarda en la plantilla y luego no cuadra con el plano.
    //
    // Por definición no puede haberlos: si la pieza tuviera las esquinas
    // redondeadas, el clasificador habría dicho «polígono redondeado», que es
    // otra clase y sí recibe sus radios.
    const Tally tally = sweepTheBank();
    if (tally.polygons == 0) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    std::printf("  [acuerdo] %d polígonos, %d con algún redondeo propuesto\n",
                tally.polygons, tally.withFakeRadii);
    EXPECT_EQ(tally.withFakeRadii, 0)
        << "a una pieza de esquinas vivas se le propone el radio de un redondeo que no "
           "tiene: es una cota inventada con aspecto de medida";
}

TEST(ShapeAndProposalsAgree, TheVerticesTravelWithTheAnswer) {
    // Y la razón por la que esto se pudo arreglar: `ShapeClass` no exponía los
    // vértices con los que decidía, así que quien quisiera una cota por lado no
    // tenía de dónde sacarlos y acababa usando la otra lectura del contorno.
    //
    // Si alguien vuelve a quitarlos, el proponedor caería otra vez en las
    // primitivas y en silencio, porque tiene una rama para las piezas que no son
    // polígonos. Esto lo hace ruidoso.
    cv::Mat canvas(400, 400, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point> hexagon;
    for (int i = 0; i < 6; ++i) {
        const double angle = 2.0 * CV_PI * i / 6.0;
        hexagon.emplace_back(cvRound(200 + 140 * std::cos(angle)),
                             cvRound(200 + 140 * std::sin(angle)));
    }
    cv::fillPoly(canvas, std::vector<std::vector<cv::Point>>{hexagon}, cv::Scalar(255),
                 cv::LINE_AA);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(canvas, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    ASSERT_FALSE(contours.empty());

    const auto shape = vision::classifyShape(contours.front(), canvas);
    ASSERT_EQ(shape.kind, vision::ShapeKind::Polygon);
    ASSERT_EQ(shape.sides, 6);
    EXPECT_EQ(static_cast<int>(shape.vertices.size()), shape.sides)
        << "la clase dice cuántos lados tiene y no dice cuáles son: quien quiera medir "
           "uno tendrá que buscarlos por su cuenta, con otro algoritmo, y volverá a salir "
           "un número distinto";
}
