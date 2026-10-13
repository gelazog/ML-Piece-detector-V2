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

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolButton>

#include <cmath>
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

// Los ids de las cotas que enseña ahora el panel, por el nombre de su celda.
QStringList cotaIds(const ui::MeasurementsPanel& panel) {
    QStringList ids;
    for (auto* cell : panel.findChildren<QWidget*>()) {
        if (cell->objectName().startsWith(QStringLiteral("cotaCell_"))) {
            ids << cell->objectName().mid(QStringLiteral("cotaCell_").size());
        }
    }
    return ids;
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
    // Ni siquiera el rato que tarda el análisis nuevo: el panel se vacía en el
    // mismo clic, en vez de enseñar un ciclo más las medidas de la anterior.
    next->click();
    EXPECT_EQ(panel->rowCount(), 0)
        << "justo al cambiar de pieza el panel sigue enseñando la anterior";
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

// SI LA PIEZA ELEGIDA DEJA DE VERSE, SUS COTAS NO PASAN A LA MAYOR.
//
// Con la 3 elegida y medida, basta con que la 3 deje de verse —sale de la cinta,
// o una zona de trabajo la deja fuera— para que el análisis vuelva a medir la
// mayor. Ese paso ponía el número a cero a pelo, sin pasar por `focusPiece`, que
// es quien cambia las cotas de pieza: las de la 3 se quedaban midiendo la mayor
// y las de la mayor seguían guardadas aparte. Con la siguiente flecha, las de la
// 3 se guardaban COMO de la mayor, encima de las suyas, y éstas se perdían.
TEST(EachPieceItsOwnMeasures, WhenTheChosenPieceLeavesItsCotasDoNotMoveToTheLargest) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // 1 pequeña y 2 la mayor arriba; 3 mediana abajo, la que se va a quedar fuera.
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
    auto* next = window.findChild<QToolButton*>(QStringLiteral("pieceNextButton"));
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(next, nullptr);
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() == 3 && next->isVisible(); }));

    const auto drawCircle = [&](float radius) {
        inspection::CircleGeometry circle;
        circle.center = cv::Point2f(0.0F, 0.0F);
        circle.radius = radius;
        circle.searchBand = 15.0F;
        emit canvas->toolCreated(inspection::ToolGeometry{circle});
    };

    // Una cota en la mayor, que es la que se mide sin elegir nada.
    drawCircle(30.0F);
    ASSERT_TRUE(waitFor([&] { return cotaIds(*panel).size() == 1; }));
    const QString ofTheLargest = cotaIds(*panel).front();

    // A la 3 —pasando por la 1 y la 2— y una cota suya.
    for (int i = 0; i < 3; ++i) {
        next->click();
    }
    ASSERT_TRUE(waitFor([&] { return cotaIds(*panel).isEmpty(); }))
        << "la 3 no empieza vacía";
    drawCircle(20.0F);
    ASSERT_TRUE(waitFor([&] { return cotaIds(*panel).size() == 1; }));
    const QString ofTheThird = cotaIds(*panel).front();
    ASSERT_NE(ofTheThird, ofTheLargest);

    // Una zona que deja fuera la 3: se queda la pequeña y la mayor, y se vuelve
    // a medir la mayor.
    emit canvas->regionPicked(cv::Rect(0, 0, 400, 170));
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() == 2; }))
        << "la zona no deja fuera a la 3";

    EXPECT_TRUE(waitFor([&] { return cotaIds(*panel) == QStringList{ofTheLargest}; }))
        << "al dejar de verse la 3 se mide la mayor, y el panel enseña las cotas «"
        << cotaIds(*panel).join(QStringLiteral(", ")).toStdString()
        << "» en vez de la suya («" << ofTheLargest.toStdString() << "»)";
}

// CON EL CONTORNO OCULTO, PASAR A OTRA PIEZA MIDE LA OTRA PIEZA.
//
// Con «Mostrar contorno» apagado la pose se congela: las cotas se miden con el
// fixture del último análisis, para que nada tiemble sobre una pieza quieta. Ese
// fixture es el de la pieza que se estaba mirando, y al pasar a otra con las
// flechas se seguía usando: las cotas de la nueva se medían donde estaba la
// ANTERIOR. Aquí, un círculo de Ø 50 trazado sobre el disco pequeño buscaba su
// borde dentro del grande y no medía nada. El mismo fixture viejo es el que
// estabiliza el de la pieza nueva con «Seguir rotación»: su giro se decidía
// contra el de la anterior.
TEST(EachPieceItsOwnMeasures, WithTheContourHiddenTheNewPieceIsMeasuredWhereItIs) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // 1 disco pequeño (Ø 50) y 2 el mayor (Ø 100) arriba; 3 mediano abajo.
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(230, 230, 230));
        painter.drawEllipse(QPointF(80, 80), 25, 25);
        painter.drawEllipse(QPointF(280, 90), 50, 50);
        painter.drawEllipse(QPointF(110, 220), 35, 35);
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("discos.png"));
    ASSERT_TRUE(photo.save(path));

    ui::MainWindow window;  // sin repositorios: ninguna pieza registrada
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Image, path));
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    auto* panel = window.findChild<ui::MeasurementsPanel*>();
    auto* next = window.findChild<QToolButton*>(QStringLiteral("pieceNextButton"));
    auto* label = window.findChild<QLabel*>(QStringLiteral("pieceNavLabel"));
    auto* contour = window.findChild<QAction*>(QStringLiteral("showContourAction"));
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(next, nullptr);
    ASSERT_NE(label, nullptr);
    ASSERT_NE(contour, nullptr);
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() == 3 && next->isVisible(); }));

    // El diámetro que enseña el panel para la única cota, o −1 si no hay número.
    const auto measuredDiameter = [&]() {
        const QStringList ids = cotaIds(*panel);
        if (ids.size() != 1) {
            return -1.0;
        }
        auto* cell = panel->findChild<QLabel*>(QStringLiteral("valueCell_") + ids.front());
        if (cell == nullptr) {
            return -1.0;
        }
        const auto match = QRegularExpression(QStringLiteral("\\d+(\\.\\d+)?")).match(cell->text());
        return match.hasMatch() ? match.captured(0).toDouble() : -1.0;
    };

    // A la 1, el disco pequeño, y un círculo justo en su borde.
    next->click();
    ASSERT_TRUE(waitFor([&] { return label->text().contains(QStringLiteral("pieza 1/3")); }));
    inspection::CircleGeometry circle;
    circle.center = cv::Point2f(0.0F, 0.0F);
    circle.radius = 25.0F;
    circle.searchBand = 10.0F;
    emit canvas->toolCreated(inspection::ToolGeometry{circle});
    ASSERT_TRUE(waitFor([&] { return std::abs(measuredDiameter() - 50.0) < 5.0; }))
        << "el círculo no mide el disco pequeño (" << measuredDiameter() << ")";

    // A la mayor, que se queda sin cotas, y ahí se oculta el contorno: la pose
    // se congela sobre ella.
    for (int i = 0; i < 3; ++i) {
        next->click();
    }
    ASSERT_TRUE(waitFor([&] {
        return label->text().contains(QStringLiteral("la mayor")) && cotaIds(*panel).isEmpty();
    }));

    // De vuelta a la 1, con un análisis de la mayor todavía en vuelo —lo que
    // pasa cuando la imagen se reemite justo antes del clic—: ese análisis
    // termina después del cambio y no puede devolver el fixture de la mayor. Se
    // lanza a propósito para que la carrera ocurra siempre, y no solo con la
    // máquina cargada, que es como apareció: su resultado solo se entrega al
    // volver al bucle de eventos, después del clic.
    window.declareExpectedPieces(0);  // ya era 0: solo vuelve a analizar
    contour->trigger();
    ASSERT_FALSE(contour->isChecked());
    next->click();
    EXPECT_TRUE(waitFor([&] { return std::abs(measuredDiameter() - 50.0) < 5.0; }))
        << "con el contorno oculto, la cota de la pieza 1 se mide donde estaba la mayor: el "
           "panel da "
        << measuredDiameter();
}
