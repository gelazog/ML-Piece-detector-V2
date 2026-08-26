// EL CLIC DERECHO PIDE OPCIONES; ANTES BORRABA.
//
// Petición de uso: «agrega alguna función al clic derecho». Al ir a hacerlo
// apareció algo peor que un hueco: el clic derecho sobre una cota LA BORRABA en
// el acto, sin menú y sin preguntar.
//
// En cualquier otro programa ese gesto significa «enséñame qué puedo hacer
// aquí». Aquí era el único que destruía trabajo, y bastaba errar el botón del
// ratón una vez sobre la cota equivocada.
//
// Esta prueba mira la LONA, que es donde estaba el fallo: que el clic derecho
// avise en vez de actuar, y que avise también sobre el vacío —que tiene sus
// propias acciones— en vez de callarse.

#include <gtest/gtest.h>

#include <QApplication>
#include <QSignalSpy>
#include <QTest>

#include <cstdio>

#include "inspection_editor/canvas/editor_canvas.h"

using namespace pci;

namespace {

QImage aFlatImage() {
    QImage image(400, 300, QImage::Format_RGB32);
    image.fill(Qt::white);
    return image;
}

}  // namespace

TEST(ContextMenu, RightClickAsksInsteadOfDeleting) {
    inspection::EditorCanvas canvas;
    canvas.setFrame(aFlatImage());
    canvas.resize(400, 300);

    QSignalSpy asked(&canvas, &inspection::EditorCanvas::contextMenuRequested);
    QSignalSpy deleted(&canvas, &inspection::EditorCanvas::toolRightClicked);
    ASSERT_TRUE(asked.isValid());

    QTest::mouseClick(&canvas, Qt::RightButton, Qt::NoModifier, QPoint(200, 150));

    std::printf("  [menú] clic derecho -> pide opciones %d vez(es), borra %d\n",
                static_cast<int>(asked.count()), static_cast<int>(deleted.count()));
    EXPECT_EQ(asked.count(), 1)
        << "el clic derecho no pide opciones: no hay menú que enseñar";
    EXPECT_EQ(deleted.count(), 0)
        << "el clic derecho sigue borrando en el acto: es el gesto de pedir "
           "información y es el único que destruye trabajo";
}

TEST(ContextMenu, OnEmptySpaceItStillAsks) {
    // El vacío tiene sus propias acciones —marcar el rasgo aquí, encuadrar— así
    // que callarse ahí deja media pantalla sin menú por ninguna razón.
    inspection::EditorCanvas canvas;
    canvas.setFrame(aFlatImage());
    canvas.resize(400, 300);
    QSignalSpy asked(&canvas, &inspection::EditorCanvas::contextMenuRequested);

    QTest::mouseClick(&canvas, Qt::RightButton, Qt::NoModifier, QPoint(20, 20));

    ASSERT_EQ(asked.count(), 1) << "sobre el vacío el clic derecho no hace nada";
    // Sin cota debajo, se avisa con -1: quien monte el menú tiene que poder
    // distinguir «no hay nada aquí» de «hay algo».
    const int tool = asked.at(0).at(0).toInt();
    std::printf("  [menú] sobre el vacío avisa con tool=%d\n", tool);
    EXPECT_EQ(tool, -1) << "dice que hay una cota donde no la hay";
}

TEST(ContextMenu, ItSaysWhereItWasClicked) {
    // El punto viaja con el aviso porque una de las acciones —marcar el rasgo
    // distintivo— actúa SOBRE ESE PUNTO. Sin él haría falta el modo de antes:
    // pulsar un botón, dejar el programa esperando y acertar con otro clic.
    inspection::EditorCanvas canvas;
    canvas.setFrame(aFlatImage());
    canvas.resize(400, 300);
    QSignalSpy asked(&canvas, &inspection::EditorCanvas::contextMenuRequested);

    QTest::mouseClick(&canvas, Qt::RightButton, Qt::NoModifier, QPoint(120, 90));
    ASSERT_EQ(asked.count(), 1);

    const auto point = asked.at(0).at(2).value<cv::Point2f>();
    std::printf("  [menú] pulsado en (120,90) -> imagen (%.0f, %.0f)\n", point.x, point.y);
    // La imagen es de 400x300 en una lona de 400x300: el punto de imagen tiene
    // que caer dentro, y cerca de donde se pulsó.
    EXPECT_GE(point.x, 0.0F);
    EXPECT_GE(point.y, 0.0F);
    EXPECT_LE(point.x, 400.0F);
    EXPECT_LE(point.y, 300.0F);
}
