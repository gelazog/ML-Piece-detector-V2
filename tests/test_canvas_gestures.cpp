// Pruebas de GESTO sobre el widget real: se renderiza fuera de pantalla y se
// le inyectan eventos de ratón sintéticos. Es lo único que comprueba de verdad
// que un clic, un arrastre o la rueda hagan lo que el operador espera; la
// aritmética por debajo la cubre test_canvas_geometry.
//
// Van en su propio ejecutable porque necesitan Qt y una plataforma "offscreen",
// que el resto de la suite no requiere.
#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPointF>
#include <QString>

#include <vector>

#include <QDialogButtonBox>
#include <QPushButton>
#include <QTableWidget>

#include "inspection_editor/auto_measure_dialog.h"
#include "inspection_editor/canvas/editor_canvas.h"

using namespace pci::inspection;

namespace {

void press(QWidget* w, QPointF pos) {
    QMouseEvent e(QEvent::MouseButtonPress, pos, w->mapToGlobal(pos), Qt::LeftButton,
                  Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}
void moveTo(QWidget* w, QPointF pos) {
    QMouseEvent e(QEvent::MouseMove, pos, w->mapToGlobal(pos), Qt::NoButton, Qt::LeftButton,
                  Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}
void release(QWidget* w, QPointF pos) {
    QMouseEvent e(QEvent::MouseButtonRelease, pos, w->mapToGlobal(pos), Qt::LeftButton,
                  Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}

void drag(QWidget* w, QPointF from, QPointF to) {
    press(w, from);
    moveTo(w, to);
    release(w, to);
}

QPointF toScreen(const ViewTransform& v, cv::Point2f p) {
    const cv::Point2d q = v.imageToWidget(p);
    return {q.x, q.y};
}

constexpr int kImageWidth = 1920;
constexpr int kImageHeight = 1080;
constexpr int kWidgetWidth = 900;
constexpr int kWidgetHeight = 640;

// La vista tal como queda el canvas: al ampliar desde el centro el
// desplazamiento se queda en cero, así que basta el zoom para reconstruirla.
ViewTransform viewAt(double zoom) {
    return ViewTransform({kImageWidth, kImageHeight}, {kWidgetWidth, kWidgetHeight}, zoom,
                         {0.0, 0.0});
}

// Imagen con un borde vertical nítido en x = 1000, para el imán.
QImage sceneWithAnEdge() {
    QImage image(kImageWidth, kImageHeight, QImage::Format_RGB888);
    image.fill(QColor(30, 30, 30));
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 1000; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor(220, 220, 220));
        }
    }
    return image;
}

// Banco común: canvas montado, con la lista de herramientas conectada como lo
// haría la ventana (el canvas solo EMITE la herramienta creada).
class CanvasGestureTest : public ::testing::Test {
protected:
    void SetUp() override {
        canvas.resize(kWidgetWidth, kWidgetHeight);
        canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
        canvas.setTools(&tools);
        QObject::connect(&canvas, &EditorCanvas::toolCreated,
                         [this](const ToolGeometry& g) {
                             EditedTool t;
                             t.config.id = static_cast<int>(tools.size()) + 1;
                             t.geometry = g;
                             tools.push_back(t);
                         });
        QObject::connect(&canvas, &EditorCanvas::traceRejected,
                         [this](const QString& r) { rejection = r; });
    }

    EditorCanvas canvas;
    std::vector<EditedTool> tools;
    QString rejection;
};

}  // namespace

// ---------------------------------------------------------------------------
// Zona de agarre: constante en pantalla, con cualquier zoom
// ---------------------------------------------------------------------------

TEST_F(CanvasGestureTest, ZoomedInAClickBesideAHandleDoesNotDragIt) {
    // Al zoom máximo la escala es 9,375: con la tolerancia en píxeles de imagen
    // (como estaba), una manija de 7 px dibujados se agarraba desde 84 px de
    // pantalla y un clic en un sitio vacío deformaba la herramienta.
    tools.resize(1);
    tools[0].config.id = 1;
    tools[0].config.type = ToolType::Caliper;
    tools[0].geometry = CaliperGeometry{{500.0F, 500.0F}, {600.0F, 500.0F}, 10.0F};
    canvas.setSelectedIndex(0);
    canvas.zoomToMax();
    ASSERT_NEAR(canvas.displayScale(), 9.375, 1e-6);

    const auto before = std::get<CaliperGeometry>(tools[0].geometry);
    // Perpendicular al trazo, para no caer sobre la línea (eso movería la
    // herramienta entera, que es lo correcto y no probaría la manija).
    const QPointF handle = toScreen(viewAt(20.0), {500.0F, 500.0F});
    drag(&canvas, handle + QPointF(0.0, 25.0), handle + QPointF(120.0, 60.0));

    const auto after = std::get<CaliperGeometry>(tools[0].geometry);
    EXPECT_NEAR(after.p0.x, before.p0.x, 0.01);
    EXPECT_NEAR(after.p0.y, before.p0.y, 0.01);
}

TEST_F(CanvasGestureTest, ZoomedInAClickOnTheHandleStillDragsIt) {
    tools.resize(1);
    tools[0].config.id = 1;
    tools[0].config.type = ToolType::Caliper;
    tools[0].geometry = CaliperGeometry{{500.0F, 500.0F}, {600.0F, 500.0F}, 10.0F};
    canvas.setSelectedIndex(0);
    canvas.zoomToMax();

    const QPointF handle = toScreen(viewAt(20.0), {500.0F, 500.0F});
    drag(&canvas, handle + QPointF(2.0, 2.0), handle + QPointF(60.0, 0.0));
    EXPECT_GT(std::get<CaliperGeometry>(tools[0].geometry).p0.x, 505.0F);
}

// ---------------------------------------------------------------------------
// Crear por arrastre: los dos mínimos, cada uno por su motivo
// ---------------------------------------------------------------------------

TEST_F(CanvasGestureTest, AShortButDeliberateStrokeCreatesATool) {
    // 30 px de pantalla al zoom de ajuste son 64 de imagen: gesto claro y
    // herramienta medible.
    canvas.setCreateType(ToolType::Ruler);
    const QPointF start = toScreen(viewAt(1.0), {800.0F, 300.0F});
    drag(&canvas, start, start + QPointF(30.0, 0.0));
    EXPECT_EQ(tools.size(), 1U);
    EXPECT_TRUE(rejection.isEmpty());
}

TEST_F(CanvasGestureTest, AThreePixelTremorIsAClickNotATool) {
    canvas.setCreateType(ToolType::Ruler);
    const QPointF start = toScreen(viewAt(1.0), {800.0F, 300.0F});
    drag(&canvas, start, start + QPointF(3.0, 1.0));
    EXPECT_TRUE(tools.empty());
    EXPECT_TRUE(rejection.isEmpty()) << "un clic no necesita explicación";
}

TEST_F(CanvasGestureTest, AStrokeTooSmallToMeasureIsRefusedOutLoud) {
    // Al zoom máximo, 30 px de pantalla son 3,2 de imagen: el gesto fue
    // deliberado, pero esa herramienta no tendría muestras que medir. Antes se
    // descartaba en silencio y el operador no entendía por qué no salía nada.
    canvas.zoomToMax();
    canvas.setCreateType(ToolType::Caliper);
    const QPointF start = toScreen(viewAt(20.0), {960.0F, 540.0F});
    drag(&canvas, start, start + QPointF(30.0, 0.0));

    EXPECT_TRUE(tools.empty());
    EXPECT_FALSE(rejection.isEmpty());
    EXPECT_TRUE(rejection.contains("corto")) << rejection.toStdString();
}

// ---------------------------------------------------------------------------
// Imán al borde
// ---------------------------------------------------------------------------

TEST_F(CanvasGestureTest, TheMagnetSnapsTheEndpointToANearbyEdge) {
    canvas.setCreateType(ToolType::Ruler);
    const ViewTransform v = viewAt(1.0);
    drag(&canvas, toScreen(v, {900.0F, 540.0F}), toScreen(v, {991.0F, 540.0F}));
    ASSERT_EQ(tools.size(), 1U);
    // El borde está en x = 1000; el extremo se soltó en 991 y se pega con
    // precisión subpíxel.
    EXPECT_NEAR(std::get<RulerGeometry>(tools[0].geometry).p1.x, 1000.0F, 4.0F);
}

TEST_F(CanvasGestureTest, ZoomedInTheMagnetNoLongerReachesAcrossTheScreen) {
    // 12 px de imagen son 112 de pantalla al máximo: un borde ahí no está
    // "cerca" de donde se soltó, y arrastrar el extremo hasta él era un salto
    // que el operador no pedía.
    canvas.zoomToMax();
    canvas.setCreateType(ToolType::Ruler);
    const ViewTransform v = viewAt(20.0);
    drag(&canvas, toScreen(v, {960.0F, 540.0F}), toScreen(v, {988.0F, 540.0F}));
    ASSERT_EQ(tools.size(), 1U);
    EXPECT_NEAR(std::get<RulerGeometry>(tools[0].geometry).p1.x, 988.0F, 4.0F);
}

// ---------------------------------------------------------------------------
// Un arrastre pertenece a la herramienta en la que empezó
// ---------------------------------------------------------------------------

TEST_F(CanvasGestureTest, ChangingTheSelectionMidDragDoesNotDeformTheNewTool) {
    tools.resize(2);
    tools[0].config.id = 1;
    tools[0].config.type = ToolType::LineToLine;
    tools[0].geometry = LineToLineGeometry{
        {200.0F, 200.0F}, {400.0F, 200.0F}, {200.0F, 400.0F}, {400.0F, 400.0F}};
    tools[1].config.id = 2;
    tools[1].config.type = ToolType::Ruler;
    tools[1].geometry = RulerGeometry{{800.0F, 800.0F}, {900.0F, 800.0F}};
    canvas.setSelectedIndex(0);

    const ViewTransform v = viewAt(1.0);
    const auto before = std::get<RulerGeometry>(tools[1].geometry);
    press(&canvas, toScreen(v, {400.0F, 400.0F}));  // manija 3 de la Línea-Línea
    canvas.setSelectedIndex(1);                     // la lista del panel cambia sola
    moveTo(&canvas, toScreen(v, {700.0F, 700.0F}));
    release(&canvas, toScreen(v, {700.0F, 700.0F}));

    const auto after = std::get<RulerGeometry>(tools[1].geometry);
    EXPECT_NEAR(after.p1.x, before.p1.x, 0.01);
    EXPECT_NEAR(after.p1.y, before.p1.y, 0.01);
}

// ---------------------------------------------------------------------------
// Vista
// ---------------------------------------------------------------------------

TEST_F(CanvasGestureTest, ZoomStepsStayWithinTheirLimits) {
    canvas.resetView();
    EXPECT_TRUE(canvas.atMinZoom());
    for (int i = 0; i < 40; ++i) {
        canvas.zoomIn();
    }
    EXPECT_TRUE(canvas.atMaxZoom());
    for (int i = 0; i < 60; ++i) {
        canvas.zoomOut();
    }
    EXPECT_TRUE(canvas.atMinZoom()) << "salir del zoom devuelve al encuadre completo";
    canvas.zoomToMax();
    EXPECT_TRUE(canvas.atMaxZoom());
    canvas.zoomToMin();
    EXPECT_TRUE(canvas.atMinZoom());
}

// ---------------------------------------------------------------------------
// Etiquetas de medida sobre el lienzo pintado
// ---------------------------------------------------------------------------

TEST_F(CanvasGestureTest, MeasurementLabelsAreDrawnInsideTheView) {
    // Comprueba el cableado real: que paintResults le pase al colocador el área
    // visible. Anclas pegadas al borde inferior, que es donde la versión
    // anterior empujaba las etiquetas fuera de la vista.
    // Las etiquetas se pintan en verde (OK) o rojo (NG); contar píxeles de esos
    // colores dice cuánta etiqueta hay realmente DENTRO del lienzo. Se compara
    // una sola medida contra catorce: si se estuvieran yendo por el borde, la
    // cuenta no crecería con ellas. Con la versión anterior la primera se veía
    // igual, así que un umbral absoluto no habría probado nada.
    const auto colouredPixels = [this](int count) {
        std::vector<ToolRunResult> results;
        for (int i = 0; i < count; ++i) {
            ToolRunResult r;
            r.toolId = i + 1;
            r.name = "medida" + std::to_string(i);
            r.type = ToolType::Ruler;
            r.ok = (i % 2 == 0);
            r.measured = 12.34 + i;
            r.overlayPoints = {{1900.0F, 1070.0F}};  // esquina inferior derecha
            results.push_back(r);
        }
        canvas.setResults(results);

        QImage rendered(kWidgetWidth, kWidgetHeight, QImage::Format_ARGB32);
        rendered.fill(Qt::black);
        canvas.render(&rendered);

        int coloured = 0;
        for (int y = 0; y < rendered.height(); ++y) {
            for (int x = 0; x < rendered.width(); ++x) {
                const QColor c = rendered.pixelColor(x, y);
                const bool greenish = c.green() > 150 && c.red() < 120 && c.blue() < 120;
                const bool reddish = c.red() > 180 && c.green() < 120 && c.blue() < 120;
                if (greenish || reddish) {
                    ++coloured;
                }
            }
        }
        return coloured;
    };

    const int one = colouredPixels(1);
    const int fourteen = colouredPixels(14);
    ASSERT_GT(one, 100) << "ni siquiera una etiqueta suelta se ve";
    EXPECT_GT(fourteen, one * 8)
        << "catorce medidas en la esquina inferior pintan " << fourteen
        << " px frente a " << one << " de una sola: se están yendo fuera de la vista";
}


// ---------------------------------------------------------------------------
// Diálogo de revisión de la medición automática
// ---------------------------------------------------------------------------

TEST(AutoMeasureDialogTest, StartsWithEverythingCheckedAndReturnsOnlyWhatStaysChecked) {
    // Lo normal es querer casi todas, así que revisar consiste en DESMARCAR lo
    // que sobra. Si empezaran todas vacías, el operador tendría que marcar de
    // una en una y el botón dejaría de ahorrar trabajo.
    std::vector<AutoProposal> proposals(3);
    for (int i = 0; i < 3; ++i) {
        proposals[static_cast<std::size_t>(i)].config.type = ToolType::Ruler;
        proposals[static_cast<std::size_t>(i)].config.name = "cota" + std::to_string(i);
        proposals[static_cast<std::size_t>(i)].measured = 10.0 * (i + 1);
        proposals[static_cast<std::size_t>(i)].reason = "porque sí";
    }

    AutoMeasureDialog dialog(proposals);
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 3);
    for (int row = 0; row < 3; ++row) {
        EXPECT_EQ(table->item(row, 0)->checkState(), Qt::Checked);
    }

    // Se desmarca la del medio y se acepta.
    table->item(1, 0)->setCheckState(Qt::Unchecked);
    dialog.accept();
    const auto accepted = dialog.accepted();
    ASSERT_EQ(accepted.size(), 2U);
    EXPECT_EQ(accepted[0].config.name, "cota0");
    EXPECT_EQ(accepted[1].config.name, "cota2");
}

TEST(AutoMeasureDialogTest, CancellingInsertsNothing) {
    std::vector<AutoProposal> proposals(2);
    proposals[0].config.name = "a";
    proposals[1].config.name = "b";
    AutoMeasureDialog dialog(proposals);
    dialog.reject();
    EXPECT_TRUE(dialog.accepted().empty()) << "cancelar no puede insertar nada";
}

TEST(AutoMeasureDialogTest, TheAcceptButtonSaysHowManyItWillInsert) {
    // Sin ese número hay que contar las casillas a mano antes de pulsar.
    std::vector<AutoProposal> proposals(4);
    for (int i = 0; i < 4; ++i) {
        proposals[static_cast<std::size_t>(i)].config.name = "c" + std::to_string(i);
    }
    AutoMeasureDialog dialog(proposals);
    auto* table = dialog.findChild<QTableWidget*>();
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(table, nullptr);
    ASSERT_NE(buttons, nullptr);
    auto* ok = buttons->button(QDialogButtonBox::Ok);
    ASSERT_NE(ok, nullptr);
    EXPECT_TRUE(ok->text().contains("4")) << ok->text().toStdString();

    table->item(0, 0)->setCheckState(Qt::Unchecked);
    table->item(1, 0)->setCheckState(Qt::Unchecked);
    EXPECT_TRUE(ok->text().contains("2")) << ok->text().toStdString();

    // Y sin nada marcado no deja aceptar: no tendría efecto.
    for (int row = 0; row < 4; ++row) {
        table->item(row, 0)->setCheckState(Qt::Unchecked);
    }
    EXPECT_FALSE(ok->isEnabled());
}

TEST(AutoMeasureDialogTest, TheFullReadingIsKeptInTheTooltip) {
    // En la celda solo cabe el número, pero un aviso de condiciones de medida
    // -cámara inclinada, poco contraste- no se puede perder por el camino.
    std::vector<AutoProposal> proposals(1);
    proposals[0].config.name = "Ø agujero 1";
    proposals[0].measured = 70.1;
    proposals[0].detail = "D=70.1px ⚠ cámara inclinada respecto al plano";
    proposals[0].reason = "Agujero interno";
    AutoMeasureDialog dialog(proposals);
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_TRUE(table->item(0, 2)->toolTip().contains("inclinada"))
        << table->item(0, 2)->toolTip().toStdString();
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
