// CIEN TUERCAS IGUALES SALÍAN CON SEIS FORMAS DISTINTAS, POR 0,24 PX.
//
// `producto-tuercas-prueba.jpg` son cien tuercas hexagonales del mismo lote. La
// detección es repetible —el área varía un 0,9 % entre la mayor y la menor— y el
// clasificador no lo era: 6, 7, 8, 9, 10 y 11 lados, con once aciertos de cien y
// la respuesta más frecuente (once lados) falsa. Como «qué medir depende de la
// forma», cada tuerca de la bandeja recibía un juego de cotas distinto.
//
// La causa, medida: el barrido de epsilon responde a DOS preguntas y el código
// solo usaba una.
//
//   - La tolerancia responde «¿este polígono explica el contorno?».
//   - La anchura de la meseta responde «¿son estos los lados que tiene la pieza?».
//
// En la tuerca, el ajuste de 6 lados aguanta 17 de los 30 epsilon barridos —la
// siguiente explicación aguanta 3— y se descartaba por 0,24 px: 6,24 contra un
// suelo de 6,00. Ese suelo supone que el borde viene dentado como lo deja el
// rasterizado, ~1 px; en una foto real el borde EN SOMBRA viene dentado 2-3 px, y
// sobre una pieza de 90 px eso basta para pasarse. Se ve dibujando el contorno:
// el borde iluminado sale limpio y el de abajo, serrado.
//
// Cuando el recuento está fuera de duda, un borde sucio no puede convertir un
// hexágono en «una cosa de once lados». Así que una meseta que ocupa media
// barrida manda sobre la tolerancia, con una holgura acotada.
//
// Esta prueba existe porque el arreglo tiene DOS condiciones y cada una sostiene
// una mitad del resultado. Con una sola, el remedio es peor que la enfermedad:
// relajando la tolerancia a secas, cinco piezas de `tornillo-ojo-5` pasaban de
// «irregular» a «polígono de 12 lados» —el tope de lados, o sea un ajuste que no
// explica nada pero cabe— y eso propone doce cotas de lado y doce ángulos que
// son ruido. Por eso aquí se comprueban las dos.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "vision/contour_analysis.h"
#include "vision/pipeline.h"
#include "vision/shape_class.h"

using namespace pci;

namespace {

struct Census {
    int pieces = 0;
    std::map<int, int> sides;
};

Census classifyEveryPieceIn(const std::string& photo) {
    Census out;
    const cv::Mat image =
        cv::imread("C:/Users/furro/Pictures/IMG-MC/" + photo, cv::IMREAD_COLOR);
    if (image.empty()) {
        return out;
    }
    vision::PipelineConfig config;
    config.segmentation.recoverHighlightsBy = 12;
    auto all = vision::analyzeFrames(image, config);
    if (!all.isOk()) {
        return out;
    }
    for (const auto& piece : all.value()) {
        const cv::Mat mask =
            vision::pieceMaskWithHoles(image, piece.mask, config.segmentation);
        const auto shape = vision::classifyShape(piece.contour.points, mask);
        ++out.pieces;
        ++out.sides[shape.sides];
    }
    return out;
}

cv::Mat regularPolygon(int n, int radius) {
    cv::Mat canvas(radius * 2 + 80, radius * 2 + 80, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point> points;
    for (int i = 0; i < n; ++i) {
        const double angle = 2.0 * CV_PI * i / n;
        points.emplace_back(cvRound(radius + 40 + radius * std::cos(angle)),
                            cvRound(radius + 40 + radius * std::sin(angle)));
    }
    cv::fillPoly(canvas, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255),
                 cv::LINE_AA);
    return canvas;
}

std::vector<cv::Point> outerOf(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    return contours.empty() ? std::vector<cv::Point>{} : contours.front();
}

}  // namespace

TEST(PlateauRules, AHundredIdenticalNutsAreAHundredHexagons) {
    const Census census = classifyEveryPieceIn("producto-tuercas-prueba.jpg");
    if (census.pieces == 0) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    std::printf("  [meseta] %d tuercas:", census.pieces);
    for (const auto& [sides, count] : census.sides) {
        std::printf("  %d lados->%d", sides, count);
    }
    std::printf("\n");

    ASSERT_GT(census.pieces, 90) << "no se detectan las cien tuercas: esta prueba no está "
                                    "midiendo lo que cree";
    const int hexagons = census.sides.count(6) > 0 ? census.sides.at(6) : 0;
    // Un trinquete sobre lo MEDIDO hoy (85), no sobre lo deseable (100). Puede
    // subir; si baja, es que alguien deshizo esto sin enterarse.
    EXPECT_GE(hexagons, 80)
        << "la misma pieza vuelve a dar formas distintas, y con la forma cambia el juego "
           "de cotas que se le propone: la bandeja entera se vuelve irrevisable";
}

TEST(PlateauRules, AWidePlateauDoesNotExcuseAFitThatExplainsNothing) {
    // LA OTRA MITAD, y sin ella el arreglo sería peor que el fallo.
    //
    // Relajar la tolerancia a secas admite ajustes que aguantan muchos epsilon
    // pero se separan una barbaridad. Medidos: el rectángulo con redondeos de 40
    // px da un ajuste de 4 lados en 21 de 30 barridos separándose 17,4 px (2,9
    // veces el tope), y el polígono de 16 da uno de 8 lados en 16 de 30
    // separándose 12,5 px (2,1 veces). La tuerca que hay que admitir se separa
    // 1,04 veces el tope. Entre 1,04 y 2,08 hay sitio de sobra.
    const cv::Mat sixteen = regularPolygon(16, 160);
    const auto shape = vision::classifyShape(outerOf(sixteen), sixteen);
    std::printf("  [meseta] polígono de 16 lados -> %s\n", shape.reason.c_str());
    EXPECT_EQ(shape.kind, vision::ShapeKind::Circle)
        << "un polígono de 16 lados se mide como redondo, y ahora sale como otra cosa: la "
           "holgura de la meseta está dejando pasar un ajuste de 8 lados que se separa "
           "el doble del tope";

    // Y los que ya salían bien siguen saliendo bien: si la holgura hubiera
    // movido estos, no sería una holgura acotada.
    const std::vector<std::pair<int, int>> straightEdged{{4, 200}, {6, 120}, {12, 100}};
    for (const auto& [sides, radius] : straightEdged) {
        const cv::Mat mask = regularPolygon(sides, radius);
        const auto fit = vision::classifyShape(outerOf(mask), mask);
        EXPECT_EQ(fit.kind, vision::ShapeKind::Polygon)
            << "el polígono de " << sides << " lados deja de ser un polígono";
        EXPECT_EQ(fit.sides, sides)
            << "el polígono de " << sides << " lados sale con " << fit.sides;
    }
}

TEST(PlateauRules, ANarrowPlateauIsNotEvidenceNoMatterHowCloseItFits) {
    // Y LA MITAD QUE PROTEGE AL BANCO. Un cáncamo no es un polígono. Su mejor
    // ajuste por debajo de 12 lados aguanta 3 de 30 barridos separándose 7,3 px,
    // y un tornillo, 1 de 30 separándose 9,6. Subir la tolerancia a 10 px —que es
    // lo que hacía falta para las tuercas por ese camino— los convertía a los dos
    // en «polígono de 12 lados», que es el tope: doce cotas de lado y doce ángulos
    // sacados de un ajuste que no explica la pieza.
    for (const auto* photo : {"tornillo-ojo-5.png", "tornillos-1.png"}) {
        const Census census = classifyEveryPieceIn(photo);
        if (census.pieces == 0) {
            GTEST_SKIP() << "sin banco de fotos";
        }
        const int cap = vision::ClassifyOptions{}.maxSides;
        const int atTheCap = census.sides.count(cap) > 0 ? census.sides.at(cap) : 0;
        std::printf("  [meseta] %s: %d piezas, %d con el tope de lados\n", photo,
                    census.pieces, atTheCap);
        EXPECT_EQ(atTheCap, 0)
            << photo
            << ": alguna pieza sale con el tope de lados justo. Eso no es un recuento, es "
               "un ajuste que no explica nada y cabe de milagro — y propone doce cotas de "
               "lado y doce ángulos que son ruido";
    }
}
