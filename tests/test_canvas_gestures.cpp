// Pruebas de GESTO sobre el widget real: se renderiza fuera de pantalla y se
// le inyectan eventos de ratón sintéticos. Es lo único que comprueba de verdad
// que un clic, un arrastre o la rueda hagan lo que el operador espera; la
// aritmética por debajo la cubre test_canvas_geometry.
//
// Van en su propio ejecutable porque necesitan Qt y una plataforma "offscreen",
// que el resto de la suite no requiere.
#include <gtest/gtest.h>

#include <QApplication>
#include <QAbstractButton>
#include <QColor>
#include <QComboBox>
#include <QSignalSpy>
#include <QImage>
#include <QMouseEvent>
#include <QPointF>
#include <QString>
#include <QMenu>
#include <QMenuBar>
#include <QStringList>
#include <QToolBox>
#include <QLabel>
#include <QLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

#include <cmath>
#include <filesystem>
#include <memory>
#include <cstdio>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "vision/geometry_features.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QTableWidget>

#include "inspection_editor/auto_measure_dialog.h"
#include "inspection_editor/piece_report.h"
#include "ui/main_window.h"
#include "ui/piece_report_dialog.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "inspection_editor/canvas/tool_icons.h"
#include "database/db.h"
#include "database/schema.h"
#include "inspection_editor/canvas/tool_palette.h"
#include "inspection_editor/editor_window.h"
#include "repositories/tool_repository.h"
#include "sample_geometries.h"

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

// ---------------------------------------------------------------------------
// Superposición del contorno detectado (A4)
// ---------------------------------------------------------------------------

namespace {

constexpr int kPlateImageSide = 400;

// Placa cuadrada con las cuatro esquinas redondeadas y un agujero: tiene rectas,
// arcos y hueco, que son las tres cosas que la superposición pinta distinto.
cv::Mat plateMask() {
    cv::Mat mask(kPlateImageSide, kPlateImageSide, CV_8UC1, cv::Scalar(0));
    const cv::Rect box(60, 60, 280, 280);
    const int radius = 40;
    cv::rectangle(mask, cv::Rect(box.x + radius, box.y, box.width - 2 * radius, box.height),
                  cv::Scalar(255), cv::FILLED);
    cv::rectangle(mask, cv::Rect(box.x, box.y + radius, box.width, box.height - 2 * radius),
                  cv::Scalar(255), cv::FILLED);
    for (const auto& c : {cv::Point(box.x + radius, box.y + radius),
                          cv::Point(box.x + box.width - radius, box.y + radius),
                          cv::Point(box.x + radius, box.y + box.height - radius),
                          cv::Point(box.x + box.width - radius, box.y + box.height - radius)}) {
        cv::circle(mask, c, radius, cv::Scalar(255), cv::FILLED);
    }
    cv::circle(mask, {200, 200}, 45, cv::Scalar(0), cv::FILLED);
    return mask;
}

QImage sceneFromMask(const cv::Mat& mask) {
    QImage image(mask.cols, mask.rows, QImage::Format_RGB888);
    for (int y = 0; y < mask.rows; ++y) {
        for (int x = 0; x < mask.cols; ++x) {
            const int v = mask.at<uchar>(y, x) != 0 ? 200 : 25;
            image.setPixelColor(x, y, QColor(v, v, v));
        }
    }
    return image;
}

// Píxeles parecidos a un color, con holgura para el suavizado de bordes.
int countNear(const QImage& image, QColor target, int tolerance = 45) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (std::abs(c.red() - target.red()) <= tolerance &&
                std::abs(c.green() - target.green()) <= tolerance &&
                std::abs(c.blue() - target.blue()) <= tolerance) {
                ++count;
            }
        }
    }
    return count;
}

QImage renderOf(EditorCanvas& canvas) {
    QImage out(canvas.size(), QImage::Format_RGB888);
    out.fill(Qt::black);
    canvas.render(&out);
    return out;
}

const QColor kLineColor(90, 180, 255);
const QColor kArcColor(255, 165, 40);
const QColor kHoleColor(230, 110, 230);

}  // namespace

TEST(ContourOverlay, DrawsStraightsArcsAndHolesInDifferentColours) {
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    const cv::Mat mask = plateMask();
    canvas.setScene(sceneFromMask(mask), pci::vision::Fixture{});

    // Sin la capa encendida, la imagen no lleva ninguno de esos colores: la
    // escena es gris. Si ya los llevara, el resto del test no probaría nada.
    const QImage before = renderOf(canvas);
    EXPECT_EQ(countNear(before, kArcColor), 0);
    EXPECT_EQ(countNear(before, kLineColor), 0);
    EXPECT_EQ(countNear(before, kHoleColor), 0);

    const auto report = pci::vision::describeContour(mask);
    ASSERT_TRUE(report.valid);
    canvas.setContourReport(true, report);
    EXPECT_TRUE(canvas.contourReportVisible());

    const QImage after = renderOf(canvas);
    const int lines = countNear(after, kLineColor);
    const int arcs = countNear(after, kArcColor);
    const int holes = countNear(after, kHoleColor);
    std::printf("  píxeles pintados: %d rectas, %d arcos, %d agujero\n", lines, arcs, holes);
    EXPECT_GT(lines, 200) << "los cuatro lados rectos tienen que verse";
    EXPECT_GT(arcs, 100) << "los cuatro redondeos tienen que verse, y en otro color";
    EXPECT_GT(holes, 100) << "el agujero tiene que verse";

    // Y se apaga entera: es una capa de consulta, no una marca permanente.
    canvas.setContourReport(false);
    EXPECT_FALSE(canvas.contourReportVisible());
    const QImage off = renderOf(canvas);
    EXPECT_EQ(countNear(off, kArcColor), 0);
    EXPECT_EQ(countNear(off, kLineColor), 0);
}

TEST(ContourOverlay, TheSummarySaysWhatWasMeasuredAndInWhichUnit) {
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    const cv::Mat mask = plateMask();
    canvas.setScene(sceneFromMask(mask), pci::vision::Fixture{});
    canvas.setContourReport(true, pci::vision::describeContour(mask));

    const QStringList px = canvas.contourSummaryLines();
    ASSERT_EQ(px.size(), 5);
    EXPECT_TRUE(px.join(QChar('\n')).contains(QStringLiteral("px²")))
        << px.join(QChar('\n')).toStdString();
    EXPECT_TRUE(px.at(2).contains(QStringLiteral("1"))) << px.at(2).toStdString();

    // Con calibración pasa a mm, y el ÁREA con el cuadrado de la escala: a 0,5
    // mm/px el área en mm² es la cuarta parte del número en px².
    canvas.setMmPerPixel(0.5);
    const QStringList mm = canvas.contourSummaryLines();
    ASSERT_EQ(mm.size(), 5);
    const QString areaPx = px.at(1);
    const QString areaMm = mm.at(1);
    EXPECT_TRUE(areaMm.contains(QStringLiteral("mm²"))) << areaMm.toStdString();
    const double valuePx = areaPx.split(QChar(' ')).at(1).toDouble();
    const double valueMm = areaMm.split(QChar(' ')).at(1).toDouble();
    std::printf("  área: %.0f px² -> %.1f mm²\n", valuePx, valueMm);
    EXPECT_NEAR(valueMm, valuePx * 0.25, valuePx * 0.25 * 0.01);
}

TEST(ContourOverlay, AnInvalidReportDoesNotTurnTheLayerOn) {
    // Pedir ver el contorno de una imagen sin pieza no puede dejar el
    // conmutador encendido enseñando nada.
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
    canvas.setContourReport(true, pci::vision::ContourReport{});
    EXPECT_FALSE(canvas.contourReportVisible());
    EXPECT_TRUE(canvas.contourSummaryLines().isEmpty());
}

TEST(ToolCoherence, EveryToolIsDrawnWithItsOwnIcon) {
    // Los iconos se dibujan en código. Uno vacío deja un botón en blanco y dos
    // iguales hacen indistinguibles dos herramientas en una fila de catorce.
    std::vector<QImage> seen;
    for (const ToolType type : allToolTypes()) {
        const QIcon icon = toolIcon(type);
        ASSERT_FALSE(icon.isNull()) << toolTypeLabel(type) << " no tiene icono";
        const QImage image = icon.pixmap(24, 24).toImage().convertToFormat(
            QImage::Format_RGBA8888);
        ASSERT_FALSE(image.isNull()) << toolTypeLabel(type);

        int painted = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y).alpha() > 0) {
                    ++painted;
                }
            }
        }
        EXPECT_GT(painted, 8) << toolTypeLabel(type) << ": el icono está en blanco";

        for (std::size_t i = 0; i < seen.size(); ++i) {
            EXPECT_NE(image, seen[i])
                << toolTypeLabel(type) << " comparte icono con otra herramienta";
        }
        seen.push_back(image);
    }
    EXPECT_EQ(seen.size(), allToolTypes().size());
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// ---------------------------------------------------------------------------
// Paleta agrupada por familias (R2)
// ---------------------------------------------------------------------------

TEST(ToolPaletteTest, EveryToolIsReachable) {
    // Agrupar no puede esconder nada: si una herramienta no aparece en ninguna
    // familia, deja de existir para el operador aunque el codigo la tenga.
    //
    // Hay que ABRIR cada familia, porque solo se instancian los botones de la
    // activa. Eso es a proposito —crear 32 botones para enseñar 8 seria trabajo
    // tirado en cada cambio— y obliga al test a recorrerlas, que es exactamente
    // lo que hace el operador.
    ToolPalette palette;
    std::vector<ToolType> reachable;

    for (const auto category : allToolCategories()) {
        if (toolsInCategory(category).empty()) {
            continue;
        }
        palette.activateCategory(category);
        for (auto* button : palette.findChildren<QToolButton*>()) {
            for (const ToolType type : allToolTypes()) {
                // Los botones de la rejilla son de solo icono: el nombre vive
                // en el tooltip, y ahi es donde el operador lo lee.
                if (button->toolTip().startsWith(QString::fromUtf8(toolTypeLabel(type)))) {
                    reachable.push_back(type);
                }
            }
        }
    }

    std::sort(reachable.begin(), reachable.end());
    reachable.erase(std::unique(reachable.begin(), reachable.end()), reachable.end());
    auto expected = std::vector<ToolType>(allToolTypes().begin(), allToolTypes().end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(reachable, expected) << "alguna herramienta quedo escondida";
}

TEST(ToolPaletteTest, FamilyPlusDigitPicksTheRightTool) {
    // El atajo que sustituye a la tabla escrita a mano de un dígito por
    // herramienta, que se quedó corta con catorce.
    ToolPalette palette;
    std::vector<std::optional<ToolType>> chosen;
    QObject::connect(&palette, &ToolPalette::toolChosen,
                     [&chosen](std::optional<ToolType> type) { chosen.push_back(type); });

    palette.activateCategory(ToolCategory::TurnedAndExtremes);
    const auto turned = toolsInCategory(ToolCategory::TurnedAndExtremes);
    ASSERT_FALSE(turned.empty());
    ASSERT_TRUE(palette.currentTool().has_value());
    EXPECT_EQ(*palette.currentTool(), turned.front())
        << "elegir familia elige su primera herramienta: se viene a dibujar";

    // Y el dígito escoge dentro de la familia activa.
    ASSERT_TRUE(palette.activateInCurrentCategory(2));
    EXPECT_EQ(*palette.currentTool(), turned.at(2));
    // Un dígito que esa familia no tiene no hace nada, en vez de saltar a otra.
    EXPECT_FALSE(palette.activateInCurrentCategory(8));
    EXPECT_EQ(*palette.currentTool(), turned.at(2));

    // Arco, Eje, Rosca y Engranaje NO tenían tecla antes: los dígitos 1-9 y 0
    // estaban agotados con las diez primeras herramientas.
    palette.activateCategory(ToolCategory::InLine);
    ASSERT_TRUE(palette.activateInCurrentCategory(
        static_cast<int>(toolsInCategory(ToolCategory::InLine).size()) - 1));
    EXPECT_EQ(*palette.currentTool(), toolsInCategory(ToolCategory::InLine).back());
}

TEST(ToolPaletteTest, ChoosingATooolAnnouncesItAndShowingDoesNot) {
    // `activate` es "el operador pulsó" y avisa; `showSelection` es sincronizar
    // desde fuera y no debe disparar nada, o se realimentaría en bucle.
    ToolPalette palette;
    int announced = 0;
    QObject::connect(&palette, &ToolPalette::toolChosen,
                     [&announced](std::optional<ToolType>) { ++announced; });

    palette.activate(ToolType::Caliper);
    EXPECT_EQ(announced, 1);
    EXPECT_EQ(palette.currentTool(), std::optional<ToolType>(ToolType::Caliper));

    palette.showSelection(ToolType::Gear);
    EXPECT_EQ(announced, 1) << "sincronizar desde fuera no puede avisar";
    EXPECT_EQ(palette.currentTool(), std::optional<ToolType>(ToolType::Gear));

    palette.activate(std::nullopt);
    EXPECT_EQ(announced, 2);
    EXPECT_FALSE(palette.currentTool().has_value());
}

// ---------------------------------------------------------------------------
// Ninguna herramienta muda (R3)
// ---------------------------------------------------------------------------

TEST(ToolCoherence, EveryToolActuallyGetsPaintedOnTheCanvas) {
    // Este barrido nació de un fallo real: Eje, Rosca y Engranaje llevaban
    // desde su entrega SIN rama en `paintTool`. Se veían solo cuando ya habían
    // medido; antes de eso eran invisibles, y nada lo dijo. Ahora la cadena
    // termina en un `static_assert`, pero el barrido comprueba lo que el
    // compilador no puede: que la rama de verdad pinte algo.
    for (const ToolType type : allToolTypes()) {
        EditorCanvas canvas;
        canvas.resize(kWidgetWidth, kWidgetHeight);
        QImage scene(kImageWidth, kImageHeight, QImage::Format_RGB888);
        scene.fill(QColor(20, 20, 20));
        canvas.setScene(scene, pci::vision::Fixture{});

        QImage before(canvas.size(), QImage::Format_RGB888);
        before.fill(Qt::black);
        canvas.render(&before);

        std::vector<EditedTool> tools(1);
        tools[0].config.id = 1;
        tools[0].config.type = type;
        tools[0].geometry = pci::inspection::testing_support::sampleGeometry(type);
        // La geometría de ejemplo está centrada en el origen; se lleva al medio
        // del frame para que caiga dentro de la vista.
        translateGeometry(tools[0].geometry, {kImageWidth / 2.0F, kImageHeight / 2.0F});
        canvas.setTools(&tools);

        QImage after(canvas.size(), QImage::Format_RGB888);
        after.fill(Qt::black);
        canvas.render(&after);

        int changed = 0;
        for (int y = 0; y < after.height(); ++y) {
            for (int x = 0; x < after.width(); ++x) {
                if (after.pixel(x, y) != before.pixel(x, y)) {
                    ++changed;
                }
            }
        }
        std::printf("  %-16s %5d px pintados\n", toolTypeLabel(type), changed);
        EXPECT_GT(changed, 30) << toolTypeLabel(type) << " no se dibuja en el lienzo";
    }
}

TEST(DependencyArrows, AReferenceIsDrawnAndABrokenOneIsNot) {
    // Sin la flecha, en el lienzo se ve una recta construida y las rectas de
    // las que sale, sin nada que las relacione: borrar la equivocada rompe la
    // medida y nada lo habría avisado.
    const auto renderWith = [](const std::string& reference) {
        EditorCanvas canvas;
        canvas.resize(kWidgetWidth, kWidgetHeight);
        QImage scene(kImageWidth, kImageHeight, QImage::Format_RGB888);
        scene.fill(QColor(20, 20, 20));
        canvas.setScene(scene, pci::vision::Fixture{});

        std::vector<EditedTool> tools(2);
        tools[0].config.id = 1;
        tools[0].config.name = "cara A";
        tools[0].config.type = ToolType::Ruler;
        tools[0].geometry = RulerGeometry{{80.0F, 80.0F}, {240.0F, 80.0F}};
        tools[1].config.id = 2;
        tools[1].config.name = "paralela";
        tools[1].config.type = ToolType::ConstructedLine;
        tools[1].config.reference = reference;
        tools[1].geometry =
            ConstructedLineGeometry{LineConstruction::ParallelThrough, {160.0F, 260.0F}};
        canvas.setTools(&tools);

        QImage shot(canvas.size(), QImage::Format_RGB888);
        shot.fill(Qt::black);
        canvas.render(&shot);
        return shot;
    };

    const QImage without = renderWith("");
    const QImage with = renderWith("cara A");
    const QImage broken = renderWith("una que no existe");

    const auto differences = [](const QImage& a, const QImage& b) {
        int changed = 0;
        for (int y = 0; y < a.height(); ++y) {
            for (int x = 0; x < a.width(); ++x) {
                if (a.pixel(x, y) != b.pixel(x, y)) {
                    ++changed;
                }
            }
        }
        return changed;
    };

    const int drawnByTheArrow = differences(with, without);
    std::printf("  la flecha de dependencia pinta %d px\n", drawnByTheArrow);
    EXPECT_GT(drawnByTheArrow, 50) << "declarar una referencia no dibuja nada";

    // Una referencia rota no inventa ninguna flecha: no hay a dónde llevarla, y
    // el motivo se lo dirá la medición. Dibujar una flecha hacia la nada haría
    // creer que el datum existe.
    EXPECT_EQ(differences(broken, without), 0)
        << "una referencia que no existe no puede dibujar una flecha";
}

// ---------------------------------------------------------------------------
// Iconos de familia (P1)
// ---------------------------------------------------------------------------

namespace {

// Fraccion de pixeles con tinta de un icono renderizado a `size`.
double inkFraction(const QIcon& icon, int size) {
    const QImage image = icon.pixmap(size, size).toImage().convertToFormat(
        QImage::Format_ARGB32);
    int inked = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 40) {
                ++inked;
            }
        }
    }
    const int total = image.width() * image.height();
    return total > 0 ? static_cast<double>(inked) / total : 0.0;
}

// Cuanto se diferencian dos iconos: fraccion de pixeles en los que uno tiene
// tinta y el otro no. 0 = identicos.
double differenceFraction(const QIcon& a, const QIcon& b, int size) {
    const QImage left = a.pixmap(size, size).toImage().convertToFormat(
        QImage::Format_ARGB32);
    const QImage right = b.pixmap(size, size).toImage().convertToFormat(
        QImage::Format_ARGB32);
    if (left.size() != right.size()) {
        return 1.0;
    }
    int differing = 0;
    for (int y = 0; y < left.height(); ++y) {
        for (int x = 0; x < left.width(); ++x) {
            const bool inkLeft = qAlpha(left.pixel(x, y)) > 40;
            const bool inkRight = qAlpha(right.pixel(x, y)) > 40;
            if (inkLeft != inkRight) {
                ++differing;
            }
        }
    }
    const int total = left.width() * left.height();
    return total > 0 ? static_cast<double>(differing) / total : 0.0;
}

}  // namespace

TEST(CategoryIcons, EveryFamilyHasAnIconWithInkAtEverySizeItIsUsed) {
    // Un icono vacio pasa desapercibido hasta que alguien mira la franja, y una
    // mancha tampoco dice nada. La banda es ancha a proposito: lo que se caza es
    // "no se dibujo" o "se dibujo del tamaño equivocado", no un matiz de estilo.
    for (const auto category : allToolCategories()) {
        for (const int size : {20, 24, 32}) {
            const double ink = inkFraction(categoryIcon(category), size);
            EXPECT_GT(ink, 0.02) << categoryLabel(category) << " a " << size << " px";
            EXPECT_LT(ink, 0.60) << categoryLabel(category) << " a " << size << " px";
        }
    }
}

TEST(CategoryIcons, NoTwoFamiliesLookAlike) {
    // Cinco pastillas indistinguibles serian peor que cinco palabras: el
    // operador tendria que leer el tooltip cada vez, o sea que la franja de
    // iconos habria empeorado lo que venia a mejorar.
    const auto categories = allToolCategories();
    double worst = 1.0;
    std::string worstPair;
    for (std::size_t i = 0; i < categories.size(); ++i) {
        for (std::size_t j = i + 1; j < categories.size(); ++j) {
            const double difference =
                differenceFraction(categoryIcon(categories[i]), categoryIcon(categories[j]), 24);
            if (difference < worst) {
                worst = difference;
                worstPair = std::string(categoryLabel(categories[i])) + " vs " +
                            categoryLabel(categories[j]);
            }
        }
    }
    std::printf("  el par mas parecido: %s (%.1f %% de pixeles distintos)\n",
                worstPair.c_str(), worst * 100.0);
    // Umbral medido primero y fijado despues: el par mas parecido difiere un
    // 19 %, asi que exigir un 12 % deja margen de sobra para el antialiasing y
    // sigue cazando de verdad un icono que se acerque a otro. Un umbral del 6 %
    // habria pasado con dos iconos casi iguales.
    EXPECT_GT(worst, 0.12) << "dos familias se parecen demasiado: " << worstPair;
}

TEST(CategoryIcons, TheyDoNotDependOnTheToolIcons) {
    // Un icono de familia que sea igual que el de una de sus herramientas
    // confundiria las dos filas del panel, que es justo lo que la franja viene
    // a separar.
    for (const auto category : allToolCategories()) {
        for (const auto type : toolsInCategory(category)) {
            const double difference =
                differenceFraction(categoryIcon(category), toolIcon(type), 24);
            EXPECT_GT(difference, 0.04)
                << categoryLabel(category) << " se parece a " << toolTypeLabel(type);
        }
    }
}

// ---------------------------------------------------------------------------
// El panel: franja, titulo y rejilla (P2)
// ---------------------------------------------------------------------------

TEST(ToolPanel, ItFitsWithoutHorizontalScrollAtEveryUsefulWidth) {
    // El panel va a vivir en una columna estrecha (el editor) y en un dock
    // (la ventana principal). Entre 180 y 400 px tiene que caber sin barra
    // horizontal: una paleta que hay que desplazar de lado esconde herramientas
    // igual que un menu.
    // Lo que decide si aparece barra horizontal es el ancho MINIMO, no el
    // preferido: un widget se deja estrechar hasta el minimo sin quejarse.
    ToolPalette palette;
    // Hay que mostrarlo: Qt APLAZA el evento de redimensionado en un widget que
    // nunca se ha mostrado, asi que sin esto la rejilla se quedaria con las
    // columnas del ancho inicial y el test mediria un panel que no existe.
    palette.show();
    for (const int width : {180, 220, 260, 300, 400}) {
        palette.resize(width, 600);
        // Se imprime porque documenta el reflujo. Y se exige que el ancho real
        // siga al pedido: si no lo siguiera, seria que el minimo del layout lo
        // esta bloqueando — que es exactamente el fallo que tuvo esto.
        std::printf("  %3d px -> minimo %3d, %d columnas\n", palette.width(),
                    palette.minimumSizeHint().width(), ToolPalette::gridColumnsFor(width));
        EXPECT_EQ(palette.width(), width)
            << "Qt no dejo estrechar el panel: algo esta imponiendo un minimo";
        EXPECT_LE(palette.minimumSizeHint().width(), width)
            << "el panel no cabe en " << width << " px";
        EXPECT_GE(ToolPalette::gridColumnsFor(width), 1);
    }
    // Y a 180 px caben al menos cuatro botones por fila: con menos, la familia
    // mas grande se convertiria en una columna larguisima.
    EXPECT_GE(ToolPalette::gridColumnsFor(180), 4) << ToolPalette::gridColumnsFor(180);
}

TEST(ToolPanel, TheBiggestFamilyStillFitsWhenItDoublesInSize) {
    // La pregunta que hay que poder responder HOY: ¿cabra la familia mas grande
    // cuando tenga el doble de herramientas? Esperar a tenerlas para
    // averiguarlo es tarde, asi que se le pregunta a la aritmetica de la
    // rejilla en vez de al widget.
    std::size_t biggest = 0;
    for (const auto category : allToolCategories()) {
        biggest = std::max(biggest, toolsInCategory(category).size());
    }
    ASSERT_GT(biggest, 0U);

    const int doubled = static_cast<int>(biggest) * 2;
    const int height = ToolPalette::gridHeightFor(doubled, 220);
    std::printf("  la familia mayor tiene %zu; con %d caben en %d px de alto a 220 de ancho\n",
                biggest, doubled, height);
    EXPECT_LT(height, 520) << "la familia mas grande no cabria al doblarse";
}

TEST(ToolPanel, OpeningAFamilyDoesNotChangeWhatYouAreDrawing) {
    // Dos gestos distintos y tienen que seguir siendolo: pulsar una familia la
    // ABRE para mirarla, y el atajo SI elige —quien pulsa un atajo quiere
    // dibujar ya—. Si abrir un cajon cambiara la herramienta activa, mirar
    // saldria caro.
    ToolPalette palette;
    palette.activateCategory(ToolCategory::InLine);
    const auto drawing = palette.currentTool();
    ASSERT_TRUE(drawing.has_value());

    // Se pulsa el boton de otra familia, como haria el raton.
    for (auto* button : palette.findChildren<QToolButton*>()) {
        if (button->toolTip() ==
            QString::fromUtf8(categoryDescription(ToolCategory::TurnedAndExtremes))) {
            button->click();
            break;
        }
    }
    EXPECT_EQ(palette.currentCategory(), ToolCategory::TurnedAndExtremes)
        << "pulsar la familia no la abrio";
    EXPECT_EQ(palette.currentTool(), drawing)
        << "abrir una familia cambio la herramienta con la que se estaba dibujando";
}

TEST(ToolPanel, TheShortcutOpensTheFamilyItPicksFrom) {
    // Si el atajo eligiera una herramienta sin abrir su familia, el operador
    // veria una rejilla que no contiene lo que esta dibujando — y dejaria de
    // fiarse de las dos cosas.
    ToolPalette palette;
    for (const auto category : allToolCategories()) {
        if (toolsInCategory(category).empty()) {
            continue;
        }
        palette.activateCategory(category);
        EXPECT_EQ(palette.currentCategory(), category);
        ASSERT_TRUE(palette.currentTool().has_value());
        EXPECT_EQ(categoryOf(*palette.currentTool()), category);
        // Y el titulo dice la misma familia que la franja.
        bool titled = false;
        for (auto* title : palette.findChildren<QLabel*>()) {
            titled = titled || title->text() == QString::fromUtf8(categoryLabel(category));
        }
        EXPECT_TRUE(titled) << "el titulo no dice " << categoryLabel(category);
    }
}

// ---------------------------------------------------------------------------
// La linea de ayuda (P3)
// ---------------------------------------------------------------------------

namespace {

// Los textos de la linea de ayuda, de arriba abajo. Se buscan por contenido y
// no por posicion: un test que hay que reparar cada vez que se añade una
// etiqueta no protege nada.
QStringList panelLabels(const ToolPalette& palette) {
    QStringList texts;
    for (auto* label : palette.findChildren<QLabel*>()) {
        texts << label->text();
    }
    return texts;
}

QToolButton* buttonFor(const ToolPalette& palette, ToolType type) {
    for (auto* button : palette.findChildren<QToolButton*>()) {
        if (button->toolTip().startsWith(QString::fromUtf8(toolTypeLabel(type)))) {
            return button;
        }
    }
    return nullptr;
}

void hover(QWidget* widget, bool entering) {
    QEvent event(entering ? QEvent::Enter : QEvent::Leave);
    QApplication::sendEvent(widget, &event);
}

}  // namespace

TEST(ToolHelpLine, ItNamesWhatTheMouseIsOverAndGoesBackWhenItLeaves) {
    // Los botones de la rejilla no tienen texto: sin esto, el panel seria mas
    // bonito y PEOR, porque habria que adivinar cada icono o esperar el
    // tooltip. La linea es lo que repone lo que la rejilla quita.
    ToolPalette palette;
    palette.show();
    palette.activateCategory(ToolCategory::InLine);
    const auto selected = palette.currentTool();
    ASSERT_TRUE(selected.has_value());

    const auto tools = toolsInCategory(ToolCategory::InLine);
    ASSERT_GE(tools.size(), 2U);
    const ToolType other = tools[1];
    ASSERT_NE(other, *selected);

    auto* button = buttonFor(palette, other);
    ASSERT_NE(button, nullptr);

    hover(button, true);
    EXPECT_TRUE(panelLabels(palette).contains(QString::fromUtf8(toolTypeLabel(other))))
        << "la linea no dice lo que el raton esta señalando";

    hover(button, false);
    EXPECT_TRUE(panelLabels(palette).contains(QString::fromUtf8(toolTypeLabel(*selected))))
        << "al salir el raton, la linea deberia volver a la seleccionada";
}

TEST(ToolHelpLine, WithNothingChosenItSaysWhereToStart) {
    // El primer momento es justo cuando mas falta hace decir algo, y es cuando
    // una linea de ayuda mal pensada se queda en blanco.
    ToolPalette palette;
    palette.show();
    ASSERT_FALSE(palette.currentTool().has_value());

    bool saysSomething = false;
    for (const QString& text : panelLabels(palette)) {
        saysSomething = saysSomething || text.contains(QStringLiteral("familia"),
                                                       Qt::CaseInsensitive);
    }
    EXPECT_TRUE(saysSomething) << "sin nada elegido la linea no dice por donde empezar";
}

TEST(ToolHelpLine, TheExplanationComesFromTheToolItselfAndIsNotACopy) {
    // Escrito dos veces acabaria divergiendo, que es la razon por la que la
    // paleta se compartio en su dia. Se comprueba que lo mostrado SALE de
    // `toolTypeDescription`, no que se le parezca.
    ToolPalette palette;
    palette.show();
    for (const auto category : allToolCategories()) {
        const auto tools = toolsInCategory(category);
        if (tools.empty()) {
            continue;
        }
        palette.activateCategory(category);
        const ToolType type = tools.front();
        const QString full = QString::fromUtf8(toolTypeDescription(type));

        bool derived = false;
        for (const QString& text : panelLabels(palette)) {
            derived = derived || (!text.isEmpty() && full.startsWith(text));
        }
        EXPECT_TRUE(derived) << toolTypeLabel(type)
                             << ": lo que se enseña no sale de su descripcion";
    }
}

TEST(ToolHelpLine, TheShortcutShownIsTheOneThatWorks) {
    // Un atajo mal anunciado es peor que ninguno: el operador lo prueba, no
    // pasa nada, y deja de fiarse de los que si funcionan. Se comprueba
    // ejecutando lo que la linea dice.
    ToolPalette palette;
    palette.show();

    int familyNumber = 0;
    for (const auto category : allToolCategories()) {
        const auto tools = toolsInCategory(category);
        if (tools.empty()) {
            continue;
        }
        ++familyNumber;
        for (std::size_t i = 0; i < tools.size() && i < 9U; ++i) {
            const QString hint = ToolPalette::shortcutHint(tools[i]);
            EXPECT_TRUE(hint.contains(QString::number(familyNumber)))
                << toolTypeLabel(tools[i]) << " -> " << hint.toStdString();

            // Y lo que dice se cumple: esa familia y esa posicion dan esa
            // herramienta.
            palette.activateCategory(category);
            ASSERT_TRUE(palette.activateInCurrentCategory(static_cast<int>(i)));
            EXPECT_EQ(palette.currentTool(), tools[i])
                << "el atajo anunciado (" << hint.toStdString() << ") elige otra cosa";
        }
    }
}

TEST(ToolHelpLine, TheGridDoesNotMoveUnderTheCursor) {
    // Lo que hay que garantizar es que la REJILLA no se mueva: si botara bajo el
    // cursor, elegir se volvería un juego de puntería.
    //
    // La versión anterior de este test medía un proxy —que la etiqueta de ayuda
    // no cambiara de alto— y eso ataba el diseño a una solución concreta: alto
    // fijo, y por tanto texto recortado. Ahora la ayuda se desplaza dentro de un
    // área cuyo alto lo fija el panel, así que la etiqueta de dentro sí crece y
    // la rejilla sigue quieta. Se comprueba la rejilla, que es lo que importa.
    ToolPalette palette;
    palette.show();
    palette.resize(220, 600);
    palette.activateCategory(ToolCategory::TurnedAndExtremes);

    QWidget* firstButton = buttonFor(palette, toolsInCategory(ToolCategory::TurnedAndExtremes).front());
    ASSERT_NE(firstButton, nullptr);
    const QPoint anchor = firstButton->mapTo(&palette, QPoint(0, 0));

    for (const auto type : toolsInCategory(ToolCategory::TurnedAndExtremes)) {
        auto* button = buttonFor(palette, type);
        ASSERT_NE(button, nullptr);
        hover(button, true);
        EXPECT_EQ(firstButton->mapTo(&palette, QPoint(0, 0)), anchor)
            << "la rejilla se movió al pasar el ratón por " << toolTypeName(type);
        hover(button, false);
    }
}

// ---------------------------------------------------------------------------
// El editor estrena el panel (P4)
// ---------------------------------------------------------------------------

namespace {

// Un editor DE VERDAD: con su base de datos, su pieza y sus herramientas
// guardadas. Darle un repositorio falso probaria otra cosa — y lo que hay que
// comprobar es justo que el cambio de paleta no rompe el editor real.
class RealEditor {
public:
    RealEditor() {
        path_ = (std::filesystem::temp_directory_path() /
                 ("pci_p4_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                  ".db"))
                    .string();
        std::filesystem::remove(path_);
        auto opened = pci::database::Db::open(path_);
        EXPECT_TRUE(opened.isOk());
        db_ = std::move(opened.value());  // `open` ya devuelve el unique_ptr
        EXPECT_TRUE(pci::database::migrate(*db_).isOk());
        repo_ = std::make_unique<pci::repositories::ToolRepository>(*db_);

        QImage reference(640, 480, QImage::Format_RGB888);
        reference.fill(QColor(200, 200, 200));
        window_ = std::make_unique<EditorWindow>(reference, pci::vision::Fixture{},
                                                 /*pieceId=*/1, repo_.get());
    }

    ~RealEditor() {
        window_.reset();
        repo_.reset();
        db_.reset();
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] EditorWindow& window() { return *window_; }

private:
    std::string path_;
    std::unique_ptr<pci::database::Db> db_;
    std::unique_ptr<pci::repositories::ToolRepository> repo_;
    std::unique_ptr<EditorWindow> window_;
};

}  // namespace

TEST(EditorPanel, TheEditorOpensWithThePanelAndReachesEveryTool) {
    // El editor pasa del acordeon al panel. Lo que hay que comprobar no es que
    // compile: es que la columna sigue dando acceso a las 32 herramientas y que
    // elegir una llega hasta el lienzo, que es para lo que sirve la paleta.
    RealEditor editor;
    auto* palette = editor.window().findChild<ToolPalette*>();
    ASSERT_NE(palette, nullptr) << "el editor se quedo sin paleta";

    editor.window().show();
    editor.window().resize(1100, 700);

    std::vector<ToolType> reached;
    for (const auto category : allToolCategories()) {
        const auto tools = toolsInCategory(category);
        if (tools.empty()) {
            continue;
        }
        palette->activateCategory(category);
        // Una de cada familia, elegida como lo haria el raton sobre la rejilla.
        for (auto* button : palette->findChildren<QToolButton*>()) {
            if (button->toolTip().startsWith(QString::fromUtf8(toolTypeLabel(tools.front())))) {
                button->click();
                break;
            }
        }
        ASSERT_TRUE(palette->currentTool().has_value()) << categoryLabel(category);
        EXPECT_EQ(*palette->currentTool(), tools.front())
            << "elegir en la rejilla de " << categoryLabel(category) << " no llego";
        reached.push_back(*palette->currentTool());
    }
    EXPECT_EQ(reached.size(), allToolCategories().size());
}

TEST(EditorPanel, TheColumnIsNarrowerThanTheAccordionItReplaces) {
    // El acordeon pedia 190 px por su texto vertical. Si el panel pidiera mas,
    // el cambio habria empeorado justo lo que venia a mejorar.
    RealEditor editor;
    auto* palette = editor.window().findChild<ToolPalette*>();
    ASSERT_NE(palette, nullptr);
    editor.window().show();

    const int minimum = palette->minimumSizeHint().width();
    std::printf("  el panel del editor pide %d px (el acordeon pedia 190)\n", minimum);
    EXPECT_LE(minimum, 190) << "el panel es mas ancho que el acordeon al que sustituye";
}

// ---------------------------------------------------------------------------
// Que se vea bien de verdad, y por tanto medible (P7)
// ---------------------------------------------------------------------------

namespace {

QImage renderWidget(QWidget* widget) {
    QImage shot(widget->size(), QImage::Format_ARGB32);
    shot.fill(Qt::transparent);
    widget->render(&shot);
    return shot;
}

// Fraccion de pixeles distintos entre dos capturas. Es la forma directa de
// preguntar "¿se nota?" sin describir COMO deberia notarse, que dejaria el test
// atado a un estilo concreto.
double pixelsChanged(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) {
        return 1.0;
    }
    int different = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                ++different;
            }
        }
    }
    const int total = a.width() * a.height();
    return total > 0 ? static_cast<double>(different) / total : 0.0;
}

}  // namespace

TEST(ToolPanelLooks, TheActiveToolIsVisibleWithoutHoveringIt) {
    // Saber con que se esta dibujando no es estetica: es la diferencia entre
    // trazar la herramienta que querias y otra. Con `autoRaise` a secas, el
    // estado marcado se dibuja como un relieve tenue que en un monitor de
    // taller no se distingue de un boton cualquiera.
    //
    // El test no describe COMO tiene que verse —eso ataria el test al estilo—
    // sino que se VE: se renderiza con y sin seleccion y se compara.
    ToolPalette palette;
    palette.show();
    palette.resize(240, 400);

    palette.activate(std::nullopt);  // Mover/Elegir, ninguna herramienta marcada
    const QImage without = renderWidget(&palette);

    const auto tools = toolsInCategory(palette.currentCategory());
    ASSERT_FALSE(tools.empty());
    palette.activate(tools.front());
    const QImage with = renderWidget(&palette);

    const double changed = pixelsChanged(without, with);
    std::printf("  marcar la herramienta activa cambia el %.1f %% del panel\n",
                changed * 100.0);
    EXPECT_GT(changed, 0.005) << "la herramienta activa no se distingue de las demas";
}

TEST(ToolPanelLooks, TheGridStepIsTheSameInEveryFamily) {
    // Rejilla uniforme: si el paso cambiara de familia en familia, los botones
    // bailarian de sitio al cambiar de cajon y la memoria muscular no serviria
    // de nada.
    ToolPalette palette;
    palette.show();
    palette.resize(240, 400);

    std::vector<int> steps;
    for (const auto category : allToolCategories()) {
        const auto tools = toolsInCategory(category);
        if (tools.size() < 2) {
            continue;
        }
        palette.activateCategory(category);
        QApplication::processEvents();
        // Sin esto los botones estan todos en (0,0): la rejilla se recoloca
        // cuando corre el ciclo de eventos, y un test que mide antes mide un
        // panel sin colocar. Se deja que Qt haga lo suyo en vez de activar
        // layouts a mano, que seria el test hablando con las tripas.
        // Los dos primeros botones de la rejilla, por su posicion en pantalla.
        std::vector<QToolButton*> row;
        for (const auto type : tools) {
            for (auto* button : palette.findChildren<QToolButton*>()) {
                if (button->toolTip().startsWith(QString::fromUtf8(toolTypeLabel(type)))) {
                    row.push_back(button);
                    break;
                }
            }
        }
        ASSERT_GE(row.size(), 2U) << categoryLabel(category);
        const int step = row[1]->x() - row[0]->x();
        if (step > 0) {  // solo si los dos primeros caen en la misma fila
            steps.push_back(step);
        }
        // Y todos los botones miden lo mismo: un hit target que cambia de
        // tamaño se falla.
        for (auto* button : row) {
            EXPECT_EQ(button->size(), row.front()->size()) << categoryLabel(category);
        }
    }
    ASSERT_FALSE(steps.empty());
    for (const int step : steps) {
        EXPECT_EQ(step, steps.front()) << "el paso de la rejilla cambia entre familias";
    }
    std::printf("  paso de rejilla uniforme: %d px en %zu familias\n", steps.front(),
                steps.size());
}

TEST(ToolPanelLooks, TheShortcutLeavesTheViewTellingTheSameStory) {
    // Si el atajo elige algo que la vista no refleja, el operador deja de
    // fiarse de los dos. Se comprueba lo que el OJO ve: el boton marcado.
    ToolPalette palette;
    palette.show();
    for (const auto category : allToolCategories()) {
        const auto tools = toolsInCategory(category);
        if (tools.empty()) {
            continue;
        }
        palette.activateCategory(category);
        ASSERT_TRUE(palette.activateInCurrentCategory(0));

        QToolButton* checkedTool = nullptr;
        for (auto* button : palette.findChildren<QToolButton*>()) {
            if (button->toolTip().startsWith(QString::fromUtf8(toolTypeLabel(tools.front())))) {
                checkedTool = button;
            }
        }
        ASSERT_NE(checkedTool, nullptr) << categoryLabel(category);
        EXPECT_TRUE(checkedTool->isChecked())
            << "el atajo eligio " << toolTypeLabel(tools.front())
            << " pero su boton no aparece marcado";

        // Y la familia de la franja tambien, o la rejilla enseñaria un cajon y
        // el atajo estaria en otro.
        bool familyChecked = false;
        for (auto* button : palette.findChildren<QToolButton*>()) {
            if (button->toolTip() == QString::fromUtf8(categoryDescription(category))) {
                familyChecked = button->isChecked();
            }
        }
        EXPECT_TRUE(familyChecked) << "la franja no marca " << categoryLabel(category);
    }
}

// La ayuda enseñaba SOLO tres renglones y el resto vivía en el tooltip. Medido:
// 29 de las 32 descripciones no cabían, y la más larga tiene 901 caracteres. O
// sea, casi toda la ayuda de la aplicación solo existía al pasar el ratón — y lo
// que solo se ve con el ratón encima no lo ve quien navega con el teclado.
TEST(ToolHelpLine, TheWholeDescriptionIsOnScreenAndNotOnlyInATooltip) {
    ToolPalette palette;
    palette.show();
    palette.resize(240, 700);

    QLabel* help = nullptr;
    for (auto* label : palette.findChildren<QLabel*>()) {
        if (label->wordWrap()) {
            help = label;
        }
    }
    ASSERT_NE(help, nullptr);

    int total = 0;
    for (const auto category : allToolCategories()) {
        palette.activateCategory(category);
        for (const auto type : toolsInCategory(category)) {
            auto* button = buttonFor(palette, type);
            ASSERT_NE(button, nullptr) << toolTypeName(type);
            hover(button, true);
            ++total;

            const QString full = QString::fromUtf8(toolTypeDescription(type));
            // ENTERA, letra por letra. No «casi toda» ni «con puntos
            // suspensivos»: lo que la herramienta explica de sí misma tiene que
            // poder leerse sin ratón.
            EXPECT_EQ(help->text(), full)
                << toolTypeName(type) << ": la ayuda sigue sin estar completa";
            EXPECT_FALSE(help->text().endsWith(QStringLiteral("…")))
                << toolTypeName(type) << ": sigue recortándose";
            // Y el tooltip deja de repetirla: un globo encima del texto que se
            // está leyendo lo tapa.
            EXPECT_TRUE(help->toolTip().isEmpty())
                << toolTypeName(type) << ": el tooltip repite lo que ya está escrito";
            hover(button, false);
        }
    }
    EXPECT_EQ(total, 32) << "cambió el número de herramientas: revisa el banco";
}

namespace {

// La herramienta de descripción más larga, para probar con el peor caso en vez
// de con una cualquiera.
ToolType wordiestTool() {
    ToolType worst = ToolType::Caliper;
    std::size_t longest = 0;
    for (const auto category : allToolCategories()) {
        for (const auto type : toolsInCategory(category)) {
            const std::size_t size = QString::fromUtf8(toolTypeDescription(type)).size();
            if (size > longest) {
                longest = size;
                worst = type;
            }
        }
    }
    return worst;
}

}  // namespace

TEST(ToolHelpLine, WithRoomTheWholeTextIsVisibleWithoutScrolling) {
    // El objetivo, no el mecanismo: con sitio, la descripción entera se LEE.
    // Antes se veían tres renglones pasara lo que pasara.
    ToolPalette palette;
    palette.show();
    palette.resize(240, 900);

    auto* scroll = palette.findChild<QScrollArea*>();
    ASSERT_NE(scroll, nullptr);
    auto* help = qobject_cast<QLabel*>(scroll->widget());
    ASSERT_NE(help, nullptr);

    const ToolType worst = wordiestTool();
    palette.activateCategory(categoryOf(worst));
    auto* button = buttonFor(palette, worst);
    ASSERT_NE(button, nullptr);
    hover(button, true);
    QApplication::processEvents();

    std::printf("  [ayuda] la descripcion mas larga (%s, %d caracteres) necesita %d px y "
                "el hueco da %d px\n",
                toolTypeName(worst), static_cast<int>(help->text().size()),
                help->heightForWidth(scroll->viewport()->width()),
                scroll->viewport()->height());
    EXPECT_EQ(scroll->verticalScrollBar()->maximum(), 0)
        << "ni con 900 px de panel cabe la descripción más larga";
}

TEST(ToolHelpLine, WithoutRoomItScrollsInsteadOfCutting) {
    // Y sin sitio no se recorta: se desplaza. Un panel bajo es lo normal en una
    // pantalla de línea, y ahí es donde el corte hacía daño.
    ToolPalette palette;
    palette.show();
    palette.resize(240, 320);

    auto* scroll = palette.findChild<QScrollArea*>();
    ASSERT_NE(scroll, nullptr);
    EXPECT_TRUE(scroll->widgetResizable());
    EXPECT_EQ(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff)
        << "una barra horizontal en un texto que se ajusta solo es un texto mal ajustado";
    auto* help = qobject_cast<QLabel*>(scroll->widget());
    ASSERT_NE(help, nullptr);

    const ToolType worst = wordiestTool();
    palette.activateCategory(categoryOf(worst));
    auto* button = buttonFor(palette, worst);
    ASSERT_NE(button, nullptr);
    hover(button, true);
    QApplication::processEvents();

    auto* bar = scroll->verticalScrollBar();
    ASSERT_NE(bar, nullptr);
    std::printf("  [ayuda] con el panel a 320 px: hueco %d px, texto %d px, desplazable %d\n",
                scroll->viewport()->height(), help->height(), bar->maximum());
    EXPECT_GT(bar->maximum(), 0) << "no hay nada que desplazar: el texto se está cortando";
    // Y el texto sigue estando ENTERO: desplazarse no es recortar.
    EXPECT_EQ(help->text(), QString::fromUtf8(toolTypeDescription(worst)));

    // Cada herramienta empieza por su principio: heredar el desplazamiento de la
    // anterior deja al operador leyendo por la mitad sin saberlo.
    bar->setValue(bar->maximum());
    ASSERT_GT(bar->value(), 0);
    hover(button, false);
    palette.activateCategory(categoryOf(ToolType::Caliper));
    auto* other = buttonFor(palette, ToolType::Caliper);
    ASSERT_NE(other, nullptr);
    hover(other, true);
    QApplication::processEvents();
    EXPECT_EQ(bar->value(), 0) << "la ayuda se abrió por la mitad";
}

// ---------------------------------------------------------------------------
// Borrar: junto a Mover/Elegir, con icono, y el destructivo preguntando
// ---------------------------------------------------------------------------

TEST(ToolPaletteDelete, TheTwoDeleteButtonsLiveNextToMoveAndSayWhatTheyNeed) {
    // Borrar es la continuación del mismo gesto que elegir: se selecciona con
    // Mover/Elegir y lo siguiente que se hace con la herramienta es moverla o
    // quitarla. Tenerlo al otro extremo del panel obligaba a un viaje de ida y
    // vuelta con el ratón para el par de acciones más encadenado que hay.
    ToolPalette palette;
    palette.show();
    // El `resize` no es decorativo: Qt difiere la disposición de un widget
    // recién mostrado, así que sin esto todas las posiciones valen cero y el
    // test no estaría mirando dónde están los botones.
    palette.resize(240, 500);

    // Se buscan por su POSICIÓN respecto a Mover/Elegir, que es lo que el test
    // tiene que garantizar: que están al lado. Buscarlos por otra cosa dejaría
    // pasar que alguien los mueva de sitio.
    QToolButton* move = nullptr;
    for (auto* button : palette.findChildren<QToolButton*>()) {
        if (button->text().contains(QStringLiteral("Mover"))) {
            move = button;
        }
    }
    ASSERT_NE(move, nullptr);

    // Se señalan por su nombre —los iconos de familia también son QToolButton
    // sin texto— y lo que el test comprueba es DÓNDE están.
    std::vector<QToolButton*> besideMove;
    for (const auto* name : {"deleteTool", "deleteAllTools"}) {
        auto* button = palette.findChild<QToolButton*>(QString::fromLatin1(name));
        ASSERT_NE(button, nullptr) << name;
        // «A su lado» es que sus franjas verticales se SOLAPEN y esté a la
        // derecha, no que empiecen en el mismo píxel: el de Mover/Elegir lleva
        // texto y es más alto, así que la fila los centra a distinta altura.
        const bool sameRow = button->y() < move->y() + move->height() &&
                             move->y() < button->y() + button->height();
        EXPECT_TRUE(sameRow) << name << " no está en la fila de Mover/Elegir";
        EXPECT_GT(button->x(), move->x()) << name << " no está a su derecha";
        besideMove.push_back(button);
    }
    ASSERT_EQ(besideMove.size(), 2U);

    // Sin nada dibujado los dos están apagados, y su tooltip dice QUÉ FALTA. Un
    // botón apagado sin explicación deja pensando que la aplicación se rompió.
    palette.setDeletable(0, 0);
    for (auto* button : besideMove) {
        EXPECT_FALSE(button->isEnabled());
        EXPECT_FALSE(button->toolTip().isEmpty());
    }

    // Con herramientas dibujadas pero ninguna elegida: «borrar todas» se
    // enciende y «borrar la seleccionada» no, porque no hay seleccionada.
    palette.setDeletable(0, 7);
    int enabled = 0;
    for (auto* button : besideMove) {
        if (button->isEnabled()) {
            ++enabled;
        }
    }
    EXPECT_EQ(enabled, 1) << "sin selección solo puede estar vivo el de borrar todas";

    // Y con una elegida, los dos.
    palette.setDeletable(1, 7);
    for (auto* button : besideMove) {
        EXPECT_TRUE(button->isEnabled());
    }

    // El tooltip de «borrar todas» dice CUÁNTAS se lleva: es lo que permite
    // reconocer si es el trabajo que crees o el de otra pieza.
    bool saysHowMany = false;
    for (auto* button : besideMove) {
        if (button->toolTip().contains(QStringLiteral("7"))) {
            saysHowMany = true;
        }
    }
    EXPECT_TRUE(saysHowMany) << "ninguno dice cuántas herramientas hay";
    EXPECT_EQ(palette.deletableTotal(), 7);
}

TEST(ToolPaletteDelete, TheButtonsOnlyAskAndDoNotDeleteAnything) {
    // La paleta no borra: no sabe qué hay dibujado ni tiene la pila de deshacer.
    // Avisa, y quien manda decide — que es lo que permite que la ventana
    // principal y el editor compartan los mismos botones haciendo cosas
    // distintas por dentro.
    ToolPalette palette;
    palette.show();
    palette.resize(240, 500);
    palette.setDeletable(2, 5);

    int deleteAsked = 0;
    int deleteAllAsked = 0;
    QObject::connect(&palette, &ToolPalette::deleteRequested, [&] { ++deleteAsked; });
    QObject::connect(&palette, &ToolPalette::deleteAllRequested, [&] { ++deleteAllAsked; });

    QToolButton* move = nullptr;
    for (auto* button : palette.findChildren<QToolButton*>()) {
        if (button->text().contains(QStringLiteral("Mover"))) {
            move = button;
        }
    }
    ASSERT_NE(move, nullptr);
    for (const auto* name : {"deleteTool", "deleteAllTools"}) {
        auto* button = palette.findChild<QToolButton*>(QString::fromLatin1(name));
        ASSERT_NE(button, nullptr) << name;
        button->click();
    }
    EXPECT_EQ(deleteAsked, 1);
    EXPECT_EQ(deleteAllAsked, 1);
}

// ---------------------------------------------------------------------------
// Zona libre: el mismo modo admite el pulso y los clics
// ---------------------------------------------------------------------------

namespace {

void rightPress(QWidget* w, QPointF pos) {
    QMouseEvent e(QEvent::MouseButtonPress, pos, w->mapToGlobal(pos), Qt::RightButton,
                  Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}

void doubleClick(QWidget* w, QPointF pos) {
    QMouseEvent e(QEvent::MouseButtonDblClick, pos, w->mapToGlobal(pos), Qt::LeftButton,
                  Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}

// Banco de la zona libre: el canvas conectado como lo hace la ventana, y lo
// último que emitió guardado para poder mirarlo.
class FreeZoneGestureTest : public ::testing::Test {
protected:
    void SetUp() override {
        canvas.resize(kWidgetWidth, kWidgetHeight);
        canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
        QObject::connect(&canvas, &EditorCanvas::freeZonePicked,
                         [this](const std::vector<cv::Point>& polygon) {
                             picked = polygon;
                             ++pickedTimes;
                         });
        QObject::connect(&canvas, &EditorCanvas::freeZoneCancelled,
                         [this] { ++cancelledTimes; });
        QObject::connect(&canvas, &EditorCanvas::traceRejected,
                         [this](const QString& r) { rejection = r; });
        canvas.setFreeZonePickMode(true);
    }

    // Un punto de la imagen en coordenadas de pantalla, con la vista sin zoom.
    [[nodiscard]] static QPointF at(double x, double y) {
        return toScreen(viewAt(1.0), cv::Point2f(static_cast<float>(x),
                                                 static_cast<float>(y)));
    }

    EditorCanvas canvas;
    std::vector<cv::Point> picked;
    int pickedTimes = 0;
    int cancelledTimes = 0;
    QString rejection;
};

}  // namespace

TEST_F(FreeZoneGestureTest, DraggingTracesTheZoneFreehand) {
    // El gesto rápido: rodear la pieza sin levantar el ratón. Se traza un
    // rombo, que no es un rectángulo ni por casualidad — si el resultado
    // saliera rectangular, la zona libre no estaría siendo libre.
    const std::vector<cv::Point2f> path{{600, 200}, {900, 500}, {600, 800}, {300, 500}};
    press(&canvas, at(path.front().x, path.front().y));
    for (std::size_t i = 1; i <= path.size(); ++i) {
        const cv::Point2f from = path[i - 1];
        const cv::Point2f to = path[i % path.size()];
        for (int s = 1; s <= 40; ++s) {
            const double t = static_cast<double>(s) / 40.0;
            moveTo(&canvas, at(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t));
        }
    }
    release(&canvas, at(path.front().x, path.front().y));

    ASSERT_EQ(pickedTimes, 1) << "el trazo a pulso no se cerró como zona";
    ASSERT_GE(picked.size(), 4U);
    std::printf("  [gesto] rombo a pulso -> %zu vertices\n", picked.size());

    // Los cuatro vértices del rombo tienen que estar donde se trazaron.
    for (const auto& corner : path) {
        double best = 1e9;
        for (const auto& vertex : picked) {
            best = std::min(best, cv::norm(cv::Point2f(vertex) - corner));
        }
        EXPECT_LT(best, 12.0) << "el vértice (" << corner.x << "," << corner.y
                              << ") no sobrevivió al trazo";
    }
    // Y la zona tiene que ser un rombo de verdad: su área es la mitad de la de
    // su envolvente, no la envolvente entera.
    const double area = std::abs(cv::contourArea(picked));
    const double hull = cv::boundingRect(picked).area();
    std::printf("  [gesto] area %.0f px2 sobre una envolvente de %.0f px2 (%.0f %%)\n", area,
                hull, 100.0 * area / hull);
    EXPECT_LT(area, hull * 0.65) << "la zona salió con forma de rectángulo";

    EXPECT_FALSE(canvas.freeZonePickMode()) << "el modo tiene que apagarse solo al cerrar";
}

TEST_F(FreeZoneGestureTest, ClickingMarksCornersAndClosingOnTheFirstFinishes) {
    // El gesto exacto: cuatro clics y cierre sobre el primero. Ni uno de ellos
    // puede cerrar antes de tiempo.
    const std::vector<cv::Point2f> corners{{400, 200}, {900, 260}, {860, 700}, {380, 640}};
    for (const auto& corner : corners) {
        press(&canvas, at(corner.x, corner.y));
        release(&canvas, at(corner.x, corner.y));
        EXPECT_EQ(pickedTimes, 0) << "un clic suelto cerró la zona antes de tiempo";
    }
    // Cierre sobre el primero, con la puntería que se tiene de verdad: unos
    // píxeles al lado, no encima.
    press(&canvas, at(corners.front().x + 3, corners.front().y - 3));
    release(&canvas, at(corners.front().x + 3, corners.front().y - 3));

    ASSERT_EQ(pickedTimes, 1) << "cerrar sobre el primer vértice no terminó la zona";
    ASSERT_EQ(picked.size(), corners.size());
    for (std::size_t i = 0; i < corners.size(); ++i) {
        EXPECT_LT(cv::norm(cv::Point2f(picked[i]) - corners[i]), 3.0)
            << "el vértice " << i << " no está donde se hizo clic";
    }
    EXPECT_FALSE(canvas.freeZonePickMode());
}

TEST_F(FreeZoneGestureTest, ADoubleClickAlsoCloses) {
    // El atajo que ya espera cualquiera que haya dibujado un polígono en otro
    // programa, y la salida cuando el primer vértice queda lejos de la mano.
    for (const auto& corner : std::vector<cv::Point2f>{{300, 300}, {800, 320}, {700, 800}}) {
        press(&canvas, at(corner.x, corner.y));
        release(&canvas, at(corner.x, corner.y));
    }
    ASSERT_EQ(pickedTimes, 0);
    doubleClick(&canvas, at(700, 800));
    EXPECT_EQ(pickedTimes, 1) << "el doble clic no cerró la zona";
    EXPECT_GE(picked.size(), 3U);
}

TEST_F(FreeZoneGestureTest, RightClickUndoesAVertexAndThenCancels) {
    // La salida del gesto. Sin ella, un trazo mal empezado solo se podía
    // terminar mal.
    for (const auto& corner : std::vector<cv::Point2f>{{300, 300}, {800, 320}, {700, 800}}) {
        press(&canvas, at(corner.x, corner.y));
        release(&canvas, at(corner.x, corner.y));
    }
    rightPress(&canvas, at(500, 500));  // deshace el tercero
    rightPress(&canvas, at(500, 500));  // el segundo
    rightPress(&canvas, at(500, 500));  // el primero
    EXPECT_EQ(cancelledTimes, 0) << "cancelar antes de quedarse sin vértices";
    EXPECT_TRUE(canvas.freeZonePickMode());

    rightPress(&canvas, at(500, 500));  // ya no queda ninguno: cancela
    EXPECT_EQ(cancelledTimes, 1) << "sin vértices, el botón derecho tiene que cancelar";
    EXPECT_FALSE(canvas.freeZonePickMode());
    EXPECT_EQ(pickedTimes, 0) << "cancelar no puede dejar una zona puesta";
}

TEST_F(FreeZoneGestureTest, ATraceThatEnclosesNothingIsRejectedOutLoud) {
    // Nada se descarta en silencio: si el operador traza y no aparece nada,
    // tiene que saber por qué. Y el modo sigue encendido, porque lo que falló
    // fue el trazo y no la intención.
    press(&canvas, at(300, 400));
    for (int x = 310; x <= 900; x += 10) {
        moveTo(&canvas, at(x, 400));
    }
    for (int x = 900; x >= 300; x -= 10) {
        moveTo(&canvas, at(x, 400));
    }
    release(&canvas, at(300, 400));

    EXPECT_EQ(pickedTimes, 0) << "un trazo sin área no puede convertirse en zona";
    EXPECT_FALSE(rejection.isEmpty()) << "se descartó el trazo sin decir por qué";
    EXPECT_TRUE(canvas.freeZonePickMode())
        << "el modo se apagó: habría que volver a pulsar el botón para reintentar";
}

TEST_F(FreeZoneGestureTest, TheGestureMeansTheSameAtAnyZoom) {
    // Distinguir un clic de un trazo se decide en píxeles de PANTALLA. Al 800 %
    // de zoom, tres píxeles de mano son veinticuatro de imagen: si el umbral
    // fuera en coordenadas de imagen, el mismo gesto significaría dos cosas
    // según por dónde se estuviera mirando.
    for (int i = 0; i < 14; ++i) {
        canvas.zoomIn();
    }
    ASSERT_GT(canvas.zoomFactor(), 5.0);

    // Un temblor de 2 px de PANTALLA sigue siendo un clic, no un trazo.
    const QPointF spot = at(960, 540);
    press(&canvas, spot);
    moveTo(&canvas, spot + QPointF(2.0, 1.0));
    release(&canvas, spot + QPointF(2.0, 1.0));
    EXPECT_EQ(pickedTimes, 0);

    // Y con tres clics más y el cierre, la zona sale: el primero contó como
    // vértice, que es lo que se esperaba de él.
    const std::vector<QPointF> rest{spot + QPointF(120, 0), spot + QPointF(120, 90),
                                    spot + QPointF(0, 90)};
    for (const auto& point : rest) {
        press(&canvas, point);
        release(&canvas, point);
    }
    press(&canvas, spot + QPointF(2.0, 1.0));
    release(&canvas, spot + QPointF(2.0, 1.0));
    EXPECT_EQ(pickedTimes, 1) << "al zoom alto el gesto dejó de significar lo mismo";
}

// ---------------------------------------------------------------------------
// El informe de pieza sobre la ventana real
// ---------------------------------------------------------------------------

namespace {

cv::Mat hexagonMask(int canvas = 600, int radius = 160) {
    cv::Mat mask = cv::Mat::zeros(canvas, canvas, CV_8UC1);
    std::vector<cv::Point> vertices;
    for (int k = 0; k < 6; ++k) {
        const double angle = 2.0 * CV_PI * k / 6 - CV_PI / 2.0;
        vertices.emplace_back(
            static_cast<int>(std::lround(canvas / 2 + radius * std::cos(angle))),
            static_cast<int>(std::lround(canvas / 2 + radius * std::sin(angle))));
    }
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{vertices}, cv::Scalar(255),
                 cv::LINE_8);
    return mask;
}

pci::inspection::PieceReport hexagonReport(double mmPerPixel = 0.0) {
    const cv::Mat mask = hexagonMask();
    cv::Mat gray(mask.size(), CV_8UC1, cv::Scalar(30));
    gray.setTo(cv::Scalar(220), mask);
    return pci::inspection::measureWholePiece(gray, mask, {}, mmPerPixel);
}

// Texto de una celda, o cadena vacía si no hay nada ahí.
QString cellText(const QTableWidget* table, int row, int column) {
    const auto* item = table->item(row, column);
    return item != nullptr ? item->text() : QString();
}

}  // namespace

TEST(PieceReportDialogTest, EveryMeasurementIsOnScreenWithItsUnit) {
    // Lo que se pidió: verlo TODO. Si la tabla se quedara corta, el informe
    // estaría contestando a medias sin decirlo.
    const auto report = hexagonReport(0.25);
    ASSERT_TRUE(report.ok) << report.problem;

    pci::ui::PieceReportDialog dialog(report, QStringLiteral("una imagen"));
    dialog.resize(900, 700);
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);

    // Una fila por medida más los dos títulos de bloque.
    EXPECT_EQ(table->rowCount(), static_cast<int>(report.rows.size()) + 2);

    int withUnit = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!cellText(table, row, 2).isEmpty()) {
            ++withUnit;
        }
    }
    std::printf("  [informe] %d filas en pantalla, %d con unidad\n", table->rowCount(),
                withUnit);
    EXPECT_EQ(withUnit, static_cast<int>(report.rows.size()))
        << "alguna medida salió a pantalla sin unidad";
}

TEST(PieceReportDialogTest, TheContourFactsComeBeforeTheDimensionsAndAreSeparated) {
    // Mezclar un hecho del contorno con una cota sin distinguirlos invita a
    // buscarle tolerancia a un área que nadie ha declarado.
    const auto report = hexagonReport();
    ASSERT_TRUE(report.ok) << report.problem;
    pci::ui::PieceReportDialog dialog(report, QStringLiteral("una imagen"));
    dialog.resize(900, 700);
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);

    // Los dos títulos de bloque son las filas que ocupan las cinco columnas.
    std::vector<int> sectionRows;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->columnSpan(row, 0) == 5) {
            sectionRows.push_back(row);
        }
    }
    ASSERT_EQ(sectionRows.size(), 2U) << "faltan los títulos que separan los dos bloques";
    EXPECT_EQ(sectionRows[0], 0) << "el informe no empieza por el contorno";
    // El primer bloque tiene exactamente los hechos del contorno.
    EXPECT_EQ(sectionRows[1], static_cast<int>(report.contourFactCount()) + 1);
}

TEST(PieceReportDialogTest, MeasuringDoesNotWatchUnlessYouSaySo) {
    // Medir y vigilar son dos decisiones. Unirlas llenaría la plantilla de
    // herramientas a cada consulta.
    const auto report = hexagonReport();
    ASSERT_TRUE(report.ok) << report.problem;
    ASSERT_FALSE(report.watchable.empty());

    pci::ui::PieceReportDialog dialog(report, QStringLiteral("una imagen"));
    dialog.resize(900, 700);
    EXPECT_TRUE(dialog.toWatch().empty()) << "el informe se llevó cotas sin que nadie lo pidiera";

    // Y con el botón, se lleva exactamente las cotas: ni una de las filas del
    // contorno, que no se pueden vigilar porque no hay herramienta que las mida.
    QPushButton* watch = nullptr;
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (button->text().startsWith(QStringLiteral("Vigilar"))) {
            watch = button;
        }
    }
    ASSERT_NE(watch, nullptr);
    EXPECT_TRUE(watch->isEnabled());
    watch->click();
    EXPECT_EQ(dialog.toWatch().size(), report.watchable.size());
}

TEST(PieceReportDialogTest, ACountIsNotShownWithDecimals) {
    // «6,00 agujeros» invita a leer un recuento como una magnitud continua.
    const auto report = hexagonReport();
    ASSERT_TRUE(report.ok) << report.problem;
    pci::ui::PieceReportDialog dialog(report, QStringLiteral("una imagen"));
    dialog.resize(900, 700);
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);

    int counts = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (cellText(table, row, 2) == QStringLiteral("n")) {
            ++counts;
            EXPECT_FALSE(cellText(table, row, 1).contains('.'))
                << "el recuento de la fila «" << cellText(table, row, 0).toStdString()
                << "» salió con decimales: " << cellText(table, row, 1).toStdString();
        }
    }
    EXPECT_GT(counts, 0) << "el informe no trae ningún recuento y este test no prueba nada";
}

TEST(PieceReportDialogTest, WithoutTolerancesTheColumnSaysSoInsteadOfShowingZero) {
    // Un cero en la columna de tolerancia parece una tolerancia de cero, que es
    // la más estricta que existe. Los hechos del contorno no llevan banda.
    const auto report = hexagonReport();
    ASSERT_TRUE(report.ok) << report.problem;
    pci::ui::PieceReportDialog dialog(report, QStringLiteral("una imagen"));
    dialog.resize(900, 700);
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);

    // La primera fila de datos es un hecho del contorno (el perímetro).
    EXPECT_EQ(cellText(table, 1, 3), QString::fromUtf8("—"))
        << "un hecho del contorno salió con una tolerancia que nadie declaró";
}

// ---------------------------------------------------------------------------
// La barra de la ventana principal
// ---------------------------------------------------------------------------
//
// No tenía ni un test, y por eso fue acumulando: trece botones del mismo peso
// repartidos en tres filas, sin agrupar, con dos desplegables que se comían el
// ancho y dos botones de zona cuyos textos cambiaban de verbo según el estado.
// Lo que se fija aquí no es el aspecto sino las decisiones: una sola acción
// destacada, un solo control de zona, y los desplegables acotados.

TEST(MainToolbar, OnlyOneActionIsEmphasised) {
    pci::ui::MainWindow window;
    window.resize(1400, 800);

    int emphasised = 0;
    QString which;
    for (auto* button : window.findChildren<QPushButton*>()) {
        if (button->isDefault() || button->font().bold()) {
            ++emphasised;
            which = button->text();
        }
    }
    std::printf("  [barra] %d boton(es) destacado(s): %s\n", emphasised,
                which.toStdString().c_str());
    // Uno, y solo uno: dos o tres destacados no destacan ninguno.
    EXPECT_EQ(emphasised, 1) << "o no destaca nada, o destaca de más";
    EXPECT_EQ(which, QStringLiteral("Inspeccionar"))
        << "lo destacado no es la acción que se pulsa cien veces al día";
}

TEST(MainToolbar, TheZoneIsOneControlWithItsThreeActionsNamed) {
    pci::ui::MainWindow window;
    window.resize(1400, 800);

    // Un solo control, no dos botones cuyas etiquetas cambian de verbo.
    QToolButton* zone = nullptr;
    for (auto* button : window.findChildren<QToolButton*>()) {
        if (button->menu() != nullptr && button->text().startsWith(QStringLiteral("Zona"))) {
            zone = button;
        }
    }
    ASSERT_NE(zone, nullptr) << "no hay control de zona con menú";

    // Y ninguno de los botones viejos sobrevive: dos verbos para la misma
    // decisión es justo lo que se quitó.
    for (auto* button : window.findChildren<QPushButton*>()) {
        EXPECT_FALSE(button->text().startsWith(QStringLiteral("Quitar zona")))
            << "sigue habiendo un botón que dice lo que borra";
    }

    // El botón dice qué zona está EN USO, no cuál hay guardada. Recién abierta
    // la zona de trabajo va en automática, así que eso es lo que tiene que
    // decir — «Zona» a secas haría creer que no hay ninguna, y «Zona fija»
    // afirmaría una que nadie dibujó.
    // Recién abierta, la zona de trabajo va en IMAGEN ENTERA: la automática
    // estuvo de fábrica y hubo que revertirla porque escondía piezas. Sin zona
    // activa, el botón dice «Zona» a secas — nombrar una que no se aplica sería
    // la misma mentira que se acaba de quitar.
    EXPECT_EQ(zone->text(), QStringLiteral("Zona"))
        << "el botón dice «" << zone->text().toStdString() << "» sin zona activa";

    QStringList actions;
    for (auto* action : zone->menu()->actions()) {
        if (!action->isSeparator()) {
            actions << action->text();
        }
    }
    std::printf("  [barra] menu de zona: %s\n", actions.join(" / ").toStdString().c_str());
    EXPECT_EQ(actions.size(), 3) << "las tres acciones de la zona son dibujar dos y quitar";

    // Sin zona dibujada, «Quitar» está apagado: un «Quitar» vivo sin nada que
    // quitar enseña a desconfiar de los menús.
    QAction* clear = zone->menu()->actions().last();
    EXPECT_FALSE(clear->isEnabled()) << "«" << clear->text().toStdString()
                                     << "» está vivo sin zona que quitar";
    EXPECT_FALSE(clear->toolTip().isEmpty()) << "apagado y sin decir por qué";

    // Y las de DIBUJAR no son marcables. Una acción marcable se marca al
    // pulsarla, y pulsar «Dibujar zona» solo empieza el gesto: todavía no hay
    // zona. El menú quedaba afirmando que sí, y si el operador no llegaba a
    // arrastrar, seguía mintiendo.
    for (auto* action : zone->menu()->actions()) {
        if (action->isSeparator() || action == clear) {
            continue;
        }
        EXPECT_FALSE(action->isCheckable())
            << "«" << action->text().toStdString()
            << "» se marca al pulsarla, antes de que exista la zona";
    }
}

TEST(MainToolbar, TheDropdownsDoNotEatTheRow) {
    // Con factor de estiramiento, «Integrated Camera» ocupaba media ventana y
    // empujaba los botones contra el borde, lejos del combo al que se refieren.
    pci::ui::MainWindow window;
    window.resize(1600, 800);
    window.show();
    QApplication::processEvents();

    for (auto* combo : window.findChildren<QComboBox*>()) {
        if (combo->maximumWidth() >= QWIDGETSIZE_MAX) {
            continue;  // los de los diálogos internos no son de la barra
        }
        EXPECT_LE(combo->width(), combo->maximumWidth())
            << "un desplegable de la barra pasa de su ancho máximo";
        EXPECT_LT(combo->width(), 700)
            << "un desplegable se está comiendo la fila con una ventana de 1600 px";
    }
}

TEST(MainMenus, EveryToolbarActionIsAlsoReachableFromAMenu) {
    // Una acción que solo existe en la barra no la encuentra quien navega con
    // el teclado, y a los menús se va justo cuando no se reconoce el icono.
    pci::ui::MainWindow window;
    window.resize(1400, 800);

    QStringList inMenus;
    for (auto* menu : window.menuBar()->findChildren<QMenu*>()) {
        for (auto* action : menu->actions()) {
            if (!action->isSeparator()) {
                inMenus << action->text();
            }
        }
    }
    ASSERT_FALSE(inMenus.isEmpty());

    // Las acciones de la barra que tienen que estar también en un menú. Se
    // nombran a mano y no se barre la barra entera a propósito: «Iniciar» o
    // «Capturar foto» son del vídeo que se está viendo y no tienen sentido
    // fuera de él.
    for (const QString& needed :
         {QStringLiteral("Inspeccionar"), QStringLiteral("Auto-inspección"),
          QStringLiteral("Medir pieza"), QStringLiteral("Guardar plantilla")}) {
        bool found = false;
        for (const QString& text : inMenus) {
            if (text.startsWith(needed)) {
                found = true;
            }
        }
        EXPECT_TRUE(found) << "«" << needed.toStdString()
                           << "» solo existe en la barra";
    }
}

TEST(MainMenus, ScaleAndUnitLiveInTheSamePlace) {
    // Para preparar una medición en milímetros había que visitar DOS menús que
    // no hablan de medir: «Calibrar escala» estaba en Fuente, junto a «Buscar
    // cámaras», y «Unidad de medida» en Ver, junto a «Mostrar contorno» — como
    // si elegir milímetros o píxeles fuera cuestión de aspecto.
    pci::ui::MainWindow window;
    window.resize(1400, 800);

    QMenu* measure = nullptr;
    for (auto* menu : window.menuBar()->findChildren<QMenu*>()) {
        if (menu->title().contains(QStringLiteral("Medida"))) {
            measure = menu;
        }
    }
    ASSERT_NE(measure, nullptr) << "no hay menú de Medida";

    bool calibration = false;
    bool unit = false;
    for (auto* action : measure->actions()) {
        if (action->text().contains(QStringLiteral("Calibrar"))) {
            calibration = true;
        }
        if (action->text().contains(QStringLiteral("Unidad"))) {
            unit = true;
        }
    }
    EXPECT_TRUE(calibration) << "calibrar la escala sigue fuera del menú de Medida";
    EXPECT_TRUE(unit) << "la unidad sigue fuera del menú de Medida";

    // Y ya no están donde estaban: dejarlas en los dos sitios sería peor que
    // no moverlas — dos caminos a lo mismo que hay que mantener a la vez.
    for (auto* menu : window.menuBar()->findChildren<QMenu*>()) {
        if (menu == measure || menu->title().isEmpty()) {
            continue;
        }
        for (auto* action : menu->actions()) {
            EXPECT_FALSE(action->text().contains(QStringLiteral("Calibrar escala")))
                << "«Calibrar escala» sigue duplicada en " << menu->title().toStdString();
        }
    }
}

TEST(MainMenus, TheAutoInspectionSaysWhyItCannotStartInsteadOfOpeningAModal) {
    // Este test no se pudo escribir la primera vez: encender la auto-inspección
    // sin cámara ni pieza abría un QMessageBox MODAL, y sin pantalla eso bloquea
    // para siempre — el banco se colgó cinco minutos hasta que hubo que matar el
    // proceso. Un control que no se puede probar es un control que nadie prueba.
    //
    // Ahora está apagado con su motivo, así que se lee ANTES de pulsar y además
    // se puede comprobar.
    pci::ui::MainWindow window;
    window.resize(1400, 800);

    QPushButton* button = nullptr;
    for (auto* candidate : window.findChildren<QPushButton*>()) {
        if (candidate->text().startsWith(QStringLiteral("Auto-inspección"))) {
            button = candidate;
        }
    }
    ASSERT_NE(button, nullptr);
    // Recién abierta no hay fuente ni pieza: no se puede empezar.
    EXPECT_FALSE(button->isEnabled()) << "se ofrece empezar algo que no puede empezar";
    EXPECT_FALSE(button->toolTip().isEmpty()) << "apagado y sin decir por qué";
    EXPECT_TRUE(button->toolTip().contains(QStringLiteral("fuente")))
        << "el motivo no menciona lo que falta: " << button->toolTip().toStdString();
    std::printf("  [auto] apagada, y dice: %s\n", button->toolTip().toStdString().c_str());

    // Y el menú dice lo mismo: si uno estuviera vivo y el otro no, el operador
    // no sabría a cuál creer.
    QAction* menuAction = nullptr;
    for (auto* menu : window.menuBar()->findChildren<QMenu*>()) {
        for (auto* action : menu->actions()) {
            if (action->text().startsWith(QStringLiteral("Auto-inspección"))) {
                menuAction = action;
            }
        }
    }
    ASSERT_NE(menuAction, nullptr);
    EXPECT_EQ(menuAction->isEnabled(), button->isEnabled());
    EXPECT_EQ(menuAction->toolTip(), button->toolTip());
}

TEST(MainMenus, TheAutoInspectionMenuAndButtonStartInAgreement) {
    // Si el menú dijera una cosa y el botón otra, el operador no sabría a cuál
    // creer. Aquí solo se comprueba el estado inicial y que el espejo exista:
    // ENCENDERLO no se puede probar, y el motivo es en sí un hallazgo — sin
    // cámara ni pieza, `onAutoToggled` abre un QMessageBox modal que sin
    // pantalla bloquea para siempre. El primer intento de este test colgó el
    // banco cinco minutos hasta que hubo que matar el proceso.
    //
    // Queda apuntado: un conmutador que abre un diálogo modal para decir que no
    // se puede encender es peor que un conmutador apagado con su motivo en el
    // tooltip, que es justo lo que este proyecto ya hace en los botones de
    // borrar.
    pci::ui::MainWindow window;
    window.resize(1400, 800);

    QAction* menuAction = nullptr;
    for (auto* menu : window.menuBar()->findChildren<QMenu*>()) {
        for (auto* action : menu->actions()) {
            if (action->text().startsWith(QStringLiteral("Auto-inspección"))) {
                menuAction = action;
            }
        }
    }
    ASSERT_NE(menuAction, nullptr) << "la auto-inspección no llegó al menú";
    EXPECT_TRUE(menuAction->isCheckable()) << "en el menú tiene que verse encendida o apagada";

    QPushButton* button = nullptr;
    for (auto* candidate : window.findChildren<QPushButton*>()) {
        if (candidate->text().startsWith(QStringLiteral("Auto-inspección"))) {
            button = candidate;
        }
    }
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(menuAction->isChecked(), button->isChecked())
        << "el menú y el botón arrancan diciendo cosas distintas";
}

TEST(MainKeyboard, TheCanvasCanTakeFocusBecauseItIsWhereOneWorks) {
    // Estaba en NoFocus: era el ÚNICO sitio de la ventana al que el teclado no
    // podía llegar, y es donde se trabaja. No había forma de saber si el lienzo
    // estaba activo, y cualquier tecla que quisiera atender no le llegaría.
    pci::ui::MainWindow window;
    window.resize(1400, 800);

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    EXPECT_NE(canvas->focusPolicy(), Qt::NoFocus) << "el lienzo sigue fuera del teclado";
    // StrongFocus y no ClickFocus: a quien navega con el teclado hay que
    // dejarle llegar hasta aquí, no solo a quien usa el ratón.
    EXPECT_TRUE((canvas->focusPolicy() & Qt::TabFocus) != 0)
        << "al lienzo solo se llega con el ratón";
}

TEST(MainKeyboard, LeavingDrawingModeIsReachableWithTheKeyboard) {
    // «Mover/Elegir» es la forma de SALIR del modo de dibujo. Dejarla fuera del
    // recorrido del teclado deja atrapado dibujando a quien navega así, que es
    // lo que las guías llaman una trampa de foco.
    ToolPalette palette;
    palette.resize(260, 700);
    palette.show();

    QAbstractButton* select = nullptr;
    for (auto* button : palette.findChildren<QAbstractButton*>()) {
        if (button->text().startsWith(QStringLiteral("Mover"))) {
            select = button;
        }
    }
    ASSERT_NE(select, nullptr);
    EXPECT_NE(select->focusPolicy(), Qt::NoFocus)
        << "no se puede salir del modo de dibujo con el teclado";
}

TEST(MainKeyboard, WhatStaysOutOfTheTabOrderHasItsOwnShortcut) {
    // Los botones de la barra de zoom se quedan fuera del recorrido a propósito:
    // añadirían cuatro paradas para acciones que YA tienen atajo, y un recorrido
    // largo se abandona. Lo que este test vigila es que la excusa siga siendo
    // cierta — si alguien quita el atajo, la acción se queda sin teclado.
    pci::ui::MainWindow window;
    window.resize(1400, 800);
    window.show();
    QApplication::processEvents();

    QStringList outside;
    for (auto* button : window.findChildren<QAbstractButton*>()) {
        if (button->isVisible() && !button->text().isEmpty() &&
            button->focusPolicy() == Qt::NoFocus) {
            outside << button->text();
        }
    }
    std::printf("  [teclado] fuera del tabulador: %s\n",
                outside.join(QStringLiteral(", ")).toStdString().c_str());

    // Todos los que queden fuera tienen que ser de zoom, y el zoom tiene sus
    // cuatro atajos entre las acciones de la ventana.
    int zoomShortcuts = 0;
    for (auto* action : window.actions()) {
        if (!action->shortcut().isEmpty()) {
            ++zoomShortcuts;
        }
    }
    EXPECT_GT(zoomShortcuts, 4) << "no hay atajos que justifiquen dejar botones fuera";
    EXPECT_LE(outside.size(), 4)
        << "hay más botones sin teclado que los de zoom: " << outside.join(", ").toStdString();
}

TEST(MainToolbar, InsertingTheOpenedFileDoesNotLookLikeChoosingASource) {
    // EL BUCLE, reproducido sin abrir ningún diálogo.
    //
    // Al abrir un fichero se inserta su nombre en la posición 0 del desplegable.
    // Eso desplaza al elemento seleccionado —«Abrir imagen…»— de la posición N a
    // la N+1, y Qt emite `currentIndexChanged` porque el ÍNDICE cambió, aunque
    // el elemento elegido sea exactamente el mismo.
    //
    // Desde que se puede cambiar de fuente en marcha, esa señal se leía como
    // «han elegido abrir una imagen»: paraba la fuente recién arrancada y volvía
    // a abrir el diálogo. El operador veía la carpeta cerrarse y abrirse una y
    // otra vez sin llegar a cargar nada.
    pci::ui::MainWindow window;
    window.resize(1400, 800);

    QComboBox* sources = nullptr;
    for (auto* combo : window.findChildren<QComboBox*>()) {
        if (combo->maximumWidth() == 320) {  // el de la fuente, acotado a 320
            sources = combo;
        }
    }
    ASSERT_NE(sources, nullptr) << "no se encontró el desplegable de fuente";
    ASSERT_GT(sources->count(), 0);

    // Se selecciona el último elemento y se cuenta cuántas veces avisa el combo
    // al insertar por delante.
    sources->setCurrentIndex(sources->count() - 1);
    const QString chosen = sources->currentText();
    QSignalSpy changed(sources, &QComboBox::currentIndexChanged);

    // Insertar por delante SIN bloquear: así es como se veía el fallo.
    sources->insertItem(0, QStringLiteral("fichero abierto"));
    std::printf("  [fuente] insertar por delante avisa %d vez(ces); sigue elegido «%s»\n",
                static_cast<int>(changed.count()), sources->currentText().toStdString().c_str());
    EXPECT_GT(changed.count(), 0)
        << "Qt ya no avisa al desplazar el elemento elegido: el bucle no se reproduce "
           "y este test dejó de vigilar nada";
    // Y la prueba de que el aviso NO significa que se haya elegido otra cosa:
    // el elemento seleccionado es el mismo de antes.
    EXPECT_EQ(sources->currentText(), chosen)
        << "el elemento elegido cambió de verdad: entonces el aviso sí era una elección";
}
