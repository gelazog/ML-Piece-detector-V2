// EL MOSAICO SE COMÍA LOS CLICS PORQUE SE REHACÍA ENTERO CADA FOTOGRAMA.
//
// Queja del taller: «en piezas de encuadre, hay bastantes veces en donde cuando
// la presiono no se cambia de imagen».
//
// El «bastantes veces» era la pista. `setPieces` se llama en CADA fotograma
// analizado, y reconstruía el panel entero: borraba todas las baldosas y creaba
// otras. Un clic necesita que apretar y soltar caigan en el MISMO widget; si
// entre las dos cosas llega un fotograma, el botón que se apretó ya no existe y
// el clic no llega a ninguna parte.
//
// Por eso fallaba a veces sí y a veces no: dependía de si el fotograma caía en
// medio del clic. No era el ratón del operador.
//
// Y de paso, con la bandeja de cien tuercas eso era crear y destruir cien
// `QToolButton` por fotograma para enseñar exactamente lo mismo.
//
// Esta prueba no comprueba «que el clic funcione» —eso ya lo hacía otra y
// pasaba, porque en una prueba no llega ningún fotograma en medio—. Comprueba
// la causa: que las baldosas SOBREVIVAN a un fotograma nuevo.

#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QPolygonF>
#include <QSignalSpy>
#include <QToolButton>

#include <cstdio>
#include <vector>

#include "ui/piece_mosaic.h"

using namespace pci;

namespace {

std::vector<QPolygonF> threeOutlines(double drift = 0.0) {
    std::vector<QPolygonF> outlines;
    for (int i = 0; i < 3; ++i) {
        QPolygonF outline;
        const double x = 20 + i * 120 + drift;
        outline << QPointF(x, 20 + drift) << QPointF(x + 80, 20 + drift)
                << QPointF(x + 80, 100 + drift) << QPointF(x, 100 + drift);
        outlines.push_back(outline);
    }
    return outlines;
}

QImage aScene() {
    QImage frame(420, 140, QImage::Format_RGB888);
    frame.fill(QColor(230, 230, 230));
    return frame;
}

}  // namespace

TEST(MosaicClickSurvives, TheTilesAreNotDestroyedByEveryNewFrame) {
    ui::PieceMosaic mosaic;
    mosaic.resize(500, 200);
    mosaic.setPieces(aScene(), threeOutlines(), 1);
    ASSERT_EQ(mosaic.tileCount(), 3);

    const auto before = mosaic.findChildren<QToolButton*>();
    ASSERT_EQ(before.size(), 3);

    // Un fotograma nuevo de la MISMA escena: las piezas están donde estaban.
    mosaic.setPieces(aScene(), threeOutlines(), 1);
    const auto after = mosaic.findChildren<QToolButton*>();

    ASSERT_EQ(after.size(), 3);
    int survived = 0;
    for (auto* tile : before) {
        if (after.contains(tile)) {
            ++survived;
        }
    }
    std::printf("  [mosaico] tras un fotograma nuevo sobreviven %d de %d baldosas\n",
                survived, static_cast<int>(before.size()));
    EXPECT_EQ(survived, before.size())
        << "las baldosas se destruyen y se recrean con cada fotograma, así que un clic "
           "que empiece antes de uno y acabe después se pierde. Es el «a veces no "
           "cambia de imagen» del taller.";

    // Y con una pieza que SÍ se ha movido, hay que rehacerlas: el recorte de la
    // baldosa ya no es el de esa pieza.
    mosaic.setPieces(aScene(), threeOutlines(60.0), 1);
    // CON EL BUCLE DE EVENTOS CORRIDO. `rebuild()` usa `deleteLater()`, así que
    // las baldosas viejas siguen siendo hijas del panel hasta que Qt las recoge:
    // sin esta línea, esta comprobación no mira si se rehicieron, mira si Qt ya
    // hizo la limpieza. La primera comprobación de arriba no lo necesita —ahí lo
    // que delata la reconstrucción es que aparecerían SEIS hijos, tres viejos
    // pendientes de borrar y tres nuevos.
    QApplication::processEvents(QEventLoop::AllEvents);
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto moved = mosaic.findChildren<QToolButton*>();
    int stillThere = 0;
    for (auto* tile : before) {
        if (moved.contains(tile)) {
            ++stillThere;
        }
    }
    std::printf("  [mosaico] con las piezas movidas sobreviven %d\n", stillThere);
    EXPECT_EQ(stillThere, 0)
        << "las piezas se han movido y las baldosas siguen siendo las de antes: "
           "estarían enseñando un recorte que ya no corresponde";
}

TEST(MosaicClickSurvives, ChoosingATileStillReportsItsNumber) {
    // Lo que el panel existe para hacer, y que el arreglo de arriba no puede
    // romper: pulsar una baldosa dice cuál se ha elegido.
    ui::PieceMosaic mosaic;
    mosaic.resize(500, 200);
    mosaic.setPieces(aScene(), threeOutlines(), 1);

    QSignalSpy chosen(&mosaic, &ui::PieceMosaic::pieceChosen);
    const auto tiles = mosaic.findChildren<QToolButton*>();
    ASSERT_EQ(tiles.size(), 3);
    tiles[2]->click();

    ASSERT_EQ(chosen.count(), 1);
    std::printf("  [mosaico] pulsada la tercera baldosa -> pieza %d\n",
                chosen.at(0).at(0).toInt());
    EXPECT_EQ(chosen.at(0).at(0).toInt(), 3);
}

TEST(MosaicClickSurvives, ChangingTheMeasuredPieceMovesTheFrameWithoutRebuilding) {
    // Cambiar cuál se mide tiene que verse —la baldosa elegida lleva marco— y no
    // puede costar una reconstrucción: si la costara, volveríamos a comernos los
    // clics justo cuando el operador está eligiendo.
    ui::PieceMosaic mosaic;
    mosaic.resize(500, 200);
    mosaic.setPieces(aScene(), threeOutlines(), 1);
    const auto before = mosaic.findChildren<QToolButton*>();
    ASSERT_EQ(before.size(), 3);
    ASSERT_TRUE(before[0]->isChecked());

    mosaic.setPieces(aScene(), threeOutlines(), 3);
    const auto after = mosaic.findChildren<QToolButton*>();
    ASSERT_EQ(after.size(), 3);
    EXPECT_TRUE(after.contains(before[0])) << "cambiar de pieza medida rehace el panel";
    std::printf("  [mosaico] al pasar la medida a la 3: marcada la 1=%d, la 3=%d\n",
                before[0]->isChecked() ? 1 : 0, before[2]->isChecked() ? 1 : 0);
    EXPECT_FALSE(before[0]->isChecked());
    EXPECT_TRUE(before[2]->isChecked())
        << "la baldosa de la pieza que se mide no queda marcada, así que el operador no "
           "ve cuál eligió";
}
