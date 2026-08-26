// LO QUE INCOMODABA DEL PINCEL: NO SE PODÍA ACERCAR.
//
// Queja de uso: «hay algo que se siente incómodo al momento de usar los
// pinceles».
//
// Una parte medible de ese algo: con el pincel encendido, la rueda cambiaba SU
// tamaño y no había ninguna forma de hacer zoom. Y perfilar un borde a mano es
// justo cuando más falta hace acercarse — para hacerlo había que apagar el
// pincel, mover la rueda, y volver a encenderlo. Tres gestos para uno.
//
// Ctrl+rueda es lo que ya hacen Krita, GIMP y Photoshop para exactamente esto.

#include <gtest/gtest.h>

#include <QApplication>
#include <QWheelEvent>

#include <cstdio>

#include "inspection_editor/canvas/editor_canvas.h"

using namespace pci;

namespace {

QImage aFlatImage() {
    QImage image(400, 300, QImage::Format_RGB32);
    image.fill(Qt::white);
    return image;
}

void turnTheWheel(inspection::EditorCanvas& canvas, int notches,
                  Qt::KeyboardModifiers modifiers) {
    QWheelEvent event(QPointF(200, 150), canvas.mapToGlobal(QPoint(200, 150)),
                      QPoint(0, 0), QPoint(0, notches * 120), Qt::NoButton, modifiers,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&canvas, &event);
}

}  // namespace

TEST(BrushComfort, WithTheBrushOnTheWheelStillSizesIt) {
    // Lo de siempre no cambia: la rueda sola sigue siendo el tamaño, que es lo
    // que se ajusta a cada momento mientras se corrige.
    inspection::EditorCanvas canvas;
    canvas.setFrame(aFlatImage());
    canvas.resize(400, 300);
    canvas.setEdgeBrush(inspection::EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(20);

    turnTheWheel(canvas, 3, Qt::NoModifier);
    std::printf("  [pincel] rueda sola: radio 20 -> %d\n", canvas.brushRadius());
    EXPECT_GT(canvas.brushRadius(), 20)
        << "la rueda sola ha dejado de cambiar el tamaño del pincel";
}

TEST(BrushComfort, CtrlWheelZoomsWithoutTurningTheBrushOff) {
    inspection::EditorCanvas canvas;
    canvas.setFrame(aFlatImage());
    canvas.resize(400, 300);
    canvas.setEdgeBrush(inspection::EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(20);
    const double before = canvas.zoomFactor();

    turnTheWheel(canvas, 3, Qt::ControlModifier);

    std::printf("  [pincel] Ctrl+rueda: zoom %.2f -> %.2f, radio sigue en %d\n", before,
                canvas.zoomFactor(), canvas.brushRadius());
    EXPECT_GT(canvas.zoomFactor(), before)
        << "con el pincel puesto no hay manera de acercarse: hay que apagarlo, "
           "hacer zoom y volver a encenderlo";
    EXPECT_EQ(canvas.brushRadius(), 20)
        << "Ctrl+rueda acerca Y además cambia el pincel: un gesto, dos efectos";
    EXPECT_EQ(canvas.edgeBrush(), inspection::EditorCanvas::EdgeBrush::AddPiece)
        << "acercarse ha apagado el pincel";
}

TEST(BrushComfort, CtrlWheelAlsoZoomsWithTheBrushOff) {
    // El gesto no puede depender del modo: uno que solo vale a veces se acaba
    // no usando, y el operador no tiene por qué recordar en cuál está.
    inspection::EditorCanvas canvas;
    canvas.setFrame(aFlatImage());
    canvas.resize(400, 300);
    canvas.setEdgeBrush(inspection::EditorCanvas::EdgeBrush::Off);
    const double before = canvas.zoomFactor();
    turnTheWheel(canvas, 2, Qt::ControlModifier);
    EXPECT_GT(canvas.zoomFactor(), before) << "Ctrl+rueda no acerca con el pincel apagado";
}
