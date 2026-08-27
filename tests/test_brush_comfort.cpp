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
#include <QKeyEvent>
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

TEST(BrushComfort, WithTheBrushOnTheWheelZoomsAndLeavesTheBrushAlone) {
    // ESTA PRUEBA DECÍA LO CONTRARIO, y se cambió a petición del taller:
    // «quiero hacerle zoom a la imagen, pero se agranda o se achica el cursor, y
    // me arruina la experiencia».
    //
    // El comentario que sostenía lo anterior afirmaba que dimensionar con la
    // rueda «es lo que hace cualquier editor». Es falso: Krita, GIMP y Photoshop
    // ponen el ZOOM en la rueda y el tamaño del pincel en las teclas [ y ].
    inspection::EditorCanvas canvas;
    canvas.setFrame(aFlatImage());
    canvas.resize(400, 300);
    canvas.setEdgeBrush(inspection::EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(20);
    const double before = canvas.zoomFactor();

    turnTheWheel(canvas, 3, Qt::NoModifier);
    std::printf("  [pincel] rueda sola: zoom %.2f -> %.2f, radio %d\n", before,
                canvas.zoomFactor(), canvas.brushRadius());
    EXPECT_GT(canvas.zoomFactor(), before)
        << "la rueda no acerca con el pincel puesto, que es exactamente la queja";
    EXPECT_EQ(canvas.brushRadius(), 20)
        << "la rueda sigue cambiando el tamaño del pincel a espaldas de quien solo "
           "quería acercarse";
}

TEST(BrushComfort, AltWheelSizesTheBrushForWhoDoesNotWantToLetGoOfTheMouse) {
    inspection::EditorCanvas canvas;
    canvas.setFrame(aFlatImage());
    canvas.resize(400, 300);
    canvas.setEdgeBrush(inspection::EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(20);
    const double zoomBefore = canvas.zoomFactor();

    turnTheWheel(canvas, 3, Qt::AltModifier);
    std::printf("  [pincel] Alt+rueda: radio 20 -> %d (zoom sin tocar: %.2f)\n",
                canvas.brushRadius(), canvas.zoomFactor());
    EXPECT_GT(canvas.brushRadius(), 20) << "Alt+rueda no dimensiona el pincel";
    EXPECT_DOUBLE_EQ(canvas.zoomFactor(), zoomBefore)
        << "Alt+rueda además acerca: entonces hace dos cosas a la vez y ninguna bien";
}

TEST(BrushComfort, TheBracketKeysSizeTheBrushLikeInEveryEditor) {
    // [ y ] es donde las busca cualquiera que venga de Krita, GIMP o Photoshop.
    // Antes no había ninguna tecla: el tamaño solo se cambiaba con la rueda, que
    // es justo lo que estorbaba.
    inspection::EditorCanvas canvas;
    canvas.setFrame(aFlatImage());
    canvas.resize(400, 300);
    canvas.setEdgeBrush(inspection::EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(20);

    QKeyEvent bigger(QEvent::KeyPress, Qt::Key_BracketRight, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &bigger);
    const int grown = canvas.brushRadius();
    std::printf("  [pincel] ]: radio 20 -> %d\n", grown);
    EXPECT_GT(grown, 20) << "la tecla ] no agranda el pincel";

    QKeyEvent smaller(QEvent::KeyPress, Qt::Key_BracketLeft, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &smaller);
    std::printf("  [pincel] [: radio %d -> %d\n", grown, canvas.brushRadius());
    EXPECT_LT(canvas.brushRadius(), grown) << "la tecla [ no encoge el pincel";
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

TEST(BrushComfort, ShiftWheelSizesTheBrushToo) {
    // Segundo modificador, a petición del taller. Alt ya estaba, y el lienzo
    // vive también en la ventana principal, que sí tiene barra de menús con
    // mnemónicos Alt+letra: ahí Alt está cargado y el gesto se vuelve incómodo.
    //
    // Lo que se pidió primero fue Ctrl+rueda y se descartó con el motivo por
    // delante: Ctrl+rueda es ZOOM en GIMP, en Krita, en los navegadores, en VS
    // Code y en el explorador de Windows. Es la costumbre más fuerte que hay con
    // una rueda. Shift, en cambio, aquí no hacía nada.
    inspection::EditorCanvas canvas;
    canvas.setFrame(aFlatImage());
    canvas.resize(400, 300);
    canvas.setEdgeBrush(inspection::EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(20);
    const double zoomBefore = canvas.zoomFactor();

    turnTheWheel(canvas, 3, Qt::ShiftModifier);
    std::printf("  [pincel] Shift+rueda: radio 20 -> %d (zoom sin tocar: %.2f)\n",
                canvas.brushRadius(), canvas.zoomFactor());
    EXPECT_GT(canvas.brushRadius(), 20) << "Shift+rueda no dimensiona el pincel";
    EXPECT_DOUBLE_EQ(canvas.zoomFactor(), zoomBefore)
        << "Shift+rueda además acerca: entonces hace dos cosas a la vez y ninguna bien";
}
