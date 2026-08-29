// Pruebas de GESTO sobre el widget real: se renderiza fuera de pantalla y se
// le inyectan eventos de ratón sintéticos. Es lo único que comprueba de verdad
// que un clic, un arrastre o la rueda hagan lo que el operador espera; la
// aritmética por debajo la cubre test_canvas_geometry.
//
// Van en su propio ejecutable porque necesitan Qt y una plataforma "offscreen",
// que el resto de la suite no requiere.
#include <gtest/gtest.h>

#include <QApplication>
#include <QRadioButton>
#include <QSpinBox>
#include <QAbstractButton>
#include <QColor>
#include <QComboBox>
#include <QSignalSpy>
#include <QImage>
#include <QWheelEvent>
#include "vision/pipeline.h"
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
#include <QPainter>
#include <QElapsedTimer>
#include <QtTest/QTest>
#include <QTemporaryDir>
#include <QAction>
#include <QAbstractSlider>
#include <opencv2/videoio.hpp>
#include "database/schema.h"
#include "repositories/settings_repository.h"
#include "ui/app_repositories.h"
#include <QListWidget>
#include <QCheckBox>
#include "ui/detection_page.h"
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
#include "ui/pieces_page.h"
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
// Recetas de medición: elegir qué se mide en cada clase de pieza
// ---------------------------------------------------------------------------
//
// Petición de uso: «un conjunto personalizado de reglas para algunas piezas
// específicas —engranajes, círculos, piezas cuadradas, rectangulares— para
// tomar de mejor manera las medidas».
//
// Lo que estas pruebas vigilan no es que el desplegable exista, sino las tres
// cosas que lo hacen honrado: que enseñe QUÉ trae cada receta, que al elegir
// una se vuelva a proponer con ella —y no se escondan filas—, y que cuando la
// receta no va con la pieza se DIGA en vez de dejar una tabla vacía.

namespace {

// Un reproponer de mentira que apunta con qué receta se le llamó. Sin esto no
// se puede distinguir «eligió la receta» de «escondió filas», que es
// exactamente el fallo que el filtro de clases ya tuvo una vez.
struct RecipeSpy {
    std::vector<pci::inspection::MeasureRecipe> asked;
    pci::inspection::RecipeResult answer;

    pci::inspection::AutoMeasureDialog::Reproposer reproposer() {
        return [this](const pci::inspection::MeasureRecipe& recipe) {
            asked.push_back(recipe);
            return answer;
        };
    }
};

pci::inspection::RecipeResult twoCotas() {
    pci::inspection::RecipeResult result;
    result.applies = true;
    result.proposals.resize(2);
    result.proposals[0].config.name = "Ø exterior";
    result.proposals[1].config.name = "Ø interior";
    return result;
}

}  // namespace

TEST(AutoMeasureRecipeUi, TheSelectorOffersEveryFactoryRecipeAndSaysWhatItBrings) {
    RecipeSpy spy;
    spy.answer = twoCotas();
    AutoMeasureDialog dialog({}, 0.0, nullptr, spy.reproposer());

    auto* box = dialog.findChild<QComboBox*>(QStringLiteral("recipeBox"));
    ASSERT_NE(box, nullptr) << "no hay dónde elegir la receta";
    // Se comparan con la lista de verdad y no con nombres escritos aquí: una
    // receta nueva tiene que aparecer sola en el diálogo, que es la razón de
    // que `factoryRecipes()` viva al lado de quien las aplica.
    ASSERT_EQ(box->count(), static_cast<int>(pci::inspection::factoryRecipes().size()));
    for (int i = 0; i < box->count(); ++i) {
        EXPECT_EQ(box->itemText(i).toStdString(),
                  pci::inspection::factoryRecipes()[static_cast<std::size_t>(i)].name);
    }

    // Y qué trae cada una, en una frase: seis nombres sin explicación se eligen
    // a ciegas.
    auto* what = dialog.findChild<QLabel*>(QStringLiteral("recipeWhat"));
    ASSERT_NE(what, nullptr) << "la receta no dice qué mide";
    const QString first = what->text();
    EXPECT_FALSE(first.isEmpty());
    box->setCurrentText(QStringLiteral("Arandela"));
    EXPECT_NE(what->text(), first)
        << "el texto no cambia al cambiar de receta: está explicando la anterior";
}

TEST(AutoMeasureRecipeUi, ChoosingARecipeReproposesWithItAndTicksItsClasses) {
    RecipeSpy spy;
    spy.answer = twoCotas();
    AutoMeasureDialog dialog({}, 0.0, nullptr, spy.reproposer());
    auto* box = dialog.findChild<QComboBox*>(QStringLiteral("recipeBox"));
    ASSERT_NE(box, nullptr);

    spy.asked.clear();
    box->setCurrentText(QStringLiteral("Arandela"));
    ASSERT_FALSE(spy.asked.empty()) << "elegir una receta no vuelve a proponer: entonces "
                                       "está escondiendo filas, y el recorte a doce ya se "
                                       "habrá comido las cotas que sí se querían";
    const auto& asked = spy.asked.back();
    const auto* washer = pci::inspection::recipeNamed("Arandela");
    ASSERT_NE(washer, nullptr);
    EXPECT_EQ(asked.name, washer->name);
    EXPECT_EQ(asked.options.allowedTypes, washer->options.allowedTypes)
        << "se vuelve a proponer con otras clases que las de la receta elegida";

    // Y las casillas enseñan lo que la receta trae: una receta que no se ve es
    // una caja negra, y el operador no puede ajustarla sin salir del diálogo.
    for (auto* check : dialog.findChildren<QCheckBox*>()) {
        const std::string name = check->text().toStdString();
        if (name == "Círculo") {
            EXPECT_TRUE(check->isChecked()) << "la receta de la arandela trae el diámetro y "
                                               "su casilla sale desmarcada";
        }
        if (name == "Ángulo") {
            EXPECT_FALSE(check->isChecked())
                << "la receta de la arandela no trae ángulos y su casilla sale marcada";
        }
    }
}

TEST(AutoMeasureRecipeUi, WhenTheRecipeIsNotForThisPieceItSaysWhy) {
    // LO QUE IMPIDE QUE «RECETA» SIGNIFIQUE «FORZAR». La receta de la tuerca
    // sobre una arandela no devuelve cero cotas: devuelve un motivo, y el
    // diálogo tiene que enseñarlo. Una tabla vacía sin explicación se lee como
    // «esta pieza no tiene nada que medir», que es falso.
    RecipeSpy spy;
    spy.answer.applies = false;
    spy.answer.why = "La receta «Tuerca hexagonal» es para una tuerca hexagonal, y esta "
                     "pieza se ha reconocido como arandela.";
    AutoMeasureDialog dialog({}, 0.0, nullptr, spy.reproposer());
    auto* box = dialog.findChild<QComboBox*>(QStringLiteral("recipeBox"));
    ASSERT_NE(box, nullptr);
    box->setCurrentText(QStringLiteral("Tuerca hexagonal"));

    auto* notice = dialog.findChild<QLabel*>(QStringLiteral("recipeNotice"));
    ASSERT_NE(notice, nullptr) << "no hay dónde decir por qué no se propone nada";
    EXPECT_TRUE(notice->isVisibleTo(&dialog))
        << "la receta no aplica y el diálogo no lo dice: la tabla vacía se lee como que la "
           "pieza no tiene cotas";
    EXPECT_TRUE(notice->text().contains(QStringLiteral("arandela")))
        << "el motivo no llega a la pantalla: " << notice->text().toStdString();

    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 0) << "no aplica y aun así enseña cotas";
}

TEST(AutoMeasureRecipeUi, AdjustingTheBoxesKeepsTheFamilyOfTheRecipe) {
    // Ajustar las casillas cambia QUÉ se propone, no A QUÉ PIEZA se aplica. Si
    // al tocar una casilla la receta se convirtiera en «todas», desmarcar una
    // clase apagaría de paso la comprobación de familia — y la receta de la
    // tuerca empezaría a medir arandelas sin que nadie lo pidiera.
    RecipeSpy spy;
    spy.answer = twoCotas();
    AutoMeasureDialog dialog({}, 0.0, nullptr, spy.reproposer());
    auto* box = dialog.findChild<QComboBox*>(QStringLiteral("recipeBox"));
    ASSERT_NE(box, nullptr);
    box->setCurrentText(QStringLiteral("Arandela"));
    ASSERT_EQ(dialog.chosenRecipe().family, pci::inspection::PieceFamily::Ring);

    for (auto* check : dialog.findChildren<QCheckBox*>()) {
        if (check->text() == QStringLiteral("Región")) {
            check->setChecked(!check->isChecked());
        }
    }
    EXPECT_EQ(dialog.chosenRecipe().family, pci::inspection::PieceFamily::Ring)
        << "tocar una casilla ha cambiado a qué familia se aplica la receta";
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

    // Con calibración pasa a unidades reales, y el ÁREA con el CUADRADO de la
    // escala: a 0,5 mm/px el área en mm² es la cuarta parte del número en px².
    //
    // Este test exigía «mm²» a secas, y con eso estaba fijando una
    // inconsistencia: el lienzo era el ÚNICO sitio que ignoraba el modo
    // Automático para áreas, así que la misma pieza salía «17506 mm²» en el
    // vídeo y «175,06 cm²» en el informe. Ahora los dos usan la misma decisión.
    //
    // Lo que hay que comprobar no es el rótulo concreto, es que **el número sea
    // correcto en la unidad que se esté enseñando** — un área con la escala
    // aplicada linealmente en vez de al cuadrado da un número creíble y falso.
    canvas.setMmPerPixel(0.5);
    const QStringList mm = canvas.contourSummaryLines();
    ASSERT_EQ(mm.size(), 5);
    const QString areaPx = px.at(1);
    const QString areaMm = mm.at(1);
    const double valuePx = areaPx.split(QChar(' ')).at(1).toDouble();
    const double valueMm = areaMm.split(QChar(' ')).at(1).toDouble();
    const double expectedMm2 = valuePx * 0.25;
    const bool inCm2 = areaMm.contains(QStringLiteral("cm²"));
    ASSERT_TRUE(inCm2 || areaMm.contains(QStringLiteral("mm²"))) << areaMm.toStdString();
    const double shownAsMm2 = inCm2 ? valueMm * 100.0 : valueMm;
    std::printf("  área: %.0f px² -> %s\n", valuePx, areaMm.toStdString().c_str());
    EXPECT_NEAR(shownAsMm2, expectedMm2, expectedMm2 * 0.01);

    // Y pidiendo milímetros expresamente, milímetros: «Automática» elige, pero
    // cuando el operador ha elegido no hay nada que decidir.
    canvas.setLengthUnit(pci::inspection::LengthUnit::Millimeters);
    const QStringList forced = canvas.contourSummaryLines();
    ASSERT_EQ(forced.size(), 5);
    EXPECT_TRUE(forced.at(1).contains(QStringLiteral("mm²")))
        << "se pidieron milímetros y el lienzo enseña otra cosa: "
        << forced.at(1).toStdString();
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
    // pasar que alguien los mueva de sitio. Los tres se localizan por su
    // nombre: el rótulo «Mover/Elegir» es de los que el taller pide reescribir.
    auto* move = palette.findChild<QToolButton*>(QStringLiteral("selectTool"));
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

    auto* move = palette.findChild<QToolButton*>(QStringLiteral("selectTool"));
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

// EL MISMO TRAZO, PARA MARCAR O DESCARTAR UNA PIEZA.
//
// Petición de uso: «añadir pieza dibujando un contorno manualmente, y que
// detecte o intente detectar la pieza (igual para quitarlo)».
//
// El gesto es el de la zona libre —a pulso o a clics, el botón derecho
// deshace—, y lo único que cambia es a dónde va el polígono. Se comprueba
// justamente eso: que el trazo llega al sitio correcto, porque un aviso que
// llega al oyente equivocado deja al operador rodeando una pieza y viendo cómo
// cambia la zona de trabajo.
TEST_F(FreeZoneGestureTest, TheSameTraceCanMarkOrDropAPiece) {
    std::vector<cv::Point> outlined;
    int addedTimes = 0;
    int droppedTimes = 0;
    QObject::connect(&canvas, &EditorCanvas::pieceOutlined,
                     [&](const std::vector<cv::Point>& polygon, bool add) {
                         outlined = polygon;
                         if (add) {
                             ++addedTimes;
                         } else {
                             ++droppedTimes;
                         }
                     });

    const auto traceASquare = [this] {
        const std::vector<cv::Point2f> path{{400, 300}, {800, 300}, {800, 700}, {400, 700}};
        press(&canvas, at(path.front().x, path.front().y));
        for (std::size_t i = 1; i <= path.size(); ++i) {
            const cv::Point2f from = path[i - 1];
            const cv::Point2f to = path[i % path.size()];
            for (int step = 1; step <= 30; ++step) {
                const double t = static_cast<double>(step) / 30.0;
                moveTo(&canvas,
                       at(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t));
            }
        }
        release(&canvas, at(path.front().x, path.front().y));
    };

    canvas.setOutlinePickMode(EditorCanvas::TracePurpose::MarkPiece);
    traceASquare();
    EXPECT_EQ(addedTimes, 1) << "rodear una pieza no avisa de que hay que marcarla";
    EXPECT_EQ(pickedTimes, 0)
        << "el trazo se ha entregado como zona de trabajo: el operador rodea una pieza y "
           "lo que cambia es dónde se busca";
    EXPECT_GE(outlined.size(), 4U);

    canvas.setOutlinePickMode(EditorCanvas::TracePurpose::DropPiece);
    traceASquare();
    EXPECT_EQ(droppedTimes, 1) << "rodear una mancha no avisa de que hay que descartarla";
    EXPECT_EQ(addedTimes, 1) << "descartar se ha entregado como marcar: el aviso lleva la "
                                "bandera al revés y la mancha se convertiría en pieza";

    // Y el modo se apaga solo, como el de la zona: dejarlo encendido convierte
    // el siguiente arrastre —mover, dibujar— en una pieza inventada.
    EXPECT_EQ(canvas.tracePurpose(), EditorCanvas::TracePurpose::WorkZone);
}

TEST_F(FreeZoneGestureTest, AnOutlinedAreaGoesIntoTheSameUndoStackAsTheBrush) {
    // Una corrección que no se puede deshacer con Ctrl+Z, al lado de otra que
    // sí, es de las cosas que se aprenden perdiendo trabajo. La zona rodeada
    // entra por el mismo camino que una pincelada.
    cv::Mat area(kImageHeight, kImageWidth, CV_8UC1, cv::Scalar(0));
    cv::rectangle(area, cv::Rect(100, 100, 200, 200), cv::Scalar(255), cv::FILLED);

    EXPECT_EQ(canvas.correctedPixelCount(), 0);
    canvas.applyCorrectionArea(area, false);
    const int marked = canvas.correctedPixelCount();
    std::printf("  [gesto] zona descartada -> %d px corregidos\n", marked);
    EXPECT_EQ(marked, 200 * 200) << "la zona rodeada no llega a la corrección del borde";
    ASSERT_TRUE(canvas.canUndoEdgeCorrection())
        << "rodear no deja nada que deshacer: Ctrl+Z no la quitaría";

    EXPECT_TRUE(canvas.undoEdgeCorrection());
    EXPECT_EQ(canvas.correctedPixelCount(), 0) << "deshacer no quita la zona rodeada";
}

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
    auto* watch = dialog.findChild<QPushButton*>(QStringLiteral("watchButton"));
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

    // Un solo control, no dos botones cuyas etiquetas cambian de verbo. Se
    // busca por nombre y se comprueba aparte que TIENE menú, que es la mitad de
    // lo que este test afirma: buscarlo por «tiene menú y dice Zona» daba por
    // buena la condición que había que comprobar.
    auto* zone = window.findChild<QToolButton*>(QStringLiteral("zoneButton"));
    ASSERT_NE(zone, nullptr) << "no hay control de zona";
    ASSERT_NE(zone->menu(), nullptr) << "el control de zona no despliega sus acciones";

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
    button = window.findChild<QPushButton*>(QStringLiteral("autoInspectButton"));
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
    button = window.findChild<QPushButton*>(QStringLiteral("autoInspectButton"));
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

    auto* select = palette.findChild<QAbstractButton*>(QStringLiteral("selectTool"));
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

// ---------------------------------------------------------------------------
// El pincel de borde
// ---------------------------------------------------------------------------

TEST(EdgeBrush, PaintingMarksTheAreaAndEmitsItOnRelease) {
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});

    cv::Mat lastAdd;
    cv::Mat lastRemove;
    int emissions = 0;
    QObject::connect(&canvas, &EditorCanvas::edgeCorrected,
                     [&](const cv::Mat& add, const cv::Mat& remove) {
                         lastAdd = add.clone();
                         lastRemove = remove.clone();
                         ++emissions;
                     });

    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);

    canvas.setBrushRadius(20);
    const ViewTransform view = viewAt(1.0);
    press(&canvas, toScreen(view, {600.0F, 400.0F}));
    moveTo(&canvas, toScreen(view, {700.0F, 400.0F}));
    // Se emite al SOLTAR, no en cada punto: reanalizar por cada píxel del trazo
    // dejaría el pincel a tirones.
    EXPECT_EQ(emissions, 0) << "se emitió a mitad del trazo";
    release(&canvas, toScreen(view, {700.0F, 400.0F}));

    ASSERT_EQ(emissions, 1);
    ASSERT_FALSE(lastAdd.empty()) << "la pincelada no marcó nada";
    EXPECT_EQ(lastAdd.size(), cv::Size(kImageWidth, kImageHeight))
        << "la corrección no está en coordenadas de imagen";
    const int painted = cv::countNonZero(lastAdd);
    std::printf("  [pincel] un trazo de 100 px con radio 20 marca %d px\n", painted);
    EXPECT_GT(painted, 0);
    // Y marca DONDE se pintó, no en cualquier sitio.
    EXPECT_EQ(lastAdd.at<uchar>(400, 650), 255) << "no marcó por donde pasó el trazo";
    EXPECT_EQ(lastAdd.at<uchar>(100, 100), 0) << "marcó donde no se pintó";
}

TEST(EdgeBrush, PaintingOneColourErasesFromTheOther) {
    // Sin esto, marcar fondo sobre algo marcado como pieza dejaría las dos
    // máscaras diciendo cosas opuestas del mismo píxel, y el resultado
    // dependería del orden en que se aplicaran — exactamente la clase de estado
    // que nadie puede razonar.
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});

    cv::Mat add;
    cv::Mat remove;
    QObject::connect(&canvas, &EditorCanvas::edgeCorrected,
                     [&](const cv::Mat& a, const cv::Mat& r) {
                         add = a.clone();
                         remove = r.clone();
                     });

    const ViewTransform view = viewAt(1.0);
    const QPointF spot = toScreen(view, {600.0F, 400.0F});

    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);

    canvas.setBrushRadius(20);
    press(&canvas, spot);
    release(&canvas, spot);
    ASSERT_FALSE(add.empty());
    ASSERT_EQ(add.at<uchar>(400, 600), 255);

    // Ahora se pinta lo mismo como fondo: tiene que desaparecer de «añadir».
    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::RemovePiece);
    canvas.setBrushRadius(20);
    press(&canvas, spot);
    release(&canvas, spot);
    ASSERT_FALSE(remove.empty());
    EXPECT_EQ(remove.at<uchar>(400, 600), 255) << "no marcó como fondo";
    EXPECT_EQ(add.at<uchar>(400, 600), 0)
        << "el mismo píxel sigue marcado como pieza Y como fondo";
}

TEST(EdgeBrush, WithTheBrushOffAClickDoesNotPaint) {
    // El pincel apagado no puede pintar: si un clic marcara, seleccionar una
    // herramienta llenaría la imagen de correcciones sin que nadie lo pidiera.
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});

    int emissions = 0;
    QObject::connect(&canvas, &EditorCanvas::edgeCorrected,
                     [&](const cv::Mat&, const cv::Mat&) { ++emissions; });

    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::Off);
    const ViewTransform view = viewAt(1.0);
    drag(&canvas, toScreen(view, {600.0F, 400.0F}), toScreen(view, {700.0F, 450.0F}));
    EXPECT_EQ(emissions, 0) << "pintó con el pincel apagado";
}

TEST(EdgeBrush, TheWheelZoomsAlwaysAndAltSizesTheBrush) {
    // Es lo que hace cualquier editor, y es lo que se necesita: el grosor se
    // ajusta constantemente mientras se corrige —grueso para rellenar, fino
    // para perfilar— y tener que ir a un menú por cada cambio haría que nadie
    // lo cambiara.
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});

    // Con el pincel APAGADO, la rueda sigue siendo el zoom.
    const double zoomBefore = canvas.zoomFactor();
    QWheelEvent zoomIn(QPointF(400, 300), canvas.mapToGlobal(QPointF(400, 300)), QPoint(),
                       QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&canvas, &zoomIn);
    EXPECT_GT(canvas.zoomFactor(), zoomBefore) << "sin pincel, la rueda dejó de hacer zoom";

    // CON EL PINCEL ENCENDIDO, LA RUEDA SIGUE HACIENDO ZOOM.
    //
    // Esta prueba comprobaba lo contrario, y se cambió a petición del taller:
    // «quiero hacerle zoom a la imagen, pero se agranda o se achica el cursor, y
    // me arruina la experiencia». El gesto ya no depende del modo — uno que solo
    // vale a veces se acaba no usando.
    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(12);
    const double zoomWithBrush = canvas.zoomFactor();
    const int radiusKept = canvas.brushRadius();
    QWheelEvent zoomAgain(QPointF(400, 300), canvas.mapToGlobal(QPointF(400, 300)), QPoint(),
                          QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                          false);
    QApplication::sendEvent(&canvas, &zoomAgain);
    std::printf("  [pincel] con el pincel puesto, la rueda: zoom %.2f -> %.2f, radio %d\n",
                zoomWithBrush, canvas.zoomFactor(), canvas.brushRadius());
    EXPECT_GT(canvas.zoomFactor(), zoomWithBrush)
        << "con el pincel puesto la rueda no acerca, que es exactamente la queja";
    EXPECT_EQ(canvas.brushRadius(), radiusKept)
        << "la rueda sigue cambiando el pincel a espaldas de quien solo quería acercarse";

    // Y ALT+RUEDA dimensiona, para quien no quiere soltar el ratón mientras
    // perfila un borde.
    const double zoomKept = canvas.zoomFactor();
    const int before = canvas.brushRadius();
    QWheelEvent bigger(QPointF(400, 300), canvas.mapToGlobal(QPointF(400, 300)), QPoint(),
                       QPoint(0, 120), Qt::NoButton, Qt::AltModifier, Qt::NoScrollPhase,
                       false);
    QApplication::sendEvent(&canvas, &bigger);
    std::printf("  [pincel] Alt+rueda: radio %d -> %d; zoom sin tocar\n", before,
                canvas.brushRadius());
    EXPECT_GT(canvas.brushRadius(), before) << "Alt+rueda no agrandó el pincel";
    EXPECT_DOUBLE_EQ(canvas.zoomFactor(), zoomKept) << "el pincel cambió Y encima hizo zoom";

    // Y hacia el otro lado.
    const int grown = canvas.brushRadius();
    QWheelEvent smaller(QPointF(400, 300), canvas.mapToGlobal(QPointF(400, 300)), QPoint(),
                        QPoint(0, -120), Qt::NoButton, Qt::AltModifier, Qt::NoScrollPhase,
                        false);
    QApplication::sendEvent(&canvas, &smaller);
    EXPECT_LT(canvas.brushRadius(), grown) << "Alt+rueda no encogió el pincel";

    // Con topes: por abajo no puntea, por arriba no borra media pieza.
    for (int i = 0; i < 40; ++i) {
        QWheelEvent tiny(QPointF(400, 300), canvas.mapToGlobal(QPointF(400, 300)), QPoint(),
                         QPoint(0, -120), Qt::NoButton, Qt::AltModifier, Qt::NoScrollPhase,
                         false);
        QApplication::sendEvent(&canvas, &tiny);
    }
    EXPECT_GE(canvas.brushRadius(), 2) << "el pincel se hizo tan fino que puntea";
}

TEST(EdgeBrush, TheCorrectionActuallyChangesWhatTheAnalysisFinds) {
    // EL TEST QUE FALTABA, y por eso se dijo que funcionaba sin que funcionara:
    // los otros comprueban que la pincelada marca píxeles, no que el ANÁLISIS
    // haga caso. Este entra por donde entra la aplicación — se pinta sobre el
    // lienzo, se recoge la corrección emitida y se analiza con ella.
    //
    // La escena: una pieza clara a la que una sombra le come el borde derecho,
    // que es el caso que motiva la herramienta.
    const int w = kImageWidth;
    const int h = kImageHeight;
    cv::Mat scene(h, w, CV_8UC3, cv::Scalar(24, 22, 20));
    const cv::Rect piece(600, 300, 400, 300);
    cv::rectangle(scene, piece, cv::Scalar(210, 214, 218), cv::FILLED, cv::LINE_8);
    const cv::Rect eaten(piece.x + piece.width - 100, piece.y, 100, piece.height);
    scene(eaten).setTo(cv::Scalar(26, 24, 22));

    const auto before = pci::vision::analyzeFrame(scene, {});
    ASSERT_TRUE(before.isOk()) << before.error().message;
    const int widthWithShadow = cv::boundingRect(before.value().contour.points).width;
    ASSERT_LT(widthWithShadow, piece.width - 50)
        << "la sombra no se comió el borde: el test no reproduce el caso";

    // Ahora el gesto de verdad, sobre el lienzo.
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    QImage shown(scene.data, scene.cols, scene.rows, static_cast<int>(scene.step),
                 QImage::Format_BGR888);
    canvas.setScene(shown.copy().convertToFormat(QImage::Format_RGB888),
                    pci::vision::Fixture{});

    cv::Mat add;
    cv::Mat remove;
    QObject::connect(&canvas, &EditorCanvas::edgeCorrected,
                     [&](const cv::Mat& a, const cv::Mat& r) {
                         add = a.clone();
                         remove = r.clone();
                     });

    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);

    canvas.setBrushRadius(60);
    const ViewTransform view = viewAt(1.0);
    const float midY = static_cast<float>(eaten.y + eaten.height / 2);
    press(&canvas, toScreen(view, {static_cast<float>(eaten.x + 10), midY}));
    for (int y = eaten.y + 60; y < eaten.br().y - 60; y += 40) {
        moveTo(&canvas, toScreen(view, {static_cast<float>(eaten.x + eaten.width / 2),
                                        static_cast<float>(y)}));
    }
    release(&canvas, toScreen(view, {static_cast<float>(eaten.br().x - 10),
                                     static_cast<float>(eaten.br().y - 60)}));
    ASSERT_FALSE(add.empty()) << "el lienzo no emitió ninguna corrección";

    // Y AHORA lo que importa: analizar CON esa corrección tiene que cambiar el
    // contorno. Si esto falla, el pincel pinta y la detección lo ignora — que es
    // exactamente lo que el operador vio.
    pci::vision::PipelineConfig corrected;
    corrected.forcePiece = add;
    corrected.forceBackground = remove;
    const auto after = pci::vision::analyzeFrame(scene, corrected);
    ASSERT_TRUE(after.isOk()) << after.error().message;
    const int widthCorrected = cv::boundingRect(after.value().contour.points).width;
    std::printf("  [pincel] ancho con sombra %d px; tras corregir %d px\n", widthWithShadow,
                widthCorrected);
    EXPECT_GT(widthCorrected, widthWithShadow + 40)
        << "la corrección no cambió lo que encuentra el análisis";
}

TEST(EdgeBrush, TheEmittedCorrectionIsNotTheCanvasBuffer) {
    // LA CAUSA DE QUE LA APLICACIÓN SE CERRARA SOLA.
    //
    // `cv::Mat` es de recuento de referencias: entregar la máscara interna tal
    // cual daba a quien la recibía el MISMO búfer que el lienzo iba a seguir
    // pintando. Y quien la recibe la pasa a un hilo de trabajo para analizar,
    // así que un hilo escribía mientras el otro leía — comportamiento
    // indefinido, y en la práctica la aplicación cerrándose a mitad de una
    // corrección.
    //
    // La carrera no se puede provocar desde un test —depende del planificador—
    // pero sí se puede comprobar la propiedad que la impide: lo que se entrega
    // no cambia cuando el lienzo sigue pintando.
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});

    cv::Mat firstDelivery;
    QObject::connect(&canvas, &EditorCanvas::edgeCorrected,
                     [&](const cv::Mat& add, const cv::Mat&) {
                         if (firstDelivery.empty()) {
                             firstDelivery = add;  // SIN clonar: es lo que se prueba
                         }
                     });

    const ViewTransform view = viewAt(1.0);
    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(20);
    press(&canvas, toScreen(view, {600.0F, 400.0F}));
    release(&canvas, toScreen(view, {600.0F, 400.0F}));
    ASSERT_FALSE(firstDelivery.empty());
    const int whenDelivered = cv::countNonZero(firstDelivery);
    ASSERT_GT(whenDelivered, 0);

    // Segunda pincelada, muy lejos de la primera.
    press(&canvas, toScreen(view, {200.0F, 800.0F}));
    release(&canvas, toScreen(view, {200.0F, 800.0F}));

    const int now = cv::countNonZero(firstDelivery);
    std::printf("  [pincel] lo entregado tenía %d px y sigue teniendo %d tras pintar más\n",
                whenDelivered, now);
    EXPECT_EQ(now, whenDelivered)
        << "lo entregado creció solo: comparte búfer con el lienzo, y eso es la carrera "
           "que cerraba la aplicación";
}

TEST(EdgeBrush, TheButtonTurnsOnOnceThereIsAnImageToPaintOn) {
    // EL FALLO QUE EL OPERADOR VIO: «no me deja usar la función sobre un vídeo,
    // u imagen». El botón se quedaba apagado PARA SIEMPRE.
    //
    // La disponibilidad se calculaba una sola vez, al montar la fuente, y en ese
    // momento todavía no había llegado ningún frame — la fuente arranca DESPUÉS.
    // Con `lastFrame_` vacío la respuesta era «no», y nadie volvía a preguntarlo.
    //
    // Este test comprueba la propiedad que faltaba: que la respuesta se
    // reconsidere cuando llega la imagen.
    pci::ui::MainWindow window;
    window.resize(1400, 800);

    QToolButton* brush = nullptr;
    brush = window.findChild<QToolButton*>(QStringLiteral("edgeBrushButton"));
    ASSERT_NE(brush, nullptr) << "no está el botón de corregir el borde";

    // Recién abierta no hay imagen: apagado, y con su motivo.
    EXPECT_FALSE(brush->isEnabled());
    EXPECT_FALSE(brush->toolTip().isEmpty()) << "apagado y sin decir por qué";

    // Llega una foto, como cuando se congela un frame o se abre un fichero.
    QImage photo(320, 240, QImage::Format_RGB888);
    photo.fill(QColor(40, 40, 40));
    QMetaObject::invokeMethod(&window, "onFrame", Qt::DirectConnection,
                              Q_ARG(QImage, photo));
    QApplication::processEvents();

    std::printf("  [pincel] tras llegar la imagen el botón está %s\n",
                brush->isEnabled() ? "encendido" : "APAGADO");
    // Nota: con la fuente todavía en «cámara» sigue apagado a propósito —lo que
    // este test fija es que la pregunta se REHACE al llegar el frame, que es lo
    // que no ocurría. El tooltip lo demuestra: cambia según el caso.
    EXPECT_FALSE(brush->toolTip().isEmpty());
}

// LA PRUEBA QUE FALTABA, y por eso se colaron tres fallos seguidos: abrir una
// imagen de verdad, pintar encima con el pincel, y mirar si la línea verde se
// mueve. Todo lo demás comprobaba piezas sueltas del camino.
// Superficie de un polígono (fórmula del cordón de zapato). Sirve para
// responder a «¿se movió la línea verde?» con un número y no con una impresión.
double polygonArea(const QPolygonF& polygon) {
    double sum = 0.0;
    for (int i = 0; i < polygon.size(); ++i) {
        const QPointF& a = polygon.at(i);
        const QPointF& b = polygon.at((i + 1) % polygon.size());
        sum += a.x() * b.y() - b.x() * a.y();
    }
    return sum / 2.0;
}

// Una pincelada de verdad sobre el lienzo: pulsar, arrastrar por puntos
// intermedios y soltar, en coordenadas de IMAGEN.
void paintStroke(EditorCanvas* canvas, QPoint fromImage, QPoint toImage) {
    const ViewTransform view({canvas->imageSize().width(), canvas->imageSize().height()},
                             {canvas->width(), canvas->height()}, 1.0, {0.0, 0.0});
    const auto screen = [&](QPoint p) {
        const cv::Point2d q = view.imageToWidget(cv::Point2f(static_cast<float>(p.x()),
                                                             static_cast<float>(p.y())));
        return QPointF(q.x, q.y);
    };
    press(canvas, screen(fromImage));
    for (int step = 1; step <= 6; ++step) {
        const QPoint mid(fromImage.x() + (toImage.x() - fromImage.x()) * step / 6,
                         fromImage.y() + (toImage.y() - fromImage.y()) * step / 6);
        moveTo(canvas, screen(mid));
    }
    release(canvas, screen(toImage));
}

TEST(EdgeBrush, PaintingOnARealOpenedImageMovesTheGreenLine) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    // Una pieza clara sobre fondo oscuro, con una MUESCA oscura en el lado
    // derecho: eso es exactamente lo que hace una sombra, y la detección la
    // dejará fuera de la pieza.
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(100, 80, 200, 140), QColor(230, 230, 230));
        painter.fillRect(QRect(250, 120, 50, 60), QColor(22, 22, 22));  // la "sombra"
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("pieza.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));

    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);

    // Esperar a que el análisis dé un contorno. Sin esto no hay nada que corregir.
    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };
    ASSERT_TRUE(waitFor([&] { return canvas->liveContour().size() >= 4; }))
        << "no se detectó ninguna pieza en la imagen abierta";

    const QRectF before = canvas->liveContour().boundingRect();
    const double areaBefore = std::abs(polygonArea(canvas->liveContour()));
    std::printf("  [borde] contorno detectado: %.0f px2, caja %.0fx%.0f\n", areaBefore,
                before.width(), before.height());

    // Ahora se pinta la muesca como PIEZA, que es lo que el operador haría al
    // ver que una sombra se le come una esquina.
    QToolButton* brush = nullptr;
    brush = window.findChild<QToolButton*>(QStringLiteral("edgeBrushButton"));
    ASSERT_NE(brush, nullptr);
    EXPECT_TRUE(brush->isEnabled())
        << "con una imagen abierta el pincel tiene que dejarse usar. Dice: "
        << brush->toolTip().toStdString();
    EXPECT_TRUE(brush->toolTip().startsWith(QStringLiteral("Corrige")))
        << "encendido pero explicando por qué está apagado";

    canvas->setEdgeBrush(pci::inspection::EditorCanvas::EdgeBrush::AddPiece);

    canvas->setBrushRadius(18);
    paintStroke(canvas, QPoint(250, 120), QPoint(300, 180));

    // Y se espera a que el reanálisis termine. NO hay que confirmar nada: el
    // trazo se procesa al soltar el botón.
    ASSERT_TRUE(waitFor([&] {
        return std::abs(polygonArea(canvas->liveContour())) > areaBefore * 1.01;
    })) << "se pintó sobre el contorno y la línea verde no se movió: el pincel no "
           "llega al análisis";

    const double areaAfter = std::abs(polygonArea(canvas->liveContour()));
    std::printf("  [borde] tras pintar la muesca: %.0f px2 (%+.1f%%)\n", areaAfter,
                100.0 * (areaAfter - areaBefore) / areaBefore);
    EXPECT_GT(areaAfter, areaBefore)
        << "corregir hacia dentro tiene que AÑADIR superficie a la pieza";
}

// Al abrir un fichero, la enumeración de cámaras seguía su curso en segundo
// plano y, al terminar, repoblaba el desplegable: `clear()` + `addItem()` mueve
// el índice de -1 a 0 y Qt emite `currentIndexChanged`. Ese aviso se leía como
// «han elegido la cámara 0», y el fichero recién abierto se cerraba solo.
//
// Nadie eligió nada. Lo eligió un índice al moverse.
TEST(SourceCombo, RefreshingTheCameraListDoesNotCloseTheOpenFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(320, 240, QImage::Format_RGB888);
    photo.fill(QColor(30, 30, 30));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(80, 60, 160, 120), QColor(220, 220, 220));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("abierta.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1000, 700);
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);

    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };
    ASSERT_TRUE(waitFor([&] { return canvas->imageSize() == QSize(320, 240); }))
        << "la imagen abierta no llegó a mostrarse";

    // Y ahora se repuebla la lista, que es lo que hace la enumeración al acabar
    // y lo que hace «Actualizar».
    QMetaObject::invokeMethod(&window, "refreshCameras", Qt::DirectConnection);
    QTest::qWait(600);
    QApplication::processEvents();

    EXPECT_EQ(canvas->imageSize(), QSize(320, 240))
        << "actualizar la lista de cámaras cerró el fichero abierto";

    // Y el desplegable sigue diciendo QUÉ está abierto, no «cámara 0».
    QComboBox* sources = nullptr;
    for (auto* combo : window.findChildren<QComboBox*>()) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemText(i).contains(QStringLiteral("abierta.png"))) {
                sources = combo;
            }
        }
    }
    ASSERT_NE(sources, nullptr) << "el fichero abierto desapareció de la lista de fuentes";
    std::printf("  [fuentes] el desplegable dice: %s\\n",
                sources->currentText().toStdString().c_str());
    EXPECT_TRUE(sources->currentText().contains(QStringLiteral("abierta.png")))
        << "la lista dice una fuente y en pantalla se ve otra";
}

// La segunda mitad de corregir el borde: que la corrección sirva para AFINAR la
// detección, y no sólo para tapar el fallo en esta imagen.
//
// Este test no abre el diálogo —hacerlo colgaría la suite, como ya pasó una
// vez— sino que comprueba lo que decide si la función se puede usar: que la
// acción esté, que nazca apagada, y que se encienda sola en cuanto hay algo que
// aprender. Una acción que existe y nunca se enciende no existe.
TEST(EdgeBrush, CorrectingTheEdgeUnlocksTuningTheDetection) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(100, 80, 200, 140), QColor(230, 230, 230));
        painter.fillRect(QRect(250, 120, 50, 60), QColor(110, 110, 110));  // penumbra
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("penumbra.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);

    auto* tune = window.findChild<QAction*>(QStringLiteral("brushTuneAction"));
    ASSERT_NE(tune, nullptr) << "no está la acción de afinar la detección";
    EXPECT_FALSE(tune->isEnabled())
        << "encendida sin ninguna corrección: afinar con la nada devolvería los "
           "ajustes de ahora presentados como un hallazgo";

    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };
    ASSERT_TRUE(waitFor([&] { return canvas->liveContour().size() >= 4; }));

    canvas->setEdgeBrush(pci::inspection::EditorCanvas::EdgeBrush::AddPiece);

    canvas->setBrushRadius(18);
    paintStroke(canvas, QPoint(255, 125), QPoint(295, 175));
    ASSERT_TRUE(waitFor([&] { return tune->isEnabled(); }))
        << "se corrigió el borde y afinar sigue apagado";

    std::printf("  [afinar] tras corregir, la acción dice: %s\\n",
                tune->text().toStdString().c_str());

    // Y al retirar la corrección vuelve a apagarse: ya no hay nada que aprender.
    canvas->clearEdgeCorrection();
    ASSERT_TRUE(waitFor([&] { return !tune->isEnabled(); }))
        << "sin corrección sigue encendida";
}

// Rodear una zona a mano sobre una imagen ABIERTA y comprobar que la medición
// se limita a ella. Es lo que se reportó roto («dibujo y no recorta nada») y lo
// que hasta ahora sólo se probaba por piezas sueltas: el gesto por un lado, la
// regla de la zona por otro, y el análisis por un tercero.
TEST(WorkingZoneEndToEnd, DrawingAZoneAroundOnePieceMeasuresThatOneAndNotTheOther) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    // Dos piezas de tamaños MUY distintos: la grande a la izquierda, la pequeña
    // a la derecha. Sin zona, el análisis se queda con la mayor; con la zona
    // alrededor de la pequeña tiene que quedarse con la pequeña. Si el recorte
    // no se aplicara, el contorno seguiría siendo el de la grande y el test lo
    // vería — que es justo lo que no detectaba nada antes.
    QImage photo(480, 320, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(40, 60, 180, 200), QColor(230, 230, 230));   // grande
        painter.fillRect(QRect(330, 130, 80, 60), QColor(230, 230, 230));   // pequeña
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("dos.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);

    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };
    ASSERT_TRUE(waitFor([&] { return canvas->liveContour().size() >= 4; }));

    const QRectF whole = canvas->liveContour().boundingRect();
    std::printf("  [zona] sin zona mide la caja %.0fx%.0f en x=%.0f\\n", whole.width(),
                whole.height(), whole.left());
    ASSERT_GT(whole.width(), 150.0) << "sin zona tenía que quedarse con la pieza grande";
    ASSERT_LT(whole.left(), 100.0) << "la grande está a la izquierda";

    // Ahora se rodea la PEQUEÑA a pulso, como haría el operador.
    const ViewTransform view({canvas->imageSize().width(), canvas->imageSize().height()},
                             {canvas->width(), canvas->height()}, 1.0, {0.0, 0.0});
    const auto screen = [&](double x, double y) {
        const cv::Point2d q = view.imageToWidget(
            cv::Point2f(static_cast<float>(x), static_cast<float>(y)));
        return QPointF(q.x, q.y);
    };
    canvas->setFreeZonePickMode(true);
    const std::vector<QPointF> corners{{300, 105}, {440, 105}, {440, 215}, {300, 215}};
    press(canvas, screen(corners.front().x(), corners.front().y()));
    for (std::size_t i = 1; i <= corners.size(); ++i) {
        const QPointF from = corners[i - 1];
        const QPointF to = corners[i % corners.size()];
        for (int step = 1; step <= 20; ++step) {
            const double t = static_cast<double>(step) / 20.0;
            moveTo(canvas, screen(from.x() + (to.x() - from.x()) * t,
                                  from.y() + (to.y() - from.y()) * t));
        }
    }
    release(canvas, screen(corners.front().x(), corners.front().y()));

    ASSERT_TRUE(waitFor([&] {
        const QRectF box = canvas->liveContour().boundingRect();
        return box.width() > 1.0 && box.left() > 250.0;
    })) << "se dibujó la zona alrededor de la pieza pequeña y se sigue midiendo la grande: "
           "la zona no recorta nada";

    const QRectF inZone = canvas->liveContour().boundingRect();
    std::printf("  [zona] con zona mide la caja %.0fx%.0f en x=%.0f\\n", inZone.width(),
                inZone.height(), inZone.left());
    EXPECT_LT(inZone.width(), 120.0) << "la pieza de dentro de la zona es la pequeña";
    EXPECT_GT(inZone.left(), 300.0) << "el contorno se salió de la zona dibujada";
}

// «Solo detecta una»: seis piezas delante y la ventana sin decir nada.
//
// El recuento existía, pero sólo se encendía si la pieza declaraba esperar
// varias o si el operador tenía abierta la pestaña Piezas — y sólo se veía DENTRO
// de ese diálogo. Con los ajustes de fábrica y seis piezas en el encuadre, la
// aplicación medía la mayor en silencio.
TEST(MultiPieceEndToEnd, WithSixPiecesInViewTheWindowSaysSix) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(600, 400, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        for (int i = 0; i < 6; ++i) {
            const int x = 40 + (i % 3) * 190;
            const int y = 50 + (i / 3) * 180;
            painter.fillRect(QRect(x, y, 120 - i * 6, 110 - i * 5), QColor(230, 230, 230));
        }
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("seis.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    const auto waitFor = [](auto predicate, int ms = 5000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };

    // El indicador tiene que estar DONDE SE TRABAJA, no dentro de un diálogo que
    // hay que saber abrir.
    QLabel* chip = nullptr;
    const auto findChip = [&] {
        // `piecesChip` y NO `modeChip`: las dos dicen «pieza» y por eso la
        // búsqueda por texto leía la que llegara última. Está avisado en el
        // propio `main_window.cpp`, donde se le puso el nombre.
        chip = window.findChild<QLabel*>(QStringLiteral("piecesChip"));
        return chip != nullptr && chip->isVisible();
    };
    ASSERT_TRUE(waitFor(findChip))
        << "con seis piezas delante, la ventana principal no dice cuántas ve";

    std::printf("  [contar] la ventana dice: «%s»\\n", chip->text().trimmed().toStdString().c_str());
    EXPECT_TRUE(chip->text().contains(QStringLiteral("6")))
        << "dice «" << chip->text().toStdString() << "» con seis piezas en el encuadre";
    EXPECT_FALSE(chip->toolTip().isEmpty())
        << "un aviso que no explica qué hacer con él es sólo un número más";
}

// Y con una sola pieza no grita: el aviso destacado sólo tiene sentido cuando
// cambia lo que el operador debe hacer.
TEST(MultiPieceEndToEnd, WithASinglePieceItDoesNotShout) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(120, 90, 160, 120), QColor(230, 230, 230));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("una.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    QElapsedTimer timer;
    timer.start();
    QLabel* chip = nullptr;
    while (timer.elapsed() < 3000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        chip = window.findChild<QLabel*>(QStringLiteral("piecesChip"));
        if (chip != nullptr && !chip->isVisible()) {
            chip = nullptr;
        }
        if (chip != nullptr) {
            break;
        }
    }
    ASSERT_NE(chip, nullptr) << "el recuento no aparece ni con una pieza";
    std::printf("  [contar] con una sola pieza dice: «%s»\\n",
                chip->text().trimmed().toStdString().c_str());
    EXPECT_TRUE(chip->text().contains(QStringLiteral("1")));
    EXPECT_FALSE(chip->styleSheet().contains(QStringLiteral("bold")))
        << "destaca con una sola pieza: el aviso pierde sentido si salta siempre";
}

// El trazo es un GESTO, no un resultado. Una vez que la corrección se ha
// aplicado y el contorno se ha movido, dejar la mancha encima confunde lo que
// uno dibujó con lo que el programa detecta: a los tres trazos ya no se sabe
// cuál de las dos cosas se está mirando.
//
// Lo que este test fija es la parte delicada: que al retirar el trazo NO se
// retire la corrección. Si se fuera con él, quitar la mancha desharía el
// trabajo, y el contorno volvería al de antes sin que nadie lo pidiera.
TEST(EdgeBrush, TheStrokeIsRemovedOnceItHasDoneItsWorkButTheCorrectionStays) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(100, 80, 200, 140), QColor(230, 230, 230));
        painter.fillRect(QRect(250, 120, 50, 60), QColor(22, 22, 22));  // la "sombra"
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("trazo.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);

    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };
    ASSERT_TRUE(waitFor([&] { return canvas->liveContour().size() >= 4; }));
    const double areaBefore = std::abs(polygonArea(canvas->liveContour()));

    canvas->setEdgeBrush(pci::inspection::EditorCanvas::EdgeBrush::AddPiece);

    canvas->setBrushRadius(18);
    paintStroke(canvas, QPoint(255, 125), QPoint(295, 175));

    // Mientras se pinta el trazo SE VE: es la única realimentación de dónde
    // está el pincel y cuánto abarca.
    EXPECT_GT(canvas->correctedPixelCount(), 0) << "la pincelada no marcó nada";

    // Y en cuanto el contorno corregido llega a la pantalla, se retira.
    ASSERT_TRUE(waitFor([&] { return !canvas->edgeCorrectionVisible(); }))
        << "el trazo sigue pintado encima después de haber hecho su trabajo";

    const double areaAfter = std::abs(polygonArea(canvas->liveContour()));
    std::printf("  [trazo] retirado; contorno %.0f -> %.0f px2, %d px corregidos siguen puestos\n",
                areaBefore, areaAfter, canvas->correctedPixelCount());

    // LO IMPORTANTE: retirar el trazo no deshace nada.
    EXPECT_GT(canvas->correctedPixelCount(), 0)
        << "quitar la mancha se llevó la corrección por delante";
    EXPECT_GT(areaAfter, areaBefore)
        << "el contorno volvió al de antes al retirar el trazo";

    // Y que la corrección sigue puesta se DICE, porque ya no se ve.
    QLabel* chip = nullptr;
    chip = window.findChild<QLabel*>(QStringLiteral("edgeChip"));
    if (chip != nullptr && !chip->isVisible()) {
        chip = nullptr;
    }
    ASSERT_NE(chip, nullptr)
        << "la corrección ni se ve ni se anuncia: es estado invisible";
    EXPECT_FALSE(chip->toolTip().isEmpty());
}

// Deshacer y rehacer las pinceladas, y que se note en el CONTORNO — que es lo
// que el operador mira. Deshacer que sólo borre la mancha y deje la corrección
// aplicada sería peor que no tener deshacer.
TEST(EdgeBrush, UndoAndRedoMoveTheContourBackAndForward) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(100, 80, 200, 140), QColor(230, 230, 230));
        painter.fillRect(QRect(250, 120, 50, 60), QColor(22, 22, 22));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("deshacer.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };
    ASSERT_TRUE(waitFor([&] { return canvas->liveContour().size() >= 4; }));
    const double clean = std::abs(polygonArea(canvas->liveContour()));

    EXPECT_FALSE(canvas->canUndoEdgeCorrection()) << "hay algo que deshacer sin haber pintado";

    canvas->setEdgeBrush(pci::inspection::EditorCanvas::EdgeBrush::AddPiece);

    canvas->setBrushRadius(18);
    paintStroke(canvas, QPoint(255, 125), QPoint(295, 175));
    ASSERT_TRUE(waitFor([&] {
        return std::abs(polygonArea(canvas->liveContour())) > clean * 1.01;
    })) << "la pincelada no movió el contorno";
    const double corrected = std::abs(polygonArea(canvas->liveContour()));
    const int correctedPx = canvas->correctedPixelCount();
    ASSERT_TRUE(canvas->canUndoEdgeCorrection());

    // Deshacer: el contorno tiene que volver al de la detección sola.
    ASSERT_TRUE(canvas->undoEdgeCorrection());
    ASSERT_TRUE(waitFor([&] {
        return std::abs(polygonArea(canvas->liveContour())) < clean * 1.005;
    })) << "se deshizo la pincelada y el contorno sigue corregido";
    const double undone = std::abs(polygonArea(canvas->liveContour()));
    EXPECT_EQ(canvas->correctedPixelCount(), 0) << "quedaron restos de la pincelada";

    // Rehacer: y vuelve.
    ASSERT_TRUE(canvas->canRedoEdgeCorrection());
    ASSERT_TRUE(canvas->redoEdgeCorrection());
    ASSERT_TRUE(waitFor([&] {
        return std::abs(polygonArea(canvas->liveContour())) > clean * 1.01;
    })) << "se rehízo la pincelada y el contorno no volvió a corregirse";
    const double redone = std::abs(polygonArea(canvas->liveContour()));

    std::printf("  [deshacer] limpio %.0f -> pintado %.0f -> deshecho %.0f -> rehecho %.0f px2\n",
                clean, corrected, undone, redone);
    EXPECT_NEAR(undone, clean, clean * 0.01) << "deshacer no devolvió el contorno original";
    EXPECT_NEAR(redone, corrected, corrected * 0.01) << "rehacer no reprodujo la corrección";
    EXPECT_EQ(canvas->correctedPixelCount(), correctedPx)
        << "rehacer no restauró los mismos píxeles";
    EXPECT_FALSE(canvas->canRedoEdgeCorrection()) << "queda camino de rehacer tras rehacerlo todo";
}

// «Quitar las correcciones» también se deshace. Es la acción más destructiva
// del pincel, y la única sin vuelta atrás sería justamente la que más la
// necesita.
TEST(EdgeBrush, ClearingEveryCorrectionCanBeUndone) {
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(20);

    const ViewTransform view = viewAt(1.0);
    press(&canvas, toScreen(view, {600.0F, 400.0F}));
    moveTo(&canvas, toScreen(view, {700.0F, 400.0F}));
    release(&canvas, toScreen(view, {700.0F, 400.0F}));
    const int painted = canvas.correctedPixelCount();
    ASSERT_GT(painted, 0);

    canvas.clearEdgeCorrection();
    EXPECT_EQ(canvas.correctedPixelCount(), 0);
    ASSERT_TRUE(canvas.canUndoEdgeCorrection()) << "borrarlo todo no dejó forma de volver";

    ASSERT_TRUE(canvas.undoEdgeCorrection());
    std::printf("  [deshacer] tras borrar todo y deshacer vuelven %d de %d px\n",
                canvas.correctedPixelCount(), painted);
    EXPECT_EQ(canvas.correctedPixelCount(), painted)
        << "deshacer el borrado no devolvió lo que había";
}

// El coste de recordar: si cada paso guardara el frame entero, cincuenta pasos
// a 1920x1080 serían doscientos megas. Se guardan PARCHES, y este test lo mide
// en vez de confiar en el comentario.
TEST(EdgeBrush, RememberingManyStrokesDoesNotEatTheMemory) {
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(12);
    const ViewTransform view = viewAt(1.0);

    for (int stroke = 0; stroke < 60; ++stroke) {
        const float y = 200.0F + static_cast<float>(stroke) * 10.0F;
        press(&canvas, toScreen(view, {1000.0F, y}));
        moveTo(&canvas, toScreen(view, {1060.0F, y}));
        release(&canvas, toScreen(view, {1060.0F, y}));
    }

    // Sesenta trazos con el tope en cincuenta: deshacer cincuenta veces tiene
    // que funcionar, y la cincuenta y una decir que no queda nada.
    int undone = 0;
    while (canvas.undoEdgeCorrection()) {
        ++undone;
        ASSERT_LT(undone, 200) << "deshacer no termina nunca";
    }
    std::printf("  [deshacer] 60 trazos -> %d pasos recordados\n", undone);
    EXPECT_EQ(undone, 50) << "el tope de pasos recordados no se respeta";
}

// UN solo Ctrl+Z, y que haga lo correcto.
//
// La aplicación ya tenía Ctrl+Z para las herramientas dibujadas. Darle al
// pincel su propio atajo obligaría a saber cuál de los dos deshaceres está uno
// usando, y a acertar. La regla es la que espera cualquiera con un pincel en la
// mano: mientras el pincel está activo deshace la pincelada; con el pincel
// apagado, sigue siendo el de las herramientas.
TEST(EdgeBrush, OneUndoThatKnowsWhetherTheBrushIsInYourHand) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(100, 80, 200, 140), QColor(230, 230, 230));
        painter.fillRect(QRect(250, 120, 50, 60), QColor(22, 22, 22));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("unctrlz.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };
    ASSERT_TRUE(waitFor([&] { return canvas->liveContour().size() >= 4; }));

    // Sólo hay un Ctrl+Z en toda la ventana: dos serían un atajo ambiguo, y Qt
    // no dispararía ninguno de los dos.
    int ctrlZ = 0;
    for (auto* action : window.findChildren<QAction*>()) {
        for (const auto& key : action->shortcuts()) {
            if (key == QKeySequence(QKeySequence::Undo)) {
                ++ctrlZ;
            }
        }
    }
    std::printf("  [deshacer] acciones con Ctrl+Z registrado: %d\n", ctrlZ);
    EXPECT_LE(ctrlZ, 1) << "hay más de un Ctrl+Z: Qt no dispara ninguno de los ambiguos";

    QAction* undo = nullptr;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Undo))) {
            undo = action;
        }
    }
    ASSERT_NE(undo, nullptr) << "no hay ninguna acción con Ctrl+Z";

    canvas->setEdgeBrush(pci::inspection::EditorCanvas::EdgeBrush::AddPiece);

    canvas->setBrushRadius(18);
    paintStroke(canvas, QPoint(255, 125), QPoint(295, 175));
    ASSERT_TRUE(waitFor([&] { return canvas->correctedPixelCount() > 0; }));

    // Con el pincel activo, Ctrl+Z se lleva la pincelada.
    undo->trigger();
    QApplication::processEvents();
    EXPECT_EQ(canvas->correctedPixelCount(), 0)
        << "con el pincel en la mano, Ctrl+Z no deshizo la pincelada";

    // Y con el pincel apagado NO toca las correcciones que queden.
    canvas->redoEdgeCorrection();
    QApplication::processEvents();
    const int restored = canvas->correctedPixelCount();
    ASSERT_GT(restored, 0);
    canvas->setEdgeBrush(pci::inspection::EditorCanvas::EdgeBrush::Off);
    undo->trigger();
    QApplication::processEvents();
    std::printf("  [deshacer] con el pincel apagado quedan %d px (había %d)\n",
                canvas->correctedPixelCount(), restored);
    EXPECT_EQ(canvas->correctedPixelCount(), restored)
        << "con el pincel apagado, Ctrl+Z se llevó una corrección que no tocaba";
}

// Cambiar de imagen con pinceladas guardadas.
//
// Los pasos de deshacer guardan un RECTÁNGULO en coordenadas de la imagen sobre
// la que se pintó. Al abrir otra más pequeña, ese rectángulo se sale de la
// máscara nueva — y recortar una `cv::Mat` fuera de sus límites no devuelve
// vacío: lanza. Sin red, deshacer después de cambiar de imagen cerraría la
// aplicación.
//
// La corrección tampoco tiene sentido ya: el análisis descarta las de otro
// tamaño, así que una pila de pasos que no se pueden aplicar es historia muerta
// que sólo sirve para romper algo.
TEST(EdgeBrush, ChangingTheImageDoesNotLeaveUndoStepsThatCrash) {
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(20);

    const ViewTransform view = viewAt(1.0);
    press(&canvas, toScreen(view, {1500.0F, 900.0F}));
    moveTo(&canvas, toScreen(view, {1700.0F, 1000.0F}));
    release(&canvas, toScreen(view, {1700.0F, 1000.0F}));
    ASSERT_GT(canvas.correctedPixelCount(), 0);
    ASSERT_TRUE(canvas.canUndoEdgeCorrection());

    // Llega una imagen MUCHO más pequeña, como al abrir otro fichero.
    QImage small(320, 240, QImage::Format_RGB888);
    small.fill(QColor(40, 40, 40));
    canvas.setFrame(small);

    std::printf("  [cambio] tras cambiar a 320x240 quedan %d px y %s pasos\n",
                canvas.correctedPixelCount(),
                canvas.canUndoEdgeCorrection() ? "algunos" : "cero");

    // Ni corrección de la imagen anterior, ni pasos que la reconstruyan.
    EXPECT_EQ(canvas.correctedPixelCount(), 0)
        << "la corrección de la imagen anterior sigue puesta sobre la nueva";
    EXPECT_FALSE(canvas.canUndoEdgeCorrection())
        << "quedan pasos con coordenadas de una imagen que ya no está";
    EXPECT_FALSE(canvas.canRedoEdgeCorrection());

    // Y aunque se pidan, no revientan.
    EXPECT_FALSE(canvas.undoEdgeCorrection());
    EXPECT_FALSE(canvas.redoEdgeCorrection());

    // El pincel sigue usable sobre la imagen nueva.
    press(&canvas, toScreen(ViewTransform({320, 240}, {kWidgetWidth, kWidgetHeight}, 1.0,
                                          {0.0, 0.0}),
                            {160.0F, 120.0F}));
    release(&canvas, toScreen(ViewTransform({320, 240}, {kWidgetWidth, kWidgetHeight}, 1.0,
                                            {0.0, 0.0}),
                              {160.0F, 120.0F}));
    EXPECT_GT(canvas.correctedPixelCount(), 0) << "el pincel dejó de funcionar tras el cambio";
}

// Corregir una imagen y abrir otra: ni la corrección ni su aviso pueden
// sobrevivir al cambio. El análisis descarta las correcciones de otro tamaño,
// así que un aviso que siguiera diciendo «Borde corregido» estaría mintiendo
// sobre lo que se está viendo.
TEST(EdgeBrush, OpeningAnotherFileLeavesNoCorrectionBehind) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    QImage first(400, 300, QImage::Format_RGB888);
    first.fill(QColor(20, 20, 20));
    {
        QPainter painter(&first);
        painter.fillRect(QRect(100, 80, 200, 140), QColor(230, 230, 230));
        painter.fillRect(QRect(250, 120, 50, 60), QColor(22, 22, 22));
    }
    const QString firstPath = QDir(dir.path()).filePath(QStringLiteral("primera.png"));
    ASSERT_TRUE(first.save(firstPath));

    QImage second(260, 200, QImage::Format_RGB888);  // OTRO tamaño a propósito
    second.fill(QColor(20, 20, 20));
    {
        QPainter painter(&second);
        painter.fillRect(QRect(60, 50, 140, 100), QColor(230, 230, 230));
    }
    const QString secondPath = QDir(dir.path()).filePath(QStringLiteral("segunda.png"));
    ASSERT_TRUE(second.save(secondPath));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, firstPath));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };
    ASSERT_TRUE(waitFor([&] { return canvas->liveContour().size() >= 4; }));

    canvas->setEdgeBrush(pci::inspection::EditorCanvas::EdgeBrush::AddPiece);

    canvas->setBrushRadius(18);
    paintStroke(canvas, QPoint(255, 125), QPoint(295, 175));
    ASSERT_TRUE(waitFor([&] { return canvas->correctedPixelCount() > 0; }));

    const auto correctionChip = [&]() -> QLabel* {
        auto* found = window.findChild<QLabel*>(QStringLiteral("edgeChip"));
        return found != nullptr && found->isVisible() ? found : nullptr;
    };
    ASSERT_TRUE(waitFor([&] { return correctionChip() != nullptr; }))
        << "no se anuncia la corrección sobre la primera imagen";

    // Y ahora se abre otra, de otro tamaño.
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, secondPath));
    ASSERT_TRUE(waitFor([&] { return canvas->imageSize() == QSize(260, 200); }))
        << "la segunda imagen no llegó a mostrarse";

    std::printf("  [cambio] tras abrir la segunda: %d px corregidos, aviso %s\n",
                canvas->correctedPixelCount(),
                correctionChip() != nullptr ? "VISIBLE" : "retirado");

    EXPECT_EQ(canvas->correctedPixelCount(), 0)
        << "la corrección de la primera imagen sigue viva sobre la segunda";
    EXPECT_FALSE(canvas->canUndoEdgeCorrection())
        << "quedan pasos con coordenadas de la imagen anterior";
    ASSERT_TRUE(waitFor([&] { return correctionChip() == nullptr; }))
        << "el aviso sigue diciendo «Borde corregido» sobre una imagen sin corregir";

    // Y se puede corregir la nueva sin que nada de lo anterior estorbe.
    canvas->setEdgeBrush(pci::inspection::EditorCanvas::EdgeBrush::AddPiece);
    canvas->setBrushRadius(12);
    paintStroke(canvas, QPoint(130, 100), QPoint(160, 120));
    EXPECT_GT(canvas->correctedPixelCount(), 0)
        << "el pincel dejó de funcionar tras cambiar de fichero";
}

// El control de vídeo, de punta a punta y a través de la VENTANA: abrir el
// fichero, pausar con el botón, mover la barra, y comprobar que la posición que
// se lee cuadra con donde se pidió ir.
//
// Se reportó como «la barra no responde», «va a saltos» y «la posición no
// cuadra», y hasta ahora sólo estaba probada la fuente por dentro. Aviso: el
// material es un AVI corto generado aquí; sobre un MP4 largo de verdad el
// comportamiento del códec puede ser otro, y eso sigue sin cubrir.
TEST(VideoTransportEndToEnd, PausingStopsItAndSeekingLandsWhereItWasAsked) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("transporte.avi"));
    {
        cv::VideoWriter writer(path.toStdString(),
                               cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 25.0,
                               cv::Size(320, 240));
        if (!writer.isOpened()) {
            GTEST_SKIP() << "sin códec de escritura de vídeo en esta máquina";
        }
        // 250 frames = 10 segundos. Cada uno lleva su número pintado como una
        // barra que crece, para que se pueda ver por dónde va.
        for (int i = 0; i < 250; ++i) {
            cv::Mat frame(240, 320, CV_8UC3, cv::Scalar(20, 20, 20));
            cv::rectangle(frame, cv::Rect(20, 20, 10 + i, 60), cv::Scalar(230, 230, 230),
                          cv::FILLED);
            writer.write(frame);
        }
    }

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Video, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    const auto waitFor = [](auto predicate, int ms = 6000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };
    ASSERT_TRUE(waitFor([&] { return canvas->imageSize() == QSize(320, 240); }))
        << "el vídeo no llegó a mostrarse";

    // La barra de transporte tiene que estar VISIBLE con un vídeo abierto, y la
    // pausa también. Los dos por nombre: el botón CAMBIA de rótulo —«Pausa» y
    // «Seguir»— así que buscarlo por texto obligaba a probar los dos, y el día
    // que ninguno encajara el test fallaría diciendo «no hay botón de pausa»
    // cuando lo que pasó fue que se reescribió el rótulo.
    auto* bar = window.findChild<QAbstractSlider*>(QStringLiteral("videoSlider"));
    ASSERT_NE(bar, nullptr) << "no hay barra de posición con un vídeo abierto";
    EXPECT_TRUE(bar->isVisible()) << "la barra de posición está oculta con un vídeo abierto";

    auto* playPause = window.findChild<QAbstractButton*>(QStringLiteral("playPauseButton"));
    ASSERT_NE(playPause, nullptr) << "no hay botón de pausa";
    EXPECT_TRUE(playPause->isVisible()) << "el botón de pausa está oculto";

    // 1) Pausar PARA de verdad: la barra deja de moverse.
    ASSERT_TRUE(waitFor([&] { return bar->value() > bar->minimum(); }))
        << "el vídeo no avanza: la barra no se mueve sola";
    playPause->click();
    QTest::qWait(250);
    const int settled = bar->value();
    QTest::qWait(500);
    std::printf("  [vídeo] en pausa la barra pasó de %d a %d\n", settled, bar->value());
    EXPECT_EQ(bar->value(), settled) << "en pausa la barra sigue corriendo";
    EXPECT_EQ(playPause->text(), QStringLiteral("Seguir"))
        << "el botón no dice cómo salir de la pausa";

    // 2) Y pausado, el pincel se deja usar: es justo el frame que uno buscó.
    QToolButton* brush = nullptr;
    brush = window.findChild<QToolButton*>(QStringLiteral("edgeBrushButton"));
    ASSERT_NE(brush, nullptr);
    EXPECT_TRUE(brush->isEnabled())
        << "vídeo en pausa y el pincel apagado. Dice: " << brush->toolTip().toStdString();

    // 3) Buscar: se pide el 70 % y se comprueba dónde dice que está.
    const int target = bar->minimum() + (bar->maximum() - bar->minimum()) * 7 / 10;
    bar->setValue(target);
    emit bar->sliderReleased();
    const double asked = 100.0 * (target - bar->minimum()) /
                         static_cast<double>(bar->maximum() - bar->minimum());
    ASSERT_TRUE(waitFor([&] {
        const double at = 100.0 * (bar->value() - bar->minimum()) /
                          static_cast<double>(bar->maximum() - bar->minimum());
        return std::abs(at - asked) < 12.0;
    })) << "se pidió el 70 % y la barra se quedó en otro sitio";

    const double landed = 100.0 * (bar->value() - bar->minimum()) /
                          static_cast<double>(bar->maximum() - bar->minimum());
    std::printf("  [vídeo] pedido %.0f %%, aterrizó en %.0f %% (desvío %.1f puntos)\n", asked,
                landed, std::abs(landed - asked));
    EXPECT_LT(std::abs(landed - asked), 12.0)
        << "la posición que muestra la barra no cuadra con donde se pidió ir";
}

// La zona sobrevive al cierre del programa, y hasta ahora sobrevivía MAL.
//
// Se guardaba en píxeles y no a qué resolución se dibujó. Al reabrir con una
// fuente de otro tamaño se aplicaba tal cual: recortada contra el frame, la
// zona se queda en un trozo que nadie eligió, o desaparece entera y se analiza
// toda la imagen. Las dos cosas en silencio, que es lo peor de las dos.
//
// Dentro de la misma sesión esto ya se corregía al cambiar de resolución. Lo
// que faltaba era el arranque, donde no hay resolución anterior con la que
// comparar porque el programa acaba de abrirse.
TEST(WorkingZoneEndToEnd, AZoneDrawnAtOneResolutionSurvivesAReopenAtAnother) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    // Dos versiones de la MISMA escena, una al doble que la otra: la zona bien
    // reajustada tiene que seguir rodeando la misma pieza.
    const auto sceneOfSize = [](int width, int height) {
        QImage photo(width, height, QImage::Format_RGB888);
        photo.fill(QColor(20, 20, 20));
        QPainter painter(&photo);
        const double sx = width / 480.0;
        const double sy = height / 320.0;
        painter.fillRect(QRect(qRound(40 * sx), qRound(60 * sy), qRound(180 * sx),
                               qRound(200 * sy)),
                         QColor(230, 230, 230));  // grande
        painter.fillRect(QRect(qRound(330 * sx), qRound(130 * sy), qRound(80 * sx),
                               qRound(60 * sy)),
                         QColor(230, 230, 230));  // pequeña
        return photo;
    };
    const QString bigPath = QDir(dir.path()).filePath(QStringLiteral("grande.png"));
    const QString smallPath = QDir(dir.path()).filePath(QStringLiteral("mitad.png"));
    ASSERT_TRUE(sceneOfSize(480, 320).save(bigPath));
    ASSERT_TRUE(sceneOfSize(240, 160).save(smallPath));

    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };

    // Las dos sesiones comparten una base de datos de verdad: con una ventana
    // sin repositorios no se persiste nada, y el test estaría comprobando que
    // no hay zona en lugar de que la zona heredada se reajusta.
    const std::string dbPath = QDir(dir.path()).filePath(QStringLiteral("s.db")).toStdString();
    auto opened = pci::database::Db::open(dbPath);
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::SettingsRepository settings(*db);
    pci::ui::AppRepositories repos;
    repos.settings = &settings;

    // --- Sesión 1: se dibuja la zona alrededor de la pieza pequeña ----------
    {
        pci::ui::MainWindow window(repos);
        window.resize(1200, 800);
        window.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
        ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, bigPath));

        auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
        ASSERT_NE(canvas, nullptr);
        ASSERT_TRUE(waitFor([&] { return canvas->liveContour().size() >= 4; }));

        const ViewTransform view({canvas->imageSize().width(), canvas->imageSize().height()},
                                 {canvas->width(), canvas->height()}, 1.0, {0.0, 0.0});
        const auto screen = [&](double x, double y) {
            const cv::Point2d q = view.imageToWidget(
                cv::Point2f(static_cast<float>(x), static_cast<float>(y)));
            return QPointF(q.x, q.y);
        };
        canvas->setFreeZonePickMode(true);
        const std::vector<QPointF> corners{{300, 105}, {440, 105}, {440, 215}, {300, 215}};
        press(canvas, screen(corners.front().x(), corners.front().y()));
        for (std::size_t i = 1; i <= corners.size(); ++i) {
            const QPointF from = corners[i - 1];
            const QPointF to = corners[i % corners.size()];
            for (int step = 1; step <= 20; ++step) {
                const double t = static_cast<double>(step) / 20.0;
                moveTo(canvas, screen(from.x() + (to.x() - from.x()) * t,
                                      from.y() + (to.y() - from.y()) * t));
            }
        }
        release(canvas, screen(corners.front().x(), corners.front().y()));

        ASSERT_TRUE(waitFor([&] {
            const QRectF box = canvas->liveContour().boundingRect();
            return box.width() > 1.0 && box.left() > 250.0;
        })) << "la zona no llegó a aplicarse en la primera sesión";
        std::printf("  [zona] sesión 1 (480x320): mide en x=%.0f, ancho %.0f\n",
                    canvas->liveContour().boundingRect().left(),
                    canvas->liveContour().boundingRect().width());
    }

    // --- Sesión 2: otra ventana, otra fuente, LA MITAD de tamaño ------------
    {
        pci::ui::MainWindow window(repos);
        window.resize(1200, 800);
        window.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
        ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, smallPath));

        auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
        ASSERT_NE(canvas, nullptr);
        ASSERT_TRUE(waitFor([&] { return canvas->liveContour().size() >= 4; }))
            << "con la zona heredada no se detecta ninguna pieza: la zona quedó "
               "señalando un sitio donde no hay nada";

        const QRectF box = canvas->liveContour().boundingRect();
        std::printf("  [zona] sesión 2 (240x160): mide en x=%.0f, ancho %.0f\n", box.left(),
                    box.width());

        // La zona reajustada rodea la MISMA pieza, ahora a la mitad de escala:
        // la pequeña está sobre x=165 y mide unos 40 px de ancho.
        EXPECT_GT(box.left(), 140.0)
            << "la zona heredada dejó de rodear la pieza pequeña: se está midiendo la grande";
        EXPECT_LT(box.width(), 70.0)
            << "el contorno es demasiado ancho para ser la pieza pequeña a media escala";
    }
}

// El cero del tablero es un PUNTO en coordenadas de imagen, igual de dependiente
// de la resolución que la zona — y quien tuviera puesto un cero y ninguna zona
// sufría el mismo fallo sin que nada lo cubriera: el origen de todas las
// medidas de Posición, corrido y sin avisar.
//
// Se comprueba sobre el ajuste guardado, que es donde vive la verdad: si el
// número que queda en la base de datos no se ha movido, el cero está mal.
TEST(WorkingZoneEndToEnd, TheBoardZeroAlsoFollowsAChangeOfResolution) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage half(240, 160, QImage::Format_RGB888);
    half.fill(QColor(20, 20, 20));
    {
        QPainter painter(&half);
        painter.fillRect(QRect(60, 40, 80, 60), QColor(230, 230, 230));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("mitad.png"));
    ASSERT_TRUE(half.save(path));

    const std::string dbPath = QDir(dir.path()).filePath(QStringLiteral("b.db")).toStdString();
    auto opened = pci::database::Db::open(dbPath);
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::SettingsRepository settings(*db);

    // Una sesión anterior dejó un cero a mano en (300, 150) sobre 480x320, y
    // ninguna zona: es el caso que no cubría nada.
    ASSERT_TRUE(settings.setString("board_origin", "fixed").isOk());
    ASSERT_TRUE(settings.setDouble("board_fixed_x", 300.0).isOk());
    ASSERT_TRUE(settings.setDouble("board_fixed_y", 150.0).isOk());
    ASSERT_TRUE(settings.setInt("det_zone_ref_w", 480).isOk());
    ASSERT_TRUE(settings.setInt("det_zone_ref_h", 320).isOk());

    pci::ui::AppRepositories repos;
    repos.settings = &settings;

    pci::ui::MainWindow window(repos);
    window.resize(1000, 700);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 4000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (canvas->imageSize() == QSize(240, 160)) {
            break;
        }
    }
    ASSERT_EQ(canvas->imageSize(), QSize(240, 160)) << "la imagen no llegó a mostrarse";

    // Y se espera a que el reajuste quede guardado.
    const auto storedX = [&] { return settings.getDouble("board_fixed_x", -1.0).value(); };
    timer.restart();
    while (timer.elapsed() < 3000 && storedX() > 200.0) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    const double x = storedX();
    const double y = settings.getDouble("board_fixed_y", -1.0).value();
    std::printf("  [tablero] cero (300,150) sobre 480x320 -> (%.0f,%.0f) sobre 240x160\n", x, y);

    // La mitad de tamaño, la mitad de coordenadas: el cero sigue en el mismo
    // sitio de la escena.
    EXPECT_NEAR(x, 150.0, 2.0) << "el cero del tablero no siguió al cambio de resolución";
    EXPECT_NEAR(y, 75.0, 2.0) << "el cero del tablero no siguió al cambio de resolución";

    // Y la referencia queda apuntando a la resolución NUEVA: si no, el próximo
    // arranque volvería a reajustar desde la vieja y lo movería otra vez.
    EXPECT_EQ(settings.getInt("det_zone_ref_w", 0).value(), 240)
        << "la referencia se quedó en la resolución anterior: al próximo arranque "
           "el cero se movería una segunda vez";
    EXPECT_EQ(settings.getInt("det_zone_ref_h", 0).value(), 160);
}

// Aprender de una captura: la última pieza que le faltaba a la tira.
//
// La visión del proyecto dice «actualizar la referencia estadística tras cada
// pieza buena, nunca reentrenar», y eso sólo se podía hacer desde el diálogo de
// una inspección recién corrida. Las fotos que uno guarda durante la puesta a
// punto —que son precisamente las buenas, elegidas a mano— no servían para
// nada.
//
// Lo que este test fija no es que el botón exista, sino CUÁNDO se deja pulsar y
// que diga por qué cuando no. Una referencia contaminada con piezas malas no
// falla ruidosamente: falla dejando pasar defectos, y nadie lo nota hasta que
// llega una reclamación. Por eso aprender no puede ser nunca un accidente.
TEST(CaptureTrayLearning, LearningIsNeverAvailableByAccident) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(120, 90, 160, 120), QColor(230, 230, 230));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("captura.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));

    QPushButton* learn = nullptr;
    learn = window.findChild<QPushButton*>(QStringLiteral("learnFromCaptureButton"));
    ASSERT_NE(learn, nullptr) << "no está el botón de aprender de una captura";

    // Sin foto elegida no se puede, y se dice.
    EXPECT_FALSE(learn->isEnabled());
    EXPECT_FALSE(learn->toolTip().isEmpty())
        << "apagado y mudo: un botón muerto sin motivo se lee como aplicación rota";
    const QString reasonWithoutCapture = learn->toolTip();
    std::printf("  [aprender] sin foto dice: «%s»\n",
                reasonWithoutCapture.left(60).toStdString().c_str());

    // Se toma una foto de verdad, abriendo una imagen y capturándola.
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));
    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return predicate();
    };
    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    ASSERT_TRUE(waitFor([&] { return canvas->imageSize() == QSize(400, 300); }));

    QListWidget* tray = nullptr;
    for (auto* list : window.findChildren<QListWidget*>()) {
        if (list->viewMode() == QListView::IconMode) {
            tray = list;
        }
    }
    ASSERT_NE(tray, nullptr) << "no está la tira de capturas";

    // Sin pieza elegida y sin modelo, el motivo tiene que CAMBIAR: tres razones
    // distintas piden tres arreglos distintos, y un texto único los escondería.
    QApplication::processEvents();
    EXPECT_FALSE(learn->isEnabled())
        << "se puede aprender sin haber elegido de qué pieza es la referencia";
    std::printf("  [aprender] con imagen abierta dice: «%s»\n",
                learn->toolTip().left(60).toStdString().c_str());
    EXPECT_FALSE(learn->toolTip().isEmpty());
}

// El botón no puede quedarse enganchado a un estado viejo: es exactamente el
// fallo que costó tres rondas con el pincel —una disponibilidad calculada una
// sola vez, y nunca más—, así que aquí se comprueba que se RECALCULA.
TEST(CaptureTrayLearning, TheReasonIsRecomputedAndNotFrozen) {
    pci::ui::MainWindow window;
    window.resize(1000, 700);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));

    QPushButton* learn = nullptr;
    learn = window.findChild<QPushButton*>(QStringLiteral("learnFromCaptureButton"));
    ASSERT_NE(learn, nullptr);
    const QString before = learn->toolTip();

    QListWidget* tray = nullptr;
    for (auto* list : window.findChildren<QListWidget*>()) {
        if (list->viewMode() == QListView::IconMode) {
            tray = list;
        }
    }
    ASSERT_NE(tray, nullptr);

    // Cambiar la selección de la tira tiene que volver a preguntarse el motivo.
    // Con la tira vacía la selección no puede cambiar, así que se fuerza la
    // señal que la ventana escucha.
    emit tray->currentRowChanged(-1);
    QApplication::processEvents();

    std::printf("  [aprender] el motivo %s tras mover la selección\n",
                learn->toolTip() == before ? "se mantiene (coherente)" : "cambió");
    EXPECT_FALSE(learn->toolTip().isEmpty())
        << "tras mover la selección el botón se quedó sin explicación";
    EXPECT_FALSE(learn->isEnabled());
}

// ---------------------------------------------------------------------------
// El afinado subpíxel, al alcance del operador
// ---------------------------------------------------------------------------

// Una mejora de precisión que sólo se puede encender desde un banco de línea de
// comandos no está entregada: el operador no tiene forma de llegar a ella.
//
// Y tiene que nacer APAGADA, porque encenderla cambia dónde está el borde y con
// él todas las cotas de la pieza. Esa decisión es del operador, con la
// consecuencia dicha; lo que no puede es tomarse sola.
TEST(SubpixelSetting, TheOperatorCanReachItAndItStartsOff) {
    pci::vision::SegmentationOptions options;
    pci::ui::DetectionPage page(options);

    auto* box = page.findChild<QCheckBox*>(QStringLiteral("subpixelCheck"));
    ASSERT_NE(box, nullptr)
        << "no hay forma de encender el afinado subpíxel desde la aplicación";

    EXPECT_FALSE(box->isChecked())
        << "nace encendida: cambiaría las cotas de todas las piezas ya registradas "
           "sin que nadie lo hubiera pedido";
    EXPECT_FALSE(page.subpixelEdges());

    // Y la advertencia va DONDE SE VE, no escondida: quien la encienda tiene que
    // enterarse de que sus tolerancias dejan de valer.
    const QString help = box->toolTip();
    EXPECT_FALSE(help.isEmpty());
    EXPECT_TRUE(help.contains(QStringLiteral("tolerancias")))
        << "la ayuda no avisa de que hay que revisar las tolerancias. Dice: "
        << help.toStdString();

    box->setChecked(true);
    EXPECT_TRUE(page.subpixelEdges()) << "marcarla no cambia lo que la página devuelve";
}

// El estado con el que se abre la página tiene que ser el que hay. Una casilla
// que siempre nace apagada mentiría en cuanto alguien la encendiera: diría
// «apagado» con el afinado funcionando.
TEST(SubpixelSetting, ThePageOpensShowingTheStateThatIsActuallyInUse) {
    pci::vision::SegmentationOptions options;
    pci::ui::DetectionPage on(options, nullptr, nullptr, 0, 0.005, 0.9, true);
    EXPECT_TRUE(on.subpixelEdges())
        << "se abrió con el afinado encendido y la página dice que está apagado";

    pci::ui::DetectionPage off(options, nullptr, nullptr, 0, 0.005, 0.9, false);
    EXPECT_FALSE(off.subpixelEdges());
}

// ---------------------------------------------------------------------------
// Los avisos viajan CON el informe
// ---------------------------------------------------------------------------

// Un aviso que se queda en la barra de estado llega a la mitad de la gente que
// lo necesita: la otra mitad exporta el CSV y se lleva las cifras sin él. Por
// eso «esta pieza está cortada» y «este perímetro no es de fiar» son parte del
// informe, no de la ventana que lo enseña.
TEST(PieceReportWarnings, ACutPieceSaysSoInsideTheReport) {
    // Una pieza que se sale por la derecha del encuadre.
    cv::Mat gray(300, 400, CV_8UC1, cv::Scalar(20));
    cv::rectangle(gray, cv::Rect(250, 80, 200, 140), cv::Scalar(220), cv::FILLED);
    cv::Mat mask;
    cv::threshold(gray, mask, 128, 255, cv::THRESH_BINARY);

    const auto cut = pci::inspection::measureWholePiece(gray, mask, {}, 0.0,
                                                        pci::inspection::LengthUnit::Auto,
                                                        gray.size());
    ASSERT_TRUE(cut.ok) << cut.problem;
    ASSERT_FALSE(cut.warnings.empty())
        << "la pieza se sale del encuadre y el informe no lo dice";
    std::printf("  [informe] %s\n", cut.warnings.front().c_str());
    EXPECT_NE(cut.warnings.front().find("limites inferiores"), std::string::npos)
        << "avisa de que toca el borde pero no de que las medidas se quedan cortas";

    // La misma pieza con margen por los cuatro lados: sin aviso.
    cv::Mat whole(300, 400, CV_8UC1, cv::Scalar(20));
    cv::rectangle(whole, cv::Rect(100, 80, 200, 140), cv::Scalar(220), cv::FILLED);
    cv::Mat wholeMask;
    cv::threshold(whole, wholeMask, 128, 255, cv::THRESH_BINARY);
    const auto fine = pci::inspection::measureWholePiece(whole, wholeMask, {}, 0.0,
                                                          pci::inspection::LengthUnit::Auto,
                                                          whole.size());
    ASSERT_TRUE(fine.ok) << fine.problem;
    EXPECT_TRUE(fine.warnings.empty())
        << "avisa de una pieza que entra entera: un aviso que salta siempre se "
           "aprende a ignorar. Dice: "
        << (fine.warnings.empty() ? std::string() : fine.warnings.front());

    // Y sin encuadre no se puede comprobar, así que no se inventa el aviso.
    const auto blind = pci::inspection::measureWholePiece(gray, mask, {}, 0.0,
                                                          pci::inspection::LengthUnit::Auto);
    ASSERT_TRUE(blind.ok);
    EXPECT_TRUE(blind.warnings.empty())
        << "sin saber el tamaño del encuadre no se puede afirmar que la pieza esté "
           "cortada, y afirmarlo igual seria inventar";
}

// Y el diálogo los enseña ANTES que las cifras. Un aviso que dice «estas
// medidas son límites inferiores» puesto al final llega cuando ya se han leído
// las cifras, y entonces no cambia nada.
TEST(PieceReportWarnings, TheDialogShowsThemBeforeTheNumbers) {
    cv::Mat gray(300, 400, CV_8UC1, cv::Scalar(20));
    cv::rectangle(gray, cv::Rect(250, 80, 200, 140), cv::Scalar(220), cv::FILLED);
    cv::Mat mask;
    cv::threshold(gray, mask, 128, 255, cv::THRESH_BINARY);
    const auto report = pci::inspection::measureWholePiece(
        gray, mask, {}, 0.0, pci::inspection::LengthUnit::Auto, gray.size());
    ASSERT_TRUE(report.ok);
    ASSERT_FALSE(report.warnings.empty());

    pci::ui::PieceReportDialog dialog(report, QStringLiteral("una imagen"), nullptr);
    dialog.resize(700, 600);
    dialog.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&dialog));

    // El aviso tiene que estar en pantalla, entero y visible. Se localiza por su
    // nombre y se comprueba QUÉ DICE, que es lo que este test afirma: buscarlo
    // por su texto daba por buena la mitad que había que comprobar, y ese texto
    // lo escribe la medición, no el diálogo.
    const auto warnings = dialog.findChildren<QLabel*>(QStringLiteral("reportWarning"));
    ASSERT_FALSE(warnings.isEmpty())
        << "el informe trae avisos y el diálogo no enseña ninguno";
    QLabel* shown = warnings.front();
    EXPECT_TRUE(shown->text().contains(QStringLiteral("limites inferiores")))
        << "el primer aviso no es el de la pieza cortada: «"
        << shown->text().toStdString() << "»";
    EXPECT_TRUE(shown->isVisible());
    EXPECT_TRUE(shown->wordWrap()) << "el aviso se corta en vez de leerse entero";

    // Y por encima de la tabla de cifras.
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    const int warningY = shown->mapTo(&dialog, QPoint(0, 0)).y();
    const int tableY = table->mapTo(&dialog, QPoint(0, 0)).y();
    std::printf("  [informe] aviso en y=%d, tabla en y=%d\n", warningY, tableY);
    EXPECT_LT(warningY, tableY)
        << "el aviso sale debajo de las cifras: para entonces ya se han leido";
}


// ---------------------------------------------------------------------------
// Los cuatro fallos del pincel, y las tres ayudas
// ---------------------------------------------------------------------------
//
// Todo lo que sigue nace de una queja de uso: «el tamaño de los pinceles
// desaparece después de usarlos, aparte es muy dispareja la línea que da como
// resultado». Al ir a buscarlo no había UN fallo: había cuatro causas
// independientes que producían el mismo síntoma, y cada una necesita su
// comprobación o volverá por su cuenta.

// CAUSA 1: `setEdgeBrush(mode, radiusPx = 12)` llevaba el tamaño como parámetro
// con valor por defecto, y la ventana lo llamaba sin él. Encender el pincel
// pisaba en silencio el tamaño que el operador acababa de elegir.
TEST(EdgeBrushSize, TheSizeSurvivesTurningTheBrushOffAndOn) {
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});

    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(40);
    ASSERT_EQ(canvas.brushRadius(), 40);

    // Lo que hace la ventana cada vez que se toca el botón del pincel.
    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::Off);
    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::RemovePiece);
    EXPECT_EQ(canvas.brushRadius(), 40)
        << "encender el pincel volvió a reiniciar el tamaño: el fallo ha vuelto";

    // Y tampoco lo pierde al pintar.
    const ViewTransform view = viewAt(1.0);
    drag(&canvas, toScreen(view, {600.0F, 400.0F}), toScreen(view, {700.0F, 400.0F}));
    EXPECT_EQ(canvas.brushRadius(), 40) << "el tamaño se perdió al usar el pincel";
}

// CAUSA 2: el anillo que enseña el tamaño se dibujaba DENTRO de
// `paintEdgeCorrection`, detrás del mismo `return` que retira la mancha del
// trazo cuando ya ha hecho su trabajo. Al soltar la primera pincelada
// desaparecía la mancha (querido) y con ella el indicador de tamaño (no
// querido). Esta es la causa que coincide literalmente con la queja.
TEST(EdgeBrushSize, TheRingIsStillDrawnAfterTheStrokeIsHidden) {
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(30);

    // Con el pincel encendido hay que seguir al ratón, o el anillo solo se
    // movería con el botón pulsado: para saber qué tamaño tienes, pintar.
    EXPECT_TRUE(canvas.hasMouseTracking())
        << "el pincel no sigue al cursor: el anillo se queda congelado";

    const ViewTransform view = viewAt(1.0);
    drag(&canvas, toScreen(view, {600.0F, 400.0F}), toScreen(view, {700.0F, 400.0F}));
    // Justo lo que hace la ventana al terminar de analizar la corrección.
    canvas.setEdgeCorrectionVisible(false);
    ASSERT_FALSE(canvas.edgeCorrectionVisible());

    // El cursor, quieto sobre el lienzo.
    moveTo(&canvas, toScreen(view, {800.0F, 500.0F}));

    const auto greenPixels = [&canvas] {
        QImage shot(canvas.size(), QImage::Format_RGB888);
        canvas.render(&shot);
        int count = 0;
        for (int y = 0; y < shot.height(); ++y) {
            for (int x = 0; x < shot.width(); ++x) {
                const QColor c = shot.pixelColor(x, y);
                // El verde del pincel (0,210,90), con holgura por el suavizado.
                if (c.green() > 150 && c.red() < 110 && c.blue() < 150 &&
                    c.green() - c.red() > 80) {
                    ++count;
                }
            }
        }
        return count;
    };

    const int withBrush = greenPixels();
    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::Off);
    const int withoutBrush = greenPixels();
    std::printf("  [pincel] verde en pantalla: %d con pincel, %d sin pincel\n", withBrush,
                withoutBrush);
    EXPECT_GT(withBrush, 40)
        << "con la mancha oculta ya no se dibuja nada del pincel: el tamaño ha vuelto a "
           "desaparecer justo al usarlo, que es la queja original";
    EXPECT_LT(withoutBrush, withBrush / 4)
        << "sigue pintándose el anillo con el pincel apagado";
}

// AYUDA 1: TRAZO RECTO. El trazo va del principio al final en línea recta, y el
// rodeo que dio la mano por el camino no cuenta.
TEST(EdgeBrushAssist, AStraightStrokeIgnoresTheDetourTheHandTook) {
    const ViewTransform view = viewAt(1.0);
    const auto strokeWithDetour = [&view](bool straight) {
        EditorCanvas canvas;
        canvas.resize(kWidgetWidth, kWidgetHeight);
        canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
        canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
        canvas.setBrushRadius(15);
        canvas.setBrushStraight(straight);
        canvas.setBrushSteady(false);  // aquí se mide el camino, no el pulso
        canvas.setBrushSnap(false);    // ni el ceñido

        cv::Mat painted;
        QObject::connect(&canvas, &EditorCanvas::edgeCorrected,
                         [&](const cv::Mat& add, const cv::Mat&) { painted = add.clone(); });
        press(&canvas, toScreen(view, {500.0F, 400.0F}));
        // El rodeo: la mano se va muy abajo y vuelve.
        moveTo(&canvas, toScreen(view, {600.0F, 700.0F}));
        moveTo(&canvas, toScreen(view, {700.0F, 700.0F}));
        moveTo(&canvas, toScreen(view, {800.0F, 400.0F}));
        release(&canvas, toScreen(view, {800.0F, 400.0F}));
        return painted;
    };

    const cv::Mat freehand = strokeWithDetour(false);
    const cv::Mat straight = strokeWithDetour(true);
    ASSERT_FALSE(freehand.empty());
    ASSERT_FALSE(straight.empty());

    // A mano alzada, el rodeo se pinta.
    EXPECT_GT(freehand.at<unsigned char>(700, 650), 0)
        << "sin la ayuda, el trazo debería haber pasado por el rodeo";
    // Recto, no.
    EXPECT_EQ(straight.at<unsigned char>(700, 650), 0)
        << "con «trazo recto» el rodeo de la mano sigue pintándose";
    // Y la recta entre los extremos sí, entera.
    for (int x = 520; x <= 780; x += 20) {
        EXPECT_GT(straight.at<unsigned char>(400, x), 0)
            << "el trazo recto tiene un hueco en x=" << x;
    }
    std::printf("  [pincel] recto: %d px marcados; a mano alzada con rodeo: %d px\n",
                cv::countNonZero(straight), cv::countNonZero(freehand));
}

// AYUDA 2: PULSO ESTABLE. Lo que se filtra es el temblor, no la intención.
TEST(EdgeBrushAssist, ASteadyHandSmoothsTheWobbleOutOfTheStroke) {
    const ViewTransform view = viewAt(1.0);
    const auto wobblyStroke = [&view](bool steady) {
        EditorCanvas canvas;
        canvas.resize(kWidgetWidth, kWidgetHeight);
        canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
        canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
        canvas.setBrushRadius(6);
        canvas.setBrushSteady(steady);
        canvas.setBrushSnap(false);

        cv::Mat painted;
        QObject::connect(&canvas, &EditorCanvas::edgeCorrected,
                         [&](const cv::Mat& add, const cv::Mat&) { painted = add.clone(); });
        press(&canvas, toScreen(view, {400.0F, 400.0F}));
        // Zigzag: la mano tiembla 14 px arriba y abajo mientras avanza.
        for (int step = 1; step <= 30; ++step) {
            const float x = 400.0F + static_cast<float>(step) * 10.0F;
            const float y = 400.0F + ((step % 2 == 0) ? 14.0F : -14.0F);
            moveTo(&canvas, toScreen(view, {x, y}));
        }
        release(&canvas, toScreen(view, {700.0F, 400.0F}));
        return painted;
    };

    // Desviación de la línea central del trazo respecto a y = 400.
    const auto wobbleOf = [](const cv::Mat& mask) {
        double sum = 0.0;
        int columns = 0;
        for (int x = 420; x < 690; ++x) {
            double weighted = 0.0;
            double total = 0.0;
            for (int y = 350; y < 460; ++y) {
                if (mask.at<unsigned char>(y, x) != 0) {
                    weighted += y;
                    total += 1.0;
                }
            }
            if (total > 0.0) {
                sum += std::abs(weighted / total - 400.0);
                ++columns;
            }
        }
        return columns > 0 ? sum / columns : 0.0;
    };

    const double loose = wobbleOf(wobblyStroke(false));
    const double steady = wobbleOf(wobblyStroke(true));
    std::printf("  [pincel] temblor de la línea: %.2f px sin ayuda, %.2f px con pulso "
                "estable\n",
                loose, steady);
    EXPECT_GT(loose, 3.0) << "el zigzag de la prueba no llegó a temblar: no se está "
                             "midiendo lo que se cree";
    EXPECT_LT(steady, loose * 0.75)
        << "el pulso estable no está quitando el temblor de la mano";
}

// AYUDA 3: CEÑIR AL BORDE. La razón entera de que exista: el resultado deja de
// tener el ancho del pincel y pasa a tener la forma de la pieza.
TEST(EdgeBrushAssist, SnappingKeepsOnlyTheSideTheStrokeStartedOn) {
    const ViewTransform view = viewAt(1.0);
    // La escena tiene un borde vertical nítido en x = 1000: oscuro a la
    // izquierda (30), claro a la derecha (220).
    const auto crossTheEdge = [&view](bool snap) {
        EditorCanvas canvas;
        canvas.resize(kWidgetWidth, kWidgetHeight);
        canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
        canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
        canvas.setBrushRadius(25);
        canvas.setBrushSnap(snap);
        canvas.setBrushSteady(false);

        cv::Mat painted;
        QObject::connect(&canvas, &EditorCanvas::edgeCorrected,
                         [&](const cv::Mat& add, const cv::Mat&) { painted = add.clone(); });
        // Empieza bien dentro de lo OSCURO y cruza el borde.
        drag(&canvas, toScreen(view, {900.0F, 540.0F}), toScreen(view, {1100.0F, 540.0F}));
        return painted;
    };

    const cv::Mat plain = crossTheEdge(false);
    const cv::Mat snapped = crossTheEdge(true);
    ASSERT_FALSE(plain.empty());
    ASSERT_FALSE(snapped.empty());

    // Cuántos píxeles marca cada uno del lado CLARO, que es el que el operador
    // no señaló.
    const auto lightSide = [](const cv::Mat& mask) {
        return cv::countNonZero(mask(cv::Rect(1005, 480, 180, 120)));
    };
    const int plainLight = lightSide(plain);
    const int snappedLight = lightSide(snapped);
    std::printf("  [pincel] del lado claro: %d px sin ceñir, %d px ceñido\n", plainLight,
                snappedLight);

    EXPECT_GT(plainLight, 1000) << "sin ceñir, la banda debería invadir el lado claro";
    EXPECT_EQ(snappedLight, 0)
        << "ceñido al borde y aun así marca el lado que el operador no señaló: el "
           "resultado sigue teniendo la forma del pincel y no la de la pieza";
    // Y del lado oscuro se sigue marcando: ceñir no es dejar de pintar.
    EXPECT_GT(cv::countNonZero(snapped(cv::Rect(880, 480, 100, 120))), 1000)
        << "ceñir se llevó también lo que sí había que marcar";
}

// Y cuando NO hay borde que seguir, la pincelada hace lo de siempre. Un pincel
// que unas veces marca y otras no, sin decir por qué, se vive como averiado.
TEST(EdgeBrushAssist, WithNoEdgeToFollowTheStrokeBehavesAsAlways) {
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
    canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
    canvas.setBrushRadius(20);
    canvas.setBrushSnap(true);
    canvas.setBrushSteady(false);

    bool snapped = true;
    double contrast = -1.0;
    QObject::connect(&canvas, &EditorCanvas::edgeStrokeFinished,
                     [&](bool did, double howMuch, int, int) {
                         snapped = did;
                         contrast = howMuch;
                     });
    cv::Mat painted;
    QObject::connect(&canvas, &EditorCanvas::edgeCorrected,
                     [&](const cv::Mat& add, const cv::Mat&) { painted = add.clone(); });

    // Un trazo entero dentro de la zona oscura y lisa.
    const ViewTransform view = viewAt(1.0);
    drag(&canvas, toScreen(view, {400.0F, 300.0F}), toScreen(view, {600.0F, 300.0F}));

    std::printf("  [pincel] zona lisa: ceñido=%s, contraste %.1f\n", snapped ? "sí" : "no",
                contrast);
    EXPECT_FALSE(snapped) << "se ciñó a un borde que no existe";
    ASSERT_FALSE(painted.empty()) << "sin borde que seguir, la pincelada dejó de marcar: "
                                     "eso parece un pincel averiado";
    EXPECT_GT(cv::countNonZero(painted), 5000);
}

// EL PRECIO DE CEÑIR, ESCRITO PARA QUE NO SE OLVIDE.
//
// Esta comprobación no defiende una mejora: documenta lo que la ayuda QUITA.
// Salió de romper una prueba de punta a punta que llevaba tiempo verde, y es la
// razón por la que «ceñir al borde» viene apagado de fábrica.
//
// Con el ceñido puesto, una pincelada NO puede meter en la pieza algo que no se
// parece a lo que se señaló. Casi siempre eso es lo que se quiere. Pero hay
// correcciones legítimas que son exactamente eso —incluir un rebaje oscuro, una
// pestaña de poco contraste, un trozo que uno sabe que es pieza aunque la imagen
// no lo respalde— y con el ceñido puesto dejan de poder hacerse.
TEST(EdgeBrushAssist, SnappingRefusesToForceARegionThatDoesNotMatch) {
    const ViewTransform view = viewAt(1.0);
    // Un trazo que empieza en lo OSCURO y quiere llevarse lo claro por delante:
    // la corrección «a la fuerza» que el ceñido ya no permite.
    const auto forceAcross = [&view](bool snap) {
        EditorCanvas canvas;
        canvas.resize(kWidgetWidth, kWidgetHeight);
        canvas.setScene(sceneWithAnEdge(), pci::vision::Fixture{});
        canvas.setEdgeBrush(EditorCanvas::EdgeBrush::AddPiece);
        canvas.setBrushRadius(25);
        canvas.setBrushSnap(snap);
        canvas.setBrushSteady(false);
        cv::Mat painted;
        QObject::connect(&canvas, &EditorCanvas::edgeCorrected,
                         [&](const cv::Mat& add, const cv::Mat&) { painted = add.clone(); });
        drag(&canvas, toScreen(view, {900.0F, 540.0F}), toScreen(view, {1100.0F, 540.0F}));
        return painted.empty() ? 0 : cv::countNonZero(painted(cv::Rect(1005, 480, 180, 120)));
    };

    const int free = forceAcross(false);
    const int snapped = forceAcross(true);
    std::printf("  [pincel] forzar el lado contrario: %d px sin ceñir, %d px ceñido\n", free,
                snapped);
    EXPECT_GT(free, 1000) << "sin la ayuda tiene que poder forzarse: es una corrección "
                             "legítima y el pincel manual existe justo para eso";
    EXPECT_EQ(snapped, 0) << "el ceñido ya no está ciñendo";

    // Y de fábrica el pincel puede forzar: la ayuda que quita capacidad se
    // enciende a mano, no llega puesta.
    EditorCanvas fresh;
    EXPECT_FALSE(fresh.brushSnap())
        << "«ceñir al borde» viene encendido de fábrica y le quita al operador una "
           "corrección que antes podía hacer, sin que él lo haya pedido";
    EXPECT_TRUE(fresh.brushSteady())
        << "«pulso estable» solo filtra el temblor y no quita ninguna pincelada "
           "posible: no hay motivo para que venga apagado";
    EXPECT_FALSE(fresh.brushStraight()) << "«trazo recto» es un modo, no un valor por defecto";
}

// ---------------------------------------------------------------------------
// AUTOMÁTICA O MANUAL: cuántas piezas hay que buscar
// ---------------------------------------------------------------------------
//
// La queja: «hay veces en donde por default le tengo una pieza, e intenta
// detectar más de una pieza». Era exacto, y por dos motivos que se sumaban.
// Contar venía puesto por defecto, así que cualquier sombra o reflejo que
// pasara el filtro de área salía como una segunda pieza; y como el número
// esperado también era 1, esa sombra daba NG «esperaba 1, veo 2».
//
// El nudo estaba en que «nadie ha configurado esto» y «el operador ha dicho que
// hay una pieza» eran EL MISMO NÚMERO, y piden lo contrario: al primero hay que
// contarle, al segundo hay que dejarlo en paz. Separarlos es todo el arreglo.

TEST(PieceCountMode, AutomaticAndManualAreTheSameNumberSaidClearly) {
    // De fábrica: automático, que se guarda como el 0 de siempre.
    pci::ui::PiecesPage automatic(0);
    EXPECT_EQ(automatic.countMode(), pci::ui::PiecesPage::CountMode::Automatic);
    EXPECT_EQ(automatic.expectedPieces(), 0)
        << "automático tiene que seguir guardándose como 0: el modo es una forma "
           "de enseñar el dato que ya había, no un dato nuevo que se pueda "
           "desincronizar del otro";

    // Un número guardado es manual, con ese número.
    pci::ui::PiecesPage manual(6);
    EXPECT_EQ(manual.countMode(), pci::ui::PiecesPage::CountMode::Manual);
    EXPECT_EQ(manual.expectedPieces(), 6);

    // Y una pieza declarada a mano es manual, no automático.
    pci::ui::PiecesPage one(1);
    EXPECT_EQ(one.countMode(), pci::ui::PiecesPage::CountMode::Manual);
    EXPECT_EQ(one.expectedPieces(), 1);
}

// EL BOTÓN QUE NO HACÍA NADA.
//
// «Usar lo que se ve ahora» prometía poner en el campo el número de piezas que
// la cámara está detectando. Su manejador llamaba a `setDetectedCount`, que solo
// refresca el texto de estado: el campo no se movía. Un botón que promete algo y
// no lo hace es peor que no tenerlo, porque quien lo pulsa se queda creyendo que
// el número ya está puesto.
TEST(PieceCountMode, UsingWhatIsSeenActuallyFillsTheField) {
    pci::ui::PiecesPage page(0);
    ASSERT_EQ(page.expectedPieces(), 0);

    page.setDetectedCount(6);
    page.setExpectedPieces(6);  // lo que hace ahora la ventana al pulsar el botón
    EXPECT_EQ(page.countMode(), pci::ui::PiecesPage::CountMode::Manual)
        << "rellenar el número no pasó a manual: el número quedaría puesto y sin usar";
    EXPECT_EQ(page.expectedPieces(), 6)
        << "el botón sigue sin escribir en el campo, que es justo lo que prometía";

    // Y volver a automático se puede.
    page.setExpectedPieces(0);
    EXPECT_EQ(page.countMode(), pci::ui::PiecesPage::CountMode::Automatic);
    EXPECT_EQ(page.expectedPieces(), 0);
}

// LA COMPROBACIÓN DE PUNTA A PUNTA: con «manual, una pieza», una sombra que pasa
// el filtro de área deja de contarse como una segunda pieza.
TEST(PieceCountMode, WithOneDeclaredPieceAShadowIsNotASecondPiece) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // Una pieza clara y, aparte, una mancha más pequeña que también pasa el
    // filtro: exactamente lo que hace un reflejo o el borde iluminado del útil.
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(60, 90, 150, 120), QColor(230, 230, 230));
        painter.fillRect(QRect(280, 200, 70, 60), QColor(200, 200, 200));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("con_reflejo.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    // De fábrica (automático) sí las cuenta: es lo que tiene que hacer cuando
    // nadie ha dicho nada, y sirve además para comprobar que la escena de esta
    // prueba de verdad produce dos manchas.
    QElapsedTimer timer;
    timer.start();
    QString seen;
    while (timer.elapsed() < 3000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (auto* chip = window.findChild<QLabel*>(QStringLiteral("piecesChip"));
            chip != nullptr && chip->isVisible()) {
            seen = chip->text().trimmed();
        }
        if (seen.contains(QStringLiteral("2"))) {
            break;
        }
    }
    std::printf("  [contar] en automático, con pieza + reflejo dice: «%s»\n",
                seen.toStdString().c_str());
    EXPECT_TRUE(seen.contains(QStringLiteral("2")))
        << "el montaje no llegó a producir dos manchas: esta prueba no estaría "
           "midiendo lo que cree (dijo: "
        << seen.toStdString() << ")";

    // Y AHORA LA MITAD QUE IMPORTA: el operador dice que hay UNA pieza.
    window.declareExpectedPieces(1);
    timer.restart();
    QString afterDeclaring;
    bool stillCounting = false;
    while (timer.elapsed() < 2000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        afterDeclaring.clear();
        // Por NOMBRE y no por texto: el selector de pieza también dice «pieza»,
        // y buscando por texto esta comprobación leía la etiqueta equivocada.
        if (auto* chip = window.findChild<QLabel*>(QStringLiteral("piecesChip"));
            chip != nullptr && chip->isVisible()) {
            afterDeclaring = chip->text().trimmed();
        }
        if (afterDeclaring.contains(QStringLiteral("2"))) {
            stillCounting = true;
        }
    }
    std::printf("  [contar] tras declarar una pieza dice: %s\n",
                afterDeclaring.isEmpty() ? "(nada)" : afterDeclaring.toStdString().c_str());
    EXPECT_FALSE(stillCounting)
        << "declarada UNA pieza, el reflejo se sigue contando como una segunda: es "
           "exactamente la queja de partida, y ademas daria NG «esperaba 1, veo 2»";
    EXPECT_FALSE(afterDeclaring.contains(QStringLiteral("2")))
        << "el recuento se quedo en 2 despues de declarar que hay una sola pieza";
}

// ---------------------------------------------------------------------------
// Pieza negra sobre fondo negro
// ---------------------------------------------------------------------------
//
// La queja: «si la pieza es negra, y el demás cuadro es negro no se alcanza a
// ver correctamente». Una pieza mate oscura sobre fondo oscuro ocupa treinta
// niveles de gris de los 256 que hay: la detección puede estar funcionando
// perfectamente y el operador no tiene forma de saberlo.
//
// EL RIESGO DE ESTE ARREGLO, y por eso esta prueba: ya existe otra forma de
// subir el brillo —los controles de la cámara— y esa SÍ cambia el fotograma que
// se analiza. Si el realce de vista llegara al análisis, las cotas se moverían
// por haber subido el brillo para poder mirar. Aquí se comprueba que no.

TEST(DarkOnDark, EnhancingTheViewDoesNotMoveASingleMeasurement) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // Pieza casi negra (46) sobre fondo casi negro (18): la escena del problema.
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(18, 18, 18));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(120, 90, 160, 120), QColor(46, 46, 46));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("negro.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return false;
    };
    ASSERT_TRUE(waitFor([&] { return canvas->liveContour().size() >= 4; }))
        << "ni siquiera detecta la pieza en la escena oscura";
    const double areaBefore = std::abs(polygonArea(canvas->liveContour()));
    ASSERT_GT(areaBefore, 1000.0);

    // Lo que se ve, ANTES de realzar.
    const auto shot = [&canvas] {
        QImage image(canvas->size(), QImage::Format_RGB888);
        canvas->render(&image);
        return image;
    };
    const QImage plain = shot();

    // El operador enciende el realce desde el menú Ver.
    auto* enhance = window.findChild<QAction*>(QStringLiteral("viewEnhanceAction"));
    ASSERT_NE(enhance, nullptr) << "no hay forma de realzar la vista desde el menú";
    ASSERT_TRUE(enhance->isCheckable());
    enhance->setChecked(true);
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    ASSERT_TRUE(canvas->viewEnhance());
    ASSERT_TRUE(canvas->viewEnhanceActive())
        << "encendido, pero dice que esta imagen no necesitaba realce";

    // 1) LO QUE SE VE CAMBIA, y mucho.
    const QImage enhanced = shot();
    long long lifted = 0;
    long long counted = 0;
    for (int y = 0; y < plain.height(); y += 3) {
        for (int x = 0; x < plain.width(); x += 3) {
            const int a = qGray(plain.pixel(x, y));
            const int b = qGray(enhanced.pixel(x, y));
            lifted += std::abs(b - a);
            ++counted;
        }
    }
    const double averageLift = counted > 0 ? static_cast<double>(lifted) / counted : 0.0;
    std::printf("  [oscuro] el realce mueve %.1f niveles de gris de media en pantalla\n",
                averageLift);
    EXPECT_GT(averageLift, 15.0)
        << "el realce apenas cambia lo que se ve: no sirve para lo único que existe";

    // 2) LO QUE SE MIDE NO CAMBIA. Ni un píxel.
    QApplication::processEvents(QEventLoop::AllEvents, 200);
    const double areaAfter = std::abs(polygonArea(canvas->liveContour()));
    std::printf("  [oscuro] contorno: %.0f px2 antes, %.0f px2 con el realce puesto\n",
                areaBefore, areaAfter);
    EXPECT_DOUBLE_EQ(areaBefore, areaAfter)
        << "realzar la vista movió el contorno medido. Es el fallo que esta prueba "
           "existe para impedir: las cotas cambiarían por subir el brillo para mirar";

    // 3) Y al apagarlo, todo vuelve.
    enhance->setChecked(false);
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    EXPECT_FALSE(canvas->viewEnhance());
    EXPECT_DOUBLE_EQ(std::abs(polygonArea(canvas->liveContour())), areaBefore);
}

// El contorno se ve encima de cualquier cosa, que era la otra mitad del
// problema: sobre una pieza clara el verde desaparece, y sobre una escena oscura
// no se distingue dónde acaba una cosa y empieza la otra.
TEST(DarkOnDark, TheContourIsDrawnWithAHaloSoItSurvivesAnyBackground) {
    EditorCanvas canvas;
    canvas.resize(kWidgetWidth, kWidgetHeight);
    // Escena BLANCA: el verde del contorno, solo, se pierde encima.
    QImage white(kImageWidth, kImageHeight, QImage::Format_RGB888);
    white.fill(QColor(245, 245, 245));
    canvas.setScene(white, pci::vision::Fixture{});
    canvas.setFrame(white);  // entra en modo vivo

    QPolygonF contour;
    contour << QPointF(700, 400) << QPointF(1200, 400) << QPointF(1200, 700)
            << QPointF(700, 700);
    canvas.setLivePiece(true, contour, QPointF(950, 550), 0.0, QString());

    QImage shot(canvas.size(), QImage::Format_RGB888);
    canvas.render(&shot);

    // SOLO DENTRO DE LA IMAGEN, y esta acotación es la prueba misma.
    //
    // Contando el fotograma entero salían 119 636 píxeles oscuros y la
    // comprobación pasaba... porque el fondo del propio widget es casi negro y
    // rodea a la imagen por los cuatro lados. Habría pasado igual sin halo
    // ninguno. Acotado al interior blanco de la escena, lo único que puede
    // salir oscuro es el halo.
    const ViewTransform view = viewAt(1.0);
    const QPointF topLeft = toScreen(view, {650.0F, 350.0F});
    const QPointF bottomRight = toScreen(view, {1250.0F, 750.0F});
    int dark = 0;
    int green = 0;
    for (int y = static_cast<int>(topLeft.y()); y < static_cast<int>(bottomRight.y()); ++y) {
        for (int x = static_cast<int>(topLeft.x()); x < static_cast<int>(bottomRight.x());
             ++x) {
            const QColor c = shot.pixelColor(x, y);
            if (qGray(c.rgb()) < 90) {
                ++dark;
            }
            if (c.green() > 150 && c.green() - c.red() > 80) {
                ++green;
            }
        }
    }
    std::printf("  [oscuro] dentro de la escena blanca: %d px de halo, %d px de verde\n",
                dark, green);
    EXPECT_GT(green, 200) << "no se está pintando el contorno: la prueba no mide nada";
    EXPECT_GT(dark, 200)
        << "el contorno no lleva halo: sobre una escena clara la línea verde "
           "desaparece y no hay forma de ver qué está detectando";
}

// ---------------------------------------------------------------------------
// Pasar de una pieza a otra para ver cómo sale cada una
// ---------------------------------------------------------------------------
//
// La queja: «si son más de una, poder cambiar de una en una para poder ver cómo
// salen». Hasta ahora las herramientas medían siempre la MAYOR y el tooltip del
// recuento lo decía con todas las letras: «si quieres medir otra, dibuja una
// zona de trabajo a su alrededor». O sea, rehacer la zona por cada pieza que se
// quisiera mirar.

TEST(PieceNavigator, TheArrowsChangeWhichPieceIsMeasured) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // Tres piezas de tamaños claramente distintos: el área dice cuál se midió
    // sin tener que adivinarlo.
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(60, 60, 60, 50), QColor(230, 230, 230));    // 1: pequeña
        painter.fillRect(QRect(250, 50, 120, 100), QColor(230, 230, 230)); // 2: LA MAYOR
        painter.fillRect(QRect(70, 200, 90, 70), QColor(230, 230, 230));   // 3: mediana
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("tres.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    const auto waitFor = [](auto predicate, int ms = 4000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (predicate()) {
                return true;
            }
        }
        return false;
    };
    ASSERT_TRUE(waitFor([&] { return canvas->liveContour().size() >= 4; }));

    // De fábrica se mide LA MAYOR, como siempre: 120x100 = 12 000 px2.
    ASSERT_TRUE(waitFor([&] {
        return std::abs(polygonArea(canvas->liveContour())) > 10000.0;
    })) << "sin tocar nada no está midiendo la mayor: se cambió el comportamiento de "
           "siempre sin que nadie lo pidiera";
    std::printf("  [navegador] de fábrica mide %.0f px2 (la mayor son 12 000)\n",
                std::abs(polygonArea(canvas->liveContour())));

    // Y el navegador aparece, porque hay más de una.
    QLabel* nav = nullptr;
    nav = window.findChild<QLabel*>(QStringLiteral("pieceNavLabel"));
    if (nav != nullptr && (!nav->isVisible() ||
                           !nav->text().contains(QStringLiteral("pieza 2/3")))) {
        nav = nullptr;
    }
    ASSERT_NE(nav, nullptr) << "con tres piezas no aparece el selector, o no dice cuál "
                               "de las tres se está midiendo";
    EXPECT_TRUE(nav->text().contains(QStringLiteral("la mayor")))
        << "no distingue «he elegido la 2» de «te ha tocado la 2». Dice: "
        << nav->text().toStdString();

    QToolButton* next = nullptr;
    next = window.findChild<QToolButton*>(QStringLiteral("pieceNextButton"));
    if (next != nullptr && !next->isVisible()) {
        next = nullptr;
    }
    ASSERT_NE(next, nullptr) << "no hay flecha para pasar a la siguiente pieza";

    // El recorrido es llano: 0 (la mayor), 1, 2, 3 y vuelta. Todas salen.
    next->click();
    ASSERT_TRUE(waitFor([&] {
        const double area = std::abs(polygonArea(canvas->liveContour()));
        return area > 2000.0 && area < 4000.0;  // la 1: 60x50 = 3 000
    })) << "la flecha no cambió la pieza medida. Mide "
        << std::abs(polygonArea(canvas->liveContour()));
    std::printf("  [navegador] pieza 1: %.0f px2 (son 3 000)\n",
                std::abs(polygonArea(canvas->liveContour())));

    next->click();  // la 2, que resulta ser la mayor
    ASSERT_TRUE(waitFor([&] {
        return std::abs(polygonArea(canvas->liveContour())) > 10000.0;
    }));

    next->click();  // la 3
    ASSERT_TRUE(waitFor([&] {
        const double area = std::abs(polygonArea(canvas->liveContour()));
        return area > 5000.0 && area < 8000.0;  // la 3: 90x70 = 6 300
    })) << "no se llega a la tercera. Mide "
        << std::abs(polygonArea(canvas->liveContour()));
    std::printf("  [navegador] pieza 3: %.0f px2 (son 6 300)\n",
                std::abs(polygonArea(canvas->liveContour())));

    // Y la vuelta entera devuelve a «la mayor»: se sale del modo manual con el
    // mismo gesto con el que se entró, sin tener que descubrir otro control.
    next->click();
    ASSERT_TRUE(waitFor([&] {
        return std::abs(polygonArea(canvas->liveContour())) > 10000.0;
    })) << "dando la vuelta no se recupera «la mayor»";
    bool backToLargest = false;
    for (auto* label : window.findChildren<QLabel*>()) {
        if (label->isVisible() && label->text().contains(QStringLiteral("la mayor"))) {
            backToLargest = true;
        }
    }
    EXPECT_TRUE(backToLargest) << "mide la mayor pero el indicador no dice que se ha "
                                  "vuelto al modo automático";
}

// Con UNA sola pieza el selector no está: un control apagado permanente es ruido
// en una barra que ya va llena.
TEST(PieceNavigator, WithOnePieceThereIsNothingToNavigate) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(120, 90, 160, 120), QColor(230, 230, 230));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("una_sola.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    QElapsedTimer timer;
    timer.start();
    bool sawCount = false;
    while (timer.elapsed() < 3000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        for (auto* label : window.findChildren<QLabel*>()) {
            if (label->isVisible() && label->text().contains(QStringLiteral("1 pieza"))) {
                sawCount = true;
            }
        }
        if (sawCount) {
            break;
        }
    }
    ASSERT_TRUE(sawCount) << "ni siquiera llegó a contar la pieza";

    for (auto* button : window.findChildren<QToolButton*>()) {
        EXPECT_FALSE(button->isVisible() && button->text() == QStringLiteral("›"))
            << "el selector de pieza está a la vista con una sola pieza";
    }
}

// TODAS LAS PIEZAS SE DIBUJAN, Y LLEVAN SU NÚMERO.
//
// La queja: «solo toma un borde de una sola pieza, aunque diga que haya
// muchos». Era literal — el recuento decía «6 piezas» y en pantalla había UNA
// línea verde. El operador no tenía forma de saber cuáles eran las otras cinco,
// ni si el programa las había encontrado donde él las veía.
//
// Y con el selector de pieza numerando en orden de lectura, hace falta además
// poder LEER ese número encima de cada una: «pieza 3» no se puede comprobar
// contra la mesa si en la mesa no pone 3 en ningún sitio.
TEST(PieceNavigator, EveryPieceIsOutlinedAndNumberedNotJustTheMeasuredOne) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    const QRect boxes[3] = {QRect(60, 60, 60, 50), QRect(250, 50, 120, 100),
                            QRect(70, 200, 90, 70)};
    {
        QPainter painter(&photo);
        for (const auto& box : boxes) {
            painter.fillRect(box, QColor(230, 230, 230));
        }
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("tres_marcadas.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    auto* canvas = window.findChild<pci::inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 4000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (canvas->liveContour().size() >= 4) {
            break;
        }
    }
    ASSERT_GE(canvas->liveContour().size(), 4);
    // Un respiro para que llegue el análisis con TODAS.
    timer.restart();
    while (timer.elapsed() < 600) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    QImage shot(canvas->size(), QImage::Format_RGB888);
    canvas->render(&shot);

    // De coordenadas de imagen a coordenadas del lienzo, igual que hace el
    // propio canvas.
    const ViewTransform view({canvas->imageSize().width(), canvas->imageSize().height()},
                             {canvas->width(), canvas->height()}, 1.0, {0.0, 0.0});
    const auto onScreen = [&view](QPoint p) {
        const cv::Point2d q = view.imageToWidget(
            cv::Point2f(static_cast<float>(p.x()), static_cast<float>(p.y())));
        return QPoint(static_cast<int>(q.x), static_cast<int>(q.y));
    };

    // Verde de cualquiera de los dos tonos: el vivo de la medida (0,220,0) y el
    // apagado de las demás (90,170,110).
    const auto greenishAround = [&](const QRect& box) {
        const QPoint a = onScreen(box.topLeft() - QPoint(6, 6));
        const QPoint b = onScreen(box.bottomRight() + QPoint(6, 6));
        int count = 0;
        for (int y = a.y(); y <= b.y() && y < shot.height(); ++y) {
            for (int x = a.x(); x <= b.x() && x < shot.width(); ++x) {
                if (x < 0 || y < 0) {
                    continue;
                }
                const QColor c = shot.pixelColor(x, y);
                if (c.green() - c.red() > 30 && c.green() - c.blue() > 20) {
                    ++count;
                }
            }
        }
        return count;
    };

    const int first = greenishAround(boxes[0]);
    const int second = greenishAround(boxes[1]);
    const int third = greenishAround(boxes[2]);
    std::printf("  [contornos] verde alrededor de cada pieza: %d, %d, %d\n", first,
                second, third);
    EXPECT_GT(first, 50) << "la pieza 1 no lleva contorno dibujado: el programa dice que "
                            "hay tres y solo enseña dónde está una";
    EXPECT_GT(second, 50) << "la pieza 2 no lleva contorno dibujado";
    EXPECT_GT(third, 50) << "la pieza 3 no lleva contorno dibujado";
}

// EL NÚMERO QUE CONFIGURA EL OPERADOR ES EL QUE SE DETECTA.
//
// La queja: «detecta muchos cuando en las configuraciones solo debería de
// detectar uno; dependiendo de lo que ponga el usuario, eso debería detectar».
// El número esperado solo servía para juzgar el recuento al final: la detección
// seguía tratando como pieza a cualquier mancha que pasara el filtro de área, y
// una sombra se numeraba, se dibujaba y se podía llegar a medir.
TEST(PieceCountMode, DeclaringTwoPiecesWorksWithTheTwoBiggestAndSaysWhatWasLeftOut) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // Dos piezas de verdad y dos manchas más pequeñas que también pasan el
    // filtro de área: reflejos, o el borde iluminado del útil.
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(40, 40, 110, 90), QColor(230, 230, 230));
        painter.fillRect(QRect(240, 40, 110, 90), QColor(230, 230, 230));
        painter.fillRect(QRect(60, 210, 45, 40), QColor(210, 210, 210));
        painter.fillRect(QRect(280, 210, 45, 40), QColor(210, 210, 210));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("dos_y_manchas.png"));
    ASSERT_TRUE(photo.save(path));

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(pci::camera::SourceKind::Image, path));

    const auto chipText = [&window] {
        auto* chip = window.findChild<QLabel*>(QStringLiteral("piecesChip"));
        return (chip != nullptr && chip->isVisible()) ? chip->text().trimmed() : QString();
    };
    const auto settle = [&](int ms) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
    };

    // En automático se ven las cuatro: si no, esta prueba no mide nada.
    settle(1200);
    std::printf("  [recorte] en automático el chip dice: «%s»\n",
                chipText().toStdString().c_str());
    // «4 piezas» y no «1 de 4»: en automático no se recorta nada. La primera
    // versión de esta comprobación solo buscaba un «4» en el texto y por eso
    // dejó pasar exactamente ese fallo — el pipeline traía un valor de fábrica
    // distinto del de la pieza y recortaba sin que nadie lo hubiera pedido.
    ASSERT_TRUE(chipText().contains(QStringLiteral("4 piezas")))
        << "en automático no se trabaja con las cuatro que hay; dice: "
        << chipText().toStdString();

    // El operador declara DOS.
    window.declareExpectedPieces(2);
    settle(1200);
    const QString declared = chipText();
    std::printf("  [recorte] declaradas 2, el chip dice: «%s»\n",
                declared.toStdString().c_str());
    EXPECT_TRUE(declared.contains(QStringLiteral("2 de 4")))
        << "no dice las dos cifras. Enseñar solo las usadas haría desaparecer del "
           "informe una sombra de más sin dejar rastro; enseñar solo las vistas "
           "contradiría al selector, que numera las usadas. Dice: "
        << declared.toStdString();

    // Y el selector navega entre DOS, no entre cuatro.
    auto* nav = window.findChild<QLabel*>(QStringLiteral("pieceNavLabel"));
    ASSERT_NE(nav, nullptr);
    ASSERT_TRUE(nav->isVisible());
    std::printf("  [recorte] el selector dice: «%s»\n",
                nav->text().trimmed().toStdString().c_str());
    EXPECT_TRUE(nav->text().contains(QStringLiteral("/2")))
        << "el selector sigue ofreciendo piezas que ya no se tratan como tales. Dice: "
        << nav->text().toStdString();

    // Con UNA declarada no se enumera en absoluto.
    window.declareExpectedPieces(1);
    settle(1000);
    EXPECT_TRUE(chipText().isEmpty())
        << "declarada una pieza, el recuento sigue a la vista: " << chipText().toStdString();
}

// CAMBIAR EL NÚMERO TIENE QUE VERSE AL MOMENTO.
//
// Esta prueba existe por un fallo que las otras no podían ver. `applyPiecesPage`
// —el camino que recorre la ventana de verdad— asignaba el número y se olvidaba
// de la mitad: no tocaba la configuración del pipeline ni pedía reanalizar. Con
// una imagen parada eso significa que cambiar el número no cambiaba NADA.
//
// Y no saltó porque `declareExpectedPieces` sí lo hacía bien, y era la que
// usaban las pruebas. Un camino de prueba que funciona mientras el de verdad no.
// Ahora hay uno solo, y esto vigila la puerta por la que entra el operador.
TEST(PieceCountMode, ChangingTheNumberIsAnnouncedImmediately) {
    pci::ui::PiecesPage page(0);
    std::vector<int> announced;
    QObject::connect(&page, &pci::ui::PiecesPage::expectedPiecesChangedLive,
                     [&announced](int value) { announced.push_back(value); });

    // Construir la ventana no anuncia nada: nadie ha cambiado todavía.
    EXPECT_TRUE(announced.empty())
        << "la página avisa de un cambio nada más abrirse: eso reanalizaría el "
           "fotograma sin que el operador haya tocado nada";

    QSpinBox* field = nullptr;
    for (auto* box : page.findChildren<QSpinBox*>()) {
        field = box;
    }
    // Por su nombre. Esta prueba ya se rompió una vez buscando la palabra
    // «Manual», que desapareció al renombrar la pareja a «Contador automático
    // de piezas» / «Número exacto:», y la vuelta siguiente se quedó buscando
    // «el que NO dice automático» — que sigue siendo el rótulo, solo que negado.
    //
    // Lo que esta prueba comprueba es el AVISO EN VIVO, no cómo se llaman los
    // controles.
    auto* manual = page.findChild<QRadioButton*>(QStringLiteral("manualCountRadio"));
    ASSERT_NE(field, nullptr);
    ASSERT_NE(manual, nullptr);

    // Pasar a manual ya es un cambio: de «no vigilar» a «tienen que ser N».
    manual->setChecked(true);
    ASSERT_FALSE(announced.empty())
        << "elegir «manual» no avisa: la pantalla seguiría contando como en automático";

    announced.clear();
    field->setValue(4);
    ASSERT_FALSE(announced.empty())
        << "mover el número no avisa: el operador lo cambia mirando el recuento y el "
           "recuento no se entera";
    EXPECT_EQ(announced.back(), 4)
        << "se avisa de un número distinto del que puso el operador";
    std::printf("  [contar] al poner 4 se anuncia %d\n", announced.back());

    // Y volver al contador automático se anuncia como 0, que es como se guarda.
    // Aquí estaba el caso más silencioso: se buscaba «automático» porque el
    // rótulo había cambiado a «Contador automático de piezas», y el día que
    // cambie otra vez el `for` no encontrará nada, no pulsará nada, y la
    // aserción de después fallará lejos de la causa.
    announced.clear();
    auto* automatic = page.findChild<QRadioButton*>(QStringLiteral("automaticCountRadio"));
    ASSERT_NE(automatic, nullptr);
    automatic->setChecked(true);
    ASSERT_FALSE(announced.empty());
    EXPECT_EQ(announced.back(), 0);
}

// LAS COTAS SE PINTAN SOBRE LA PIEZA QUE SE ESTÁ MIDIENDO.
//
// El lienzo dibuja las marcas de todas las piezas pero los NÚMEROS de una sola:
// con seis piezas y cinco herramientas serían treinta etiquetas encima del
// vídeo, y eso no se lee. La elegida se decidía con `focusedPiece_`, que valía
// 0 y no lo movía nadie — o sea, siempre la primera en orden de lectura.
//
// Mientras las medidas en vivo no llevaban número de pieza daba igual: todas
// valían 0 y todas se pintaban. En cuanto el operador puede enfocar la tercera
// —con las flechas o pulsando su baldosa en el mosaico— eso deja de ser
// inofensivo: la pieza se remarca en verde y las cifras se quedan encima de
// otra. Es peor que no enseñar cotas, porque parecen las de la pieza señalada.
TEST_F(CanvasGestureTest, TheNumbersFollowTheFocusedPieceAndNotAlwaysTheFirst) {
    const auto labelPixels = [this](int focused) {
        // Tres medidas, una por pieza, cada una en un sitio distinto del
        // lienzo. Sólo la de la pieza enfocada debe salir escrita.
        std::vector<ToolRunResult> results;
        for (int i = 0; i < 3; ++i) {
            ToolRunResult r;
            r.toolId = i + 1;
            r.name = "ancho";
            r.type = ToolType::Ruler;
            r.ok = true;
            r.measured = 100.0 + i;
            r.pieceIndex = i;
            r.overlayPoints = {{300.0F + 400.0F * static_cast<float>(i), 500.0F}};
            results.push_back(r);
        }
        canvas.setResults(results);
        canvas.setFocusedPiece(focused);

        QImage rendered(kWidgetWidth, kWidgetHeight, QImage::Format_ARGB32);
        rendered.fill(Qt::black);
        canvas.render(&rendered);

        // NO CUÁNTA TINTA, SINO DÓNDE.
        //
        // Contar píxeles de etiqueta no distingue nada: con el filtro roto sale
        // la de la pieza 1 y con el filtro bien sale la de la 3, y las dos
        // manchan lo mismo. Se devuelve la x MEDIA de la tinta, que es la única
        // forma de saber sobre qué pieza han caído los números.
        //
        // Aprendido a base de escribir primero la versión que contaba: pasaba
        // igual de verde con el filtro mutado a «siempre la pieza 0», que es
        // exactamente el fallo que venía a cazar.
        long long sumX = 0;
        int coloured = 0;
        for (int y = 0; y < rendered.height(); ++y) {
            for (int x = 0; x < rendered.width(); ++x) {
                const QColor c = rendered.pixelColor(x, y);
                if (c.green() > 150 && c.red() < 120 && c.blue() < 120) {
                    sumX += x;
                    ++coloured;
                }
            }
        }
        return std::pair<int, double>{
            coloured, coloured > 0 ? static_cast<double>(sumX) / coloured : -1.0};
    };

    const auto [firstInk, firstX] = labelPixels(0);
    const auto [thirdInk, thirdX] = labelPixels(2);
    std::printf("  [cotas] enfocando la 1: %d px centrados en x=%.0f;  "
                "enfocando la 3: %d px centrados en x=%.0f\n",
                firstInk, firstX, thirdInk, thirdX);

    ASSERT_GT(firstInk, 0) << "no se pinta ninguna etiqueta ni con la primera pieza";
    ASSERT_GT(thirdInk, 0) << "enfocar la tercera pieza deja el vídeo sin cotas";
    // Las tres anclas están a 300, 700 y 1100 en la imagen: izquierda, centro y
    // derecha. Enfocar la tercera tiene que llevar los números claramente más a
    // la derecha; si el filtro se quedara en la pieza 0, las dos medidas
    // caerían en el mismo sitio.
    EXPECT_GT(thirdX, firstX + 100.0)
        << "los números no se han movido al enfocar otra pieza: siguen sobre la primera";
}
