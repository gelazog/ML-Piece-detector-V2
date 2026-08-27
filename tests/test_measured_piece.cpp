// QUÉ PIEZA SE MIDE CUANDO HAY VARIAS.
//
// Queja del taller: «si hay más de una pieza, y se usa la automedición de
// pieza, esta toma una medición para todas las piezas, en lugar de una medición
// independiente por pieza».
//
// El navegador de piezas existía y funcionaba: las flechas cambian la pieza
// enfocada, el vídeo la remarca y el rótulo dice «Midiendo la pieza 3 de 5».
// Pero **solo lo entendía el camino del vídeo**. «Medir pieza», la medición
// automática del editor y la apertura del propio editor llamaban a
// `vision::analyzeFrame`, que devuelve LA MAYOR y no sabe nada de navegadores.
//
// Así que el operador señalaba una pieza, la veía medida en pantalla, pulsaba
// «Medir pieza» — y recibía el informe de otra. Sin ningún aviso.
//
// Lo irónico es que `largestPieceIndex` se escribió precisamente para que esto
// no pasara, y su comentario lo dice con todas las letras: «qué pieza se mide
// es exactamente la decisión que no se puede permitir divergir en silencio: en
// vivo se vería una y el informe traería la de la otra». La decisión estaba
// centralizada un escalón por debajo del que hacía falta.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>

#include "inspection_editor/piece_report.h"
#include "vision/contour_analysis.h"
#include "vision/pipeline.h"

using namespace pci;

namespace {

cv::Mat photo(const std::string& name) {
    return cv::imread("C:/Users/furro/Pictures/IMG-MC/" + name, cv::IMREAD_COLOR);
}

// Piezas sintéticas de tamaños muy distintos, para poder hablar de la elección
// sin depender de ninguna foto.
std::vector<vision::PieceAnalysis> threePieces() {
    cv::Mat frame(300, 600, CV_8UC1, cv::Scalar(240));
    cv::circle(frame, {80, 150}, 30, cv::Scalar(30), cv::FILLED);   // 1: pequeña
    cv::circle(frame, {300, 150}, 60, cv::Scalar(30), cv::FILLED);  // 2: LA MAYOR
    cv::circle(frame, {500, 150}, 45, cv::Scalar(30), cv::FILLED);  // 3: mediana
    auto all = vision::analyzeFrames(frame, {});
    return all.isOk() ? all.value() : std::vector<vision::PieceAnalysis>{};
}

}  // namespace

TEST(MeasuredPiece, TheNavigatorNumberWinsAndZeroStillMeansTheBiggest) {
    const auto pieces = threePieces();
    ASSERT_EQ(pieces.size(), 3U) << "la escena de prueba no da tres piezas separables";
    const std::size_t biggest = vision::largestPieceIndex(pieces);
    std::printf("  [pieza] la mayor es la #%d en orden de lectura\n",
                static_cast<int>(biggest) + 1);

    // El cero es un estado con nombre: «la que decidas tú». Quien no toque el
    // navegador tiene que seguir midiendo exactamente lo que medía antes, o
    // este arreglo sería un cambio de comportamiento a espaldas de todos.
    EXPECT_EQ(vision::measuredPieceIndex(pieces, 0), biggest);

    // Y con número, ese. En orden de LECTURA y empezando por 1, que es lo que
    // el rótulo del navegador enseña: si aquí se contara desde cero, «pieza 3»
    // en pantalla mediría la 4.
    EXPECT_EQ(vision::measuredPieceIndex(pieces, 1), 0U);
    EXPECT_EQ(vision::measuredPieceIndex(pieces, 2), 1U);
    EXPECT_EQ(vision::measuredPieceIndex(pieces, 3), 2U);
}

TEST(MeasuredPiece, AskingForAPieceThatIsNoLongerThereFallsBackInsteadOfGivingUp) {
    const auto pieces = threePieces();
    ASSERT_EQ(pieces.size(), 3U);
    const std::size_t biggest = vision::largestPieceIndex(pieces);

    // Las piezas se mueven, entran y salen del encuadre. Un encuadre que deja
    // de dar cotas porque falta la pieza 5 es peor que uno que mide la que hay.
    EXPECT_EQ(vision::measuredPieceIndex(pieces, 9), biggest);
    EXPECT_EQ(vision::measuredPieceIndex(pieces, -3), biggest);
    // Y con la lista vacía no se puede indexar nada: cero, y que el llamante
    // compruebe que hay piezas.
    EXPECT_EQ(vision::measuredPieceIndex({}, 2), 0U);
}

TEST(MeasuredPiece, MeasuringPieceOneInsteadOfTheBiggestIsADifferentReportEntirely) {
    // ESTO ES LO QUE COSTABA EL FALLO, sobre una foto real.
    //
    // `arandelas-5`: diez piezas surtidas, y la mayor NO es la primera en orden
    // de lectura — es la octava. O sea que el operador que señalaba la 1 y
    // pedía el informe recibía el de la 8 sin que nada se lo dijera.
    const cv::Mat image = photo("arandelas-5.png");
    if (image.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    vision::PipelineConfig config;
    config.segmentation.recoverHighlightsBy = 12;
    auto all = vision::analyzeFrames(image, config);
    ASSERT_TRUE(all.isOk());
    ASSERT_GE(all.value().size(), 8U) << "esta foto ya no da las diez piezas de siempre";

    const std::size_t biggest = vision::largestPieceIndex(all.value());
    EXPECT_NE(biggest, 0U) << "si la mayor volviera a ser la primera, esta foto dejaría de "
                              "poder enseñar la diferencia y habría que buscar otra";

    const auto reportOf = [&](std::size_t index) {
        const auto& piece = all.value()[index];
        const cv::Mat mask =
            vision::pieceMaskWithHoles(image, piece.mask, config.segmentation);
        return inspection::measureWholePiece(image, mask, piece.fixture, 0.0,
                                             inspection::LengthUnit::Pixels, image.size());
    };
    const auto first = reportOf(vision::measuredPieceIndex(all.value(), 1));
    const auto big = reportOf(vision::measuredPieceIndex(all.value(), 0));
    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(big.ok);
    std::printf("  [pieza] señalando la 1: %s\n", first.headline.c_str());
    std::printf("  [pieza] la mayor (#%d): %s\n", static_cast<int>(biggest) + 1,
                big.headline.c_str());

    // No es un matiz: son piezas de otra forma y de otro tamaño. Un informe por
    // el otro no se detecta leyéndolo, se detecta cuando la pieza buena se
    // rechaza meses después.
    EXPECT_NE(first.headline, big.headline)
        << "las dos piezas dan el mismo titular, así que esta prueba no puede distinguir "
           "si se midió la señalada o la mayor";
    EXPECT_LT(all.value()[0].contour.area, all.value()[biggest].contour.area * 0.5)
        << "la primera pieza ya no es mucho menor que la mayor: hace falta otra foto para "
           "que la diferencia siga siendo evidente";
}
