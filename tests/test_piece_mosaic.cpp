// CIEN PIEZAS NO SE REVISAN DE UNA EN UNA.
//
// Con una bandeja llena, el vídeo las enseña a todas pero al tamaño que tengan:
// cada tuerca ocupa ochenta píxeles en pantalla y no hay forma de mirar ninguna.
// Y pasar de una en una con las flechas del selector para revisar cien es un
// trabajo que, sencillamente, nadie hace.
//
// El mosaico recorta cada pieza y las pone en cuadrícula, todas al mismo tamaño
// y con su número. Lo que se comprueba aquí es lo que hace que sirva de algo:
//
//  1. Que cada baldosa enseña SU pieza y no el encuadre entero. Un mosaico que
//     repite la misma imagen N veces queda bien en una captura de pantalla y es
//     inútil delante de una bandeja: es el fallo que hay que poder detectar.
//  2. Que los números son los mismos que usan el selector y el informe, porque
//     si no, elegir «la 3» aquí y leer «la 3» allí serían dos piezas distintas.
//  3. Que con una sola pieza no enseña nada. El vídeo ya la da entera y más
//     grande; un panel que ocupa sitio para no decir nada enseña a cerrarlo.

#include <gtest/gtest.h>

#include <QApplication>
#include <QPolygonF>
#include <QSignalSpy>
#include <QToolButton>

#include <vector>

#include "ui/piece_mosaic.h"

namespace {

// Tres piezas de colores distintos, bien separadas, sobre fondo negro.
//
// Los colores son el truco de la prueba: si un recorte sale del sitio
// equivocado, el color de la baldosa lo delata sin ambigüedad. Con piezas
// grises habría que comparar formas y la prueba fallaría por cualquier
// diferencia de escalado.
struct Scene {
    QImage frame;
    std::vector<QPolygonF> outlines;
    std::vector<QColor> colours;
};

Scene threeColouredPieces() {
    Scene scene;
    scene.frame = QImage(600, 200, QImage::Format_RGB888);
    scene.frame.fill(Qt::black);
    scene.colours = {QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255)};

    for (int i = 0; i < 3; ++i) {
        const QRect box(40 + i * 200, 60, 80, 80);
        for (int y = box.top(); y <= box.bottom(); ++y) {
            for (int x = box.left(); x <= box.right(); ++x) {
                scene.frame.setPixelColor(x, y, scene.colours[static_cast<std::size_t>(i)]);
            }
        }
        scene.outlines.push_back(QPolygonF(QRectF(box)));
    }
    return scene;
}

// El color que domina una baldosa. Se mira el centro y no la media porque el
// recorte lleva margen: promediar mezclaría la pieza con el fondo negro.
QColor centreOf(const QToolButton* tile) {
    const QIcon icon = tile->icon();
    const QImage art = icon.pixmap(tile->iconSize()).toImage();
    if (art.isNull()) {
        return {};
    }
    return art.pixelColor(art.width() / 2, art.height() / 2);
}

std::vector<QToolButton*> tilesOf(const pci::ui::PieceMosaic& mosaic) {
    const auto found = mosaic.findChildren<QToolButton*>();
    return {found.begin(), found.end()};
}

}  // namespace

TEST(PieceMosaic, EachTileShowsItsOwnPiece) {
    const Scene scene = threeColouredPieces();
    pci::ui::PieceMosaic mosaic;
    mosaic.resize(400, 300);
    mosaic.setPieces(scene.frame, scene.outlines, 1);

    ASSERT_EQ(mosaic.tileCount(), 3);
    const auto tiles = tilesOf(mosaic);
    ASSERT_EQ(tiles.size(), 3U);

    // Cada baldosa lleva su color, en el orden en que llegaron los contornos
    // —que es el orden de lectura que fija el análisis—. Si el recorte tomara
    // el encuadre entero, las tres saldrían negras y esto lo cazaría.
    for (std::size_t i = 0; i < 3; ++i) {
        const QColor got = centreOf(tiles[i]);
        const QColor want = scene.colours[i];
        EXPECT_NEAR(got.red(), want.red(), 40) << "baldosa " << i;
        EXPECT_NEAR(got.green(), want.green(), 40) << "baldosa " << i;
        EXPECT_NEAR(got.blue(), want.blue(), 40) << "baldosa " << i;
    }
}

TEST(PieceMosaic, ClickingATileChoosesThatPieceByNumber) {
    const Scene scene = threeColouredPieces();
    pci::ui::PieceMosaic mosaic;
    mosaic.resize(400, 300);
    mosaic.setPieces(scene.frame, scene.outlines, 1);

    QSignalSpy chosen(&mosaic, &pci::ui::PieceMosaic::pieceChosen);
    const auto tiles = tilesOf(mosaic);
    ASSERT_EQ(tiles.size(), 3U);

    tiles[2]->click();
    ASSERT_EQ(chosen.count(), 1);
    // La TERCERA baldosa es la pieza 3, no la 2. Los números empiezan en 1
    // porque son los que ve el operador en el selector y en el informe; un
    // desfase de uno aquí haría que eligiera una y se midiera otra.
    EXPECT_EQ(chosen.at(0).at(0).toInt(), 3);
}

TEST(PieceMosaic, WithASinglePieceItShowsNothing) {
    Scene scene = threeColouredPieces();
    scene.outlines.resize(1);

    pci::ui::PieceMosaic mosaic;
    mosaic.resize(400, 300);
    mosaic.setPieces(scene.frame, scene.outlines, 1);

    // Ni una baldosa: con una sola pieza el vídeo ya la enseña entera y más
    // grande, y un panel que ocupa sitio para repetir eso enseña a cerrarlo y a
    // no volver a abrirlo — con lo que tampoco estará el día que haya cien.
    EXPECT_EQ(mosaic.tileCount(), 0);
    EXPECT_TRUE(tilesOf(mosaic).empty());
}

TEST(PieceMosaic, RebuildingReplacesTheTilesInsteadOfPilingThemUp) {
    const Scene scene = threeColouredPieces();
    pci::ui::PieceMosaic mosaic;
    mosaic.resize(400, 300);

    // El panel se repinta con cada análisis, o sea varias veces por segundo. Si
    // cada pasada dejara las baldosas anteriores, la cuadrícula crecería sin
    // parar hasta comerse la memoria — y el operador vería la misma pieza
    // repetida decenas de veces.
    for (int pass = 0; pass < 5; ++pass) {
        mosaic.setPieces(scene.frame, scene.outlines, 1 + pass % 3);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        EXPECT_EQ(mosaic.tileCount(), 3) << "pasada " << pass;
        // Y se cuentan también los widgets de verdad, no sólo el vector: si el
        // vector se vaciara pero las baldosas viejas siguieran colgando del
        // panel, `tileCount()` diría 3 mientras la pantalla enseña 15.
        EXPECT_EQ(tilesOf(mosaic).size(), 3U) << "pasada " << pass;
    }
}
