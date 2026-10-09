// CADA PIEZA DEL ENCUADRE CON SUS PROPIAS MEDIDAS, MIENTRAS NO HAYA PLANTILLA.
//
// Queja del dueño: con varias piezas distintas en el encuadre y ninguna
// registrada, medías una («Medir pieza», o dibujando), pasabas a otra con las
// flechas o el mosaico, y las cotas de la primera se quedaban puestas y medían
// la segunda. La lista de herramientas era una sola para toda la pantalla, en
// coordenadas de pieza, así que se aplicaba tal cual a una pieza que no tenía
// nada que ver: un círculo de 20 px en el centro de un tornillo, un ancho sobre
// una arandela.
//
// Con una pieza REGISTRADA no cambia nada: su plantilla vale para todas las de
// la bandeja, que es justo para lo que existe. Sin plantilla, cada pieza guarda
// las suyas, y volver a una devuelve lo que se le midió.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolButton>

#include <functional>
#include <vector>

#include "camera/frame_source.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "ui/main_window.h"
#include "ui/measurements_panel.h"

using namespace pci;

namespace {

bool waitFor(const std::function<bool()>& condition, int ms = 8000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (condition()) {
            return true;
        }
    }
    return condition();
}

}  // namespace

TEST(EachPieceItsOwnMeasures, SwitchingPieceDoesNotCarryTheOtherPiecesCotas) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // Tres piezas de tamaños distintos, en orden de lectura: 1 pequeña, 2 la
    // mayor, 3 mediana. Es la escena del selector de piezas.
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(60, 60, 60, 50), QColor(230, 230, 230));
        painter.fillRect(QRect(250, 50, 120, 100), QColor(230, 230, 230));
        painter.fillRect(QRect(70, 200, 90, 70), QColor(230, 230, 230));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("tres.png"));
    ASSERT_TRUE(photo.save(path));

    ui::MainWindow window;  // sin repositorios: ninguna pieza registrada
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Image, path));
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    auto* panel = window.findChild<ui::MeasurementsPanel*>();
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() >= 3; }))
        << "no se ven las tres piezas";
    auto* next = window.findChild<QToolButton*>(QStringLiteral("pieceNextButton"));
    ASSERT_NE(next, nullptr);
    ASSERT_TRUE(waitFor([&] { return next->isVisible(); })) << "no aparece el selector";

    // Una cota sobre la que se está midiendo, la mayor (la 2).
    inspection::CircleGeometry circle;
    circle.center = cv::Point2f(0.0F, 0.0F);
    circle.radius = 30.0F;
    circle.searchBand = 15.0F;
    emit canvas->toolCreated(inspection::ToolGeometry{circle});
    ASSERT_TRUE(waitFor([&] { return panel->rowCount() > 0; }))
        << "la cota dibujada no llega a medirse";

    // A la pieza 1, que no se ha medido: no puede llevarse la cota de la 2.
    next->click();
    EXPECT_TRUE(waitFor([&] { return panel->rowCount() == 0; }))
        << "al pasar a otra pieza siguen puestas las medidas de la anterior ("
        << panel->rowCount() << " fila(s))";
    // Y dice por qué el panel se ha quedado vacío y qué hacer, en vez de dejar
    // al operador pensando que dejó de medir.
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("Medir pieza")))
        << "no explica que esta pieza aún no tiene medidas: «"
        << window.statusBar()->currentMessage().toStdString() << "»";

    // A la 2 por su número: es la pieza en la que se dibujó, así que sus
    // medidas vuelven. Que «la mayor» y «la 2» sean la misma pieza es lo que
    // el operador ve, y es lo que cuenta.
    next->click();
    EXPECT_TRUE(waitFor([&] { return panel->rowCount() > 0; }))
        << "volver a la pieza medida no devuelve sus medidas";
}

// DOS COTAS SIN GUARDAR SON DOS FILAS, Y LA ✕ DE CADA UNA BORRA LA SUYA.
//
// Toda cota nacía con id −1 («aún sin guardar») y el panel de medidas agrupa e
// identifica las filas por id. Con dos o más sin guardar —lo normal justo
// después de «Medir pieza» y «Vigilar estas cotas»— se juntaban en una sola
// fila, y el ojo, la ✕ y el clic en la fila iban siempre a la primera. Ocultar
// una las ocultaba todas.
TEST(EachPieceItsOwnMeasures, TwoUnsavedCotasAreTwoRowsAndEachCrossDeletesItsOwn) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.setBrush(QColor(230, 230, 230));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(200, 150), 80, 80);
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("disco.png"));
    ASSERT_TRUE(photo.save(path));

    ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Image, path));
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    auto* panel = window.findChild<ui::MeasurementsPanel*>();
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() >= 1; }));

    for (const float radius : {80.0F, 40.0F}) {
        inspection::CircleGeometry circle;
        circle.center = cv::Point2f(0.0F, 0.0F);
        circle.radius = radius;
        circle.searchBand = 20.0F;
        emit canvas->toolCreated(inspection::ToolGeometry{circle});
    }
    EXPECT_TRUE(waitFor([&] { return panel->rowCount() == 2; }))
        << "dos cotas dibujadas y el panel enseña " << panel->rowCount() << " fila(s)";

    std::vector<QToolButton*> crosses;
    for (auto* button : panel->findChildren<QToolButton*>()) {
        if (button->objectName().startsWith(QStringLiteral("deleteButton_"))) {
            crosses.push_back(button);
        }
    }
    ASSERT_EQ(crosses.size(), 2U) << "no hay una ✕ por cota";
    const QString kept = crosses.front()->objectName().mid(QStringLiteral("deleteButton_").size());
    crosses.back()->click();
    ASSERT_TRUE(waitFor([&] { return panel->rowCount() == 1; }))
        << "la ✕ no quitó ninguna fila";
    bool firstStillThere = false;
    for (auto* label : panel->findChildren<QWidget*>()) {
        if (label->objectName() == QStringLiteral("cotaCell_") + kept) {
            firstStillThere = true;
        }
    }
    EXPECT_TRUE(firstStillThere) << "la ✕ de la segunda cota borró la primera";
}
