// Pruebas de GESTO sobre el widget real: se renderiza fuera de pantalla y se
// le inyectan eventos de ratón sintéticos. Es lo único que comprueba de verdad
// que un clic, un arrastre o la rueda hagan lo que el operador espera; la
// aritmética por debajo la cubre test_canvas_geometry.
//
// Van en su propio ejecutable porque necesitan Qt y una plataforma "offscreen",
// que el resto de la suite no requiere.
#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPointF>
#include <QString>
#include <QMenu>
#include <QStringList>
#include <QToolBox>
#include <QLabel>
#include <QLayout>
#include <QToolButton>

#include <cmath>
#include <cstdio>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "vision/geometry_features.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QTableWidget>

#include "inspection_editor/auto_measure_dialog.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "inspection_editor/canvas/tool_icons.h"
#include "inspection_editor/canvas/tool_palette.h"
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

TEST(ToolPaletteTest, EveryToolIsReachableInEveryShape) {
    // Agrupar no puede esconder nada: si una herramienta no aparece en ninguna
    // familia, deja de existir para el operador aunque el codigo la tenga.
    //
    // En el panel hay que ABRIR cada familia, porque solo se instancian los
    // botones de la activa. Eso es a proposito —crear 32 botones para enseñar 8
    // seria trabajo tirado en cada cambio— y obliga al test a recorrerlas, que
    // es exactamente lo que hace el operador.
    for (const auto shape : {ToolPalette::Shape::Compact, ToolPalette::Shape::Accordion,
                             ToolPalette::Shape::Panel}) {
        ToolPalette palette(shape);
        std::vector<ToolType> reachable;

        const auto collect = [&palette, &reachable] {
            for (auto* button : palette.findChildren<QToolButton*>()) {
                if (auto* menu = button->menu(); menu != nullptr) {
                    for (auto* action : menu->actions()) {
                        for (const ToolType type : allToolTypes()) {
                            if (action->text() == QString::fromUtf8(toolTypeLabel(type))) {
                                reachable.push_back(type);
                            }
                        }
                    }
                    continue;
                }
                for (const ToolType type : allToolTypes()) {
                    const QString name = QString::fromUtf8(toolTypeLabel(type));
                    // El panel usa botones de solo icono: el nombre vive en el
                    // tooltip, y ahi es donde el operador lo lee.
                    if (button->text() == name || button->toolTip() == name) {
                        reachable.push_back(type);
                    }
                }
            }
        };

        if (shape == ToolPalette::Shape::Panel) {
            for (const auto category : allToolCategories()) {
                if (toolsInCategory(category).empty()) {
                    continue;
                }
                palette.activateCategory(category);
                collect();
            }
        } else {
            collect();
        }

        std::sort(reachable.begin(), reachable.end());
        reachable.erase(std::unique(reachable.begin(), reachable.end()), reachable.end());
        auto expected = std::vector<ToolType>(allToolTypes().begin(), allToolTypes().end());
        std::sort(expected.begin(), expected.end());
        EXPECT_EQ(reachable, expected)
            << "forma " << static_cast<int>(shape) << ": alguna herramienta quedo escondida";
    }
}

TEST(ToolPaletteTest, TheCompactRowFitsInTheWindow) {
    // La razón de ser del ítem: la fila plana pedía ~1400 px de ancho mínimo en
    // una ventana que arranca a 1100.
    ToolPalette palette(ToolPalette::Shape::Compact);
    const int width = palette.sizeHint().width();
    std::printf("  paleta compacta: %d px de ancho (la fila plana pedía ~1400)\n", width);
    EXPECT_LT(width, 700) << "la paleta tiene que dejar sitio al resto de la barra";
}

TEST(ToolPaletteTest, FamilyPlusDigitPicksTheRightTool) {
    // El atajo que sustituye a la tabla escrita a mano de un dígito por
    // herramienta, que se quedó corta con catorce.
    ToolPalette palette(ToolPalette::Shape::Compact);
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
    ToolPalette palette(ToolPalette::Shape::Accordion);
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
    ToolPalette palette(ToolPalette::Shape::Panel);
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
    ToolPalette palette(ToolPalette::Shape::Panel);
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
    ToolPalette palette(ToolPalette::Shape::Panel);
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
        if (button->toolTip() == QString::fromUtf8(toolTypeLabel(type))) {
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
    ToolPalette palette(ToolPalette::Shape::Panel);
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
    ToolPalette palette(ToolPalette::Shape::Panel);
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
    ToolPalette palette(ToolPalette::Shape::Panel);
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
    ToolPalette palette(ToolPalette::Shape::Panel);
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

TEST(ToolHelpLine, ItDoesNotGrowAndShrinkUnderTheCursor) {
    // Si la linea cambiara de alto al pasar el raton, la rejilla botaria bajo
    // el cursor y elegir se volveria un juego de punteria.
    ToolPalette palette(ToolPalette::Shape::Panel);
    palette.show();
    palette.resize(220, 600);
    palette.activateCategory(ToolCategory::TurnedAndExtremes);

    int tallest = 0;
    int shortest = 1 << 20;
    for (const auto type : toolsInCategory(ToolCategory::TurnedAndExtremes)) {
        auto* button = buttonFor(palette, type);
        ASSERT_NE(button, nullptr);
        hover(button, true);
        for (auto* label : palette.findChildren<QLabel*>()) {
            if (label->wordWrap()) {  // la de la explicacion es la unica que envuelve
                tallest = std::max(tallest, label->height());
                shortest = std::min(shortest, label->height());
            }
        }
        hover(button, false);
    }
    EXPECT_EQ(tallest, shortest) << "la linea de ayuda cambia de alto: la rejilla botaria";
}
