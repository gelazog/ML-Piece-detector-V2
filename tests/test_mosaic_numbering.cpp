// EL NÚMERO DEL MOSAICO Y EL DEL MOTOR TIENEN QUE SER EL MISMO.
//
// El mosaico sirve para una cosa: ver las piezas de un vistazo y **pulsar la
// que desentona** para que la midan las herramientas. Todo eso se apoya en un
// acuerdo tácito entre dos ficheros que no se conocen:
//
//   · `PieceMosaic` numera la baldosa `i` como pieza `i + 1` y eso es lo que
//     emite al pulsarla.
//   · El motor recibe ese número como `wantedPiece` y mide `piezas[n - 1]`.
//
// Si uno de los dos cambia de convención, no salta nada: el panel sigue
// pintando baldosas, el motor sigue midiendo, y el operador elige la pieza 3 y
// se le mide la 2. Un fallo silencioso que solo se nota comparando dos informes
// de la misma bandeja — o no se nota nunca.
//
// Esta prueba pone los dos lados frente al mismo encuadre y comprueba que
// coinciden. La escena está hecha a mala idea: **la tercera pieza en orden de
// lectura no es la mayor**, así que si el enfoque se ignorara y se midiera «la
// de siempre», la comprobación lo ve.

#include <gtest/gtest.h>

#include <QPolygonF>
#include <QSignalSpy>
#include <QToolButton>

#include <opencv2/imgproc.hpp>

#include <vector>

#include "camera/frame_utils.h"
#include "ui/piece_mosaic.h"
#include "vision/contour_analysis.h"
#include "vision/pipeline.h"

namespace {

// Tres cuadrados en fila. El tercero es el MÁS PEQUEÑO y el primero el mayor:
// así, «la que toca por defecto» (la mayor) y «la tercera» son piezas
// distintas, que es la única forma de que la prueba distinga si el enfoque
// llega o se ignora.
cv::Mat threeSquaresLastIsSmallest() {
    cv::Mat scene(300, 900, CV_8UC1, cv::Scalar(20));
    const int sides[3] = {140, 110, 80};
    int x = 60;
    for (int side : sides) {
        cv::rectangle(scene, cv::Rect(x, 150 - side / 2, side, side), cv::Scalar(230),
                      cv::FILLED);
        x += 300;
    }
    return scene;
}

// Los contornos tal como se los pasa la ventana al mosaico: en el mismo orden
// en que los devuelve el análisis, sin reordenar nada por el camino.
std::vector<QPolygonF> outlinesOf(const std::vector<pci::vision::PieceAnalysis>& pieces) {
    std::vector<QPolygonF> outlines;
    outlines.reserve(pieces.size());
    for (const auto& piece : pieces) {
        QPolygonF outline;
        for (const auto& point : piece.contour.points) {
            outline << QPointF(point.x, point.y);
        }
        outlines.push_back(std::move(outline));
    }
    return outlines;
}

}  // namespace

TEST(MosaicNumbering, TheTileNumberIndexesThePieceTheEngineWouldMeasure) {
    const cv::Mat scene = threeSquaresLastIsSmallest();
    pci::vision::PipelineConfig config;
    auto all = pci::vision::analyzeFrames(scene, config);
    ASSERT_TRUE(all.isOk()) << all.error().message;
    ASSERT_EQ(all.value().size(), 3U) << "la escena de prueba no da tres piezas";

    // Premisa de la prueba: la tercera NO es la mayor. Si esto dejara de
    // cumplirse, la comprobación de abajo pasaría sin comprobar nada.
    const std::size_t biggest = pci::vision::largestPieceIndex(all.value());
    ASSERT_NE(biggest, 2U) << "la escena ya no distingue «la tercera» de «la mayor»";

    const auto outlines = outlinesOf(all.value());
    pci::ui::PieceMosaic mosaic;
    mosaic.resize(500, 300);
    mosaic.setPieces(pci::camera::matToQImage(scene), outlines,
                     static_cast<int>(biggest) + 1);

    const auto tiles = mosaic.findChildren<QToolButton*>();
    ASSERT_EQ(tiles.size(), 3);

    QSignalSpy chosen(&mosaic, &pci::ui::PieceMosaic::pieceChosen);
    tiles[2]->click();
    ASSERT_EQ(chosen.count(), 1);
    const int number = chosen.at(0).at(0).toInt();

    // ESTE es el acuerdo. El motor hace exactamente `piezas[wantedPiece - 1]`
    // (ver `buildOverlay`), así que el número que sale del mosaico tiene que
    // indexar la misma pieza que enseña la baldosa pulsada.
    ASSERT_GE(number, 1);
    ASSERT_LE(number, 3);
    const auto& engineWouldMeasure = all.value()[static_cast<std::size_t>(number - 1)];
    const auto& thirdInReadingOrder = all.value()[2];
    EXPECT_DOUBLE_EQ(engineWouldMeasure.contour.area, thirdInReadingOrder.contour.area);
    EXPECT_EQ(engineWouldMeasure.contour.centroid.x, thirdInReadingOrder.contour.centroid.x);

    // Y no es la que se mediría sin elegir: el enfoque cambia algo de verdad.
    EXPECT_NE(engineWouldMeasure.contour.centroid.x, all.value()[biggest].contour.centroid.x)
        << "elegir la tercera acaba midiendo la misma que sin elegir nada";
}
