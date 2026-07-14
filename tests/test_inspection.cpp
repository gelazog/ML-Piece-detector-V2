#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>

#include "inspection_editor/execution/edge_detection.h"
#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/undo_stack.h"
#include "test_helpers.h"
#include "vision/pipeline.h"
#include "vision/position_fixture.h"

using namespace pci::inspection;
using pci::testhelpers::drawLPiece;
using pci::testhelpers::lPointToImage;
using pci::vision::Fixture;

namespace {

// Fixture identidad: coords de pieza == coords de imagen (para probar cada
// herramienta sin depender del pipeline de visión).
const Fixture kIdentity{{0.0F, 0.0F}, 0.0};

ToolConfig makeConfig(ToolType type, const ToolGeometry& geometry, double tolMin,
                      double tolMax) {
    ToolConfig config;
    config.type = type;
    config.name = std::string("test_") + toolTypeName(type);
    config.geometryJson = toJson(geometry);
    config.toleranceMin = tolMin;
    config.toleranceMax = tolMax;
    return config;
}

}  // namespace

// --- JSON de geometrías ---

TEST(ToolGeometry, JsonRoundTripAllTypes) {
    const CaliperGeometry caliper{{10.5F, 20.0F}, {110.0F, 25.5F}, 8.0F};
    auto caliperBack = geometryFromJson(ToolType::Caliper, toJson(ToolGeometry(caliper)));
    ASSERT_TRUE(caliperBack.isOk());
    const auto& c = std::get<CaliperGeometry>(caliperBack.value());
    EXPECT_FLOAT_EQ(c.p0.x, 10.5F);
    EXPECT_FLOAT_EQ(c.p1.y, 25.5F);
    EXPECT_FLOAT_EQ(c.bandWidth, 8.0F);

    const CircleGeometry circle{{50.0F, 60.0F}, 42.0F, 9.0F, 72};
    auto circleBack = geometryFromJson(ToolType::Circle, toJson(ToolGeometry(circle)));
    ASSERT_TRUE(circleBack.isOk());
    EXPECT_FLOAT_EQ(std::get<CircleGeometry>(circleBack.value()).radius, 42.0F);
    EXPECT_EQ(std::get<CircleGeometry>(circleBack.value()).rayCount, 72);

    // JSON de la versión anterior (sin "rays"): usa el valor por defecto.
    auto legacy = geometryFromJson(ToolType::Circle,
                                   R"({"cx":50.0,"cy":60.0,"r":42.0,"band":9.0})");
    ASSERT_TRUE(legacy.isOk());
    EXPECT_EQ(std::get<CircleGeometry>(legacy.value()).rayCount, 36);

    const PointToLineGeometry p2l{{0, 0}, {100, 0}, {50, 10}, {50, 90}};
    auto p2lBack = geometryFromJson(ToolType::PointToLine, toJson(ToolGeometry(p2l)));
    ASSERT_TRUE(p2lBack.isOk());
    EXPECT_FLOAT_EQ(std::get<PointToLineGeometry>(p2lBack.value()).scanB.y, 90.0F);

    const EdgeFlawGeometry flaw{{5, 5}, {95, 5}, 14.0F, 12};
    auto flawBack = geometryFromJson(ToolType::EdgeFlaw, toJson(ToolGeometry(flaw)));
    ASSERT_TRUE(flawBack.isOk());
    EXPECT_EQ(std::get<EdgeFlawGeometry>(flawBack.value()).scanCount, 12);

    const BlobGeometry blob{{30, 40}, 80.0F, 60.0F, 25.0F, false};
    auto blobBack = geometryFromJson(ToolType::Blob, toJson(ToolGeometry(blob)));
    ASSERT_TRUE(blobBack.isOk());
    EXPECT_FALSE(std::get<BlobGeometry>(blobBack.value()).darkBlobs);
}

TEST(ToolGeometry, RulerRoundTrip) {
    const RulerGeometry ruler{{5.5F, 10.0F}, {65.5F, 10.0F}};
    auto back = geometryFromJson(ToolType::Ruler, toJson(ToolGeometry(ruler)));
    ASSERT_TRUE(back.isOk());
    EXPECT_FLOAT_EQ(std::get<RulerGeometry>(back.value()).p0.x, 5.5F);
    EXPECT_FLOAT_EQ(std::get<RulerGeometry>(back.value()).p1.x, 65.5F);
}

TEST(ToolGeometry, WrongTypeOrGarbageFails) {
    const CaliperGeometry caliper{{0, 0}, {10, 10}, 5.0F};
    EXPECT_FALSE(geometryFromJson(ToolType::Circle, toJson(ToolGeometry(caliper))).isOk());
    EXPECT_FALSE(geometryFromJson(ToolType::Caliper, "esto no es json").isOk());
}

// --- Detección de bordes ---

TEST(EdgeDetection, FindsStepEdgeSubpixel) {
    cv::Mat gray(100, 100, CV_8UC1, cv::Scalar(40));
    gray.colRange(50, 100).setTo(220);

    const auto edges = detectEdges(gray, {10.0F, 50.0F}, {90.0F, 50.0F}, 5.0F);
    ASSERT_FALSE(edges.empty());
    EXPECT_NEAR(edges[0].point.x, 50.0, 1.5);
    EXPECT_GT(edges[0].strength, 0.0);  // oscuro -> claro en el sentido del escaneo
}

TEST(EdgeDetection, FlatImageHasNoEdges) {
    const cv::Mat gray(100, 100, CV_8UC1, cv::Scalar(128));
    EXPECT_TRUE(detectEdges(gray, {10.0F, 50.0F}, {90.0F, 50.0F}, 5.0F).empty());
}

// --- Caliper ---

TEST(Caliper, MeasuresBarWidth) {
    cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(220));
    gray.colRange(80, 120).setTo(40);  // barra oscura de 40 px

    const CaliperGeometry g{{40.0F, 100.0F}, {160.0F, 100.0F}, 10.0F};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::Caliper, ToolGeometry(g), 35, 45));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_TRUE(result.value().ok) << result.value().detail;
    EXPECT_NEAR(result.value().measured, 40.0, 1.5);
}

TEST(Caliper, OutOfToleranceIsNg) {
    cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(220));
    gray.colRange(80, 120).setTo(40);

    const CaliperGeometry g{{40.0F, 100.0F}, {160.0F, 100.0F}, 10.0F};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::Caliper, ToolGeometry(g), 45, 60));
    ASSERT_TRUE(result.isOk());
    EXPECT_FALSE(result.value().ok);
    EXPECT_NEAR(result.value().measured, 40.0, 1.5);
}

TEST(Caliper, FlatSceneReportsMissingEdges) {
    const cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(128));
    const CaliperGeometry g{{40.0F, 100.0F}, {160.0F, 100.0F}, 10.0F};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::Caliper, ToolGeometry(g), 0, 100));
    ASSERT_TRUE(result.isOk());
    EXPECT_FALSE(result.value().ok);
    EXPECT_NE(result.value().detail.find("bordes"), std::string::npos);
}

// --- Círculo ---

TEST(Circle, MeasuresDiameterOfDisc) {
    cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(220));
    cv::circle(gray, {100, 100}, 40, cv::Scalar(40), cv::FILLED, cv::LINE_AA);

    const CircleGeometry g{{100.0F, 100.0F}, 40.0F, 12.0F};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::Circle, ToolGeometry(g), 76, 84));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_TRUE(result.value().ok) << result.value().detail;
    EXPECT_NEAR(result.value().measured, 80.0, 2.5);
}

TEST(Circle, EmptySceneFailsControlled) {
    const cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(128));
    const CircleGeometry g{{100.0F, 100.0F}, 40.0F, 12.0F};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::Circle, ToolGeometry(g), 0, 999));
    ASSERT_TRUE(result.isOk());
    EXPECT_FALSE(result.value().ok);
}

// --- Point-to-Line ---

TEST(PointToLine, MeasuresPerpendicularDistance) {
    cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(220));
    cv::rectangle(gray, {50, 120}, {150, 190}, cv::Scalar(40), cv::FILLED);

    // Línea de referencia horizontal en y=100; escaneo vertical que cruza el
    // borde superior del rectángulo (y=120) -> distancia esperada 20 px.
    const PointToLineGeometry g{{50.0F, 100.0F}, {150.0F, 100.0F},
                                {100.0F, 105.0F}, {100.0F, 140.0F}};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::PointToLine, ToolGeometry(g), 18, 22));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_TRUE(result.value().ok) << result.value().detail;
    EXPECT_NEAR(result.value().measured, 20.0, 1.5);
}

// --- Edge Flaw ---

TEST(EdgeFlaw, StraightEdgeHasLowDeviation) {
    cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(220));
    cv::rectangle(gray, {40, 120}, {160, 190}, cv::Scalar(40), cv::FILLED);

    const EdgeFlawGeometry g{{60.0F, 120.0F}, {140.0F, 120.0F}, 16.0F, 15};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::EdgeFlaw, ToolGeometry(g), 0, 2));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_TRUE(result.value().ok) << result.value().detail;
    EXPECT_LT(result.value().measured, 1.5);
}

TEST(EdgeFlaw, NotchIsDetected) {
    cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(220));
    cv::rectangle(gray, {40, 120}, {160, 190}, cv::Scalar(40), cv::FILLED);
    // Muesca de 5 px de profundidad en el borde superior.
    cv::rectangle(gray, {95, 120}, {105, 125}, cv::Scalar(220), cv::FILLED);

    const EdgeFlawGeometry g{{60.0F, 120.0F}, {140.0F, 120.0F}, 16.0F, 25};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::EdgeFlaw, ToolGeometry(g), 0, 2));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_FALSE(result.value().ok);  // la muesca supera la tolerancia de 2 px
    EXPECT_NEAR(result.value().measured, 5.0, 2.0);
}

// --- Blob ---

TEST(Blob, CountsSpotsAboveMinArea) {
    cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(220));
    cv::circle(gray, {70, 100}, 5, cv::Scalar(40), cv::FILLED);
    cv::circle(gray, {100, 100}, 5, cv::Scalar(40), cv::FILLED);
    cv::circle(gray, {130, 90}, 5, cv::Scalar(40), cv::FILLED);
    cv::circle(gray, {90, 80}, 1, cv::Scalar(40), cv::FILLED);  // demasiado pequeño

    const BlobGeometry g{{100.0F, 95.0F}, 120.0F, 80.0F, 20.0F, true};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::Blob, ToolGeometry(g), 3, 3));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_TRUE(result.value().ok) << result.value().detail;
    EXPECT_DOUBLE_EQ(result.value().measured, 3.0);
}

// --- Regla ---

TEST(Ruler, MeasuresOwnLengthWithMmDetail) {
    const cv::Mat gray(100, 100, CV_8UC1, cv::Scalar(128));
    const RulerGeometry g{{10.0F, 10.0F}, {70.0F, 10.0F}};

    // Sin calibración: 60 px, detalle en px.
    auto result = runTool(gray, kIdentity, makeConfig(ToolType::Ruler, ToolGeometry(g), 55, 65));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_TRUE(result.value().ok);
    EXPECT_NEAR(result.value().measured, 60.0, 1e-6);
    EXPECT_NE(result.value().detail.find("px"), std::string::npos);
    EXPECT_EQ(result.value().detail.find("mm"), std::string::npos);

    // Con escala 0.25 mm/px: el detalle incluye los mm.
    result = runTool(gray, kIdentity, makeConfig(ToolType::Ruler, ToolGeometry(g), 55, 65),
                     0.25);
    ASSERT_TRUE(result.isOk());
    EXPECT_NE(result.value().detail.find("15.00mm"), std::string::npos);

    // Unidad forzada a cm: 60 px * 0.25 = 15 mm = 1.5 cm.
    result = runTool(gray, kIdentity, makeConfig(ToolType::Ruler, ToolGeometry(g), 55, 65),
                     0.25, LengthUnit::Centimeters);
    ASSERT_TRUE(result.isOk());
    EXPECT_NE(result.value().detail.find("1.50cm"), std::string::npos);

    // Unidad forzada a px: sin mm aunque haya escala.
    result = runTool(gray, kIdentity, makeConfig(ToolType::Ruler, ToolGeometry(g), 55, 65),
                     0.25, LengthUnit::Pixels);
    ASSERT_TRUE(result.isOk());
    EXPECT_EQ(result.value().detail.find("mm"), std::string::npos);

    // Tolerancias sugeridas: banda de ±10%.
    double lo = 0.0;
    double hi = 0.0;
    suggestTolerances(ToolType::Ruler, 60.0, lo, hi);
    EXPECT_DOUBLE_EQ(lo, 54.0);
    EXPECT_DOUBLE_EQ(hi, 66.0);
}

// --- Anclaje al fixture (el test de oro de la fase) ---

TEST(FixtureAnchoring, CaliperMeasuresSameOnRotatedPiece) {
    // Pieza L a 20°: se define un caliper cruzando el brazo vertical
    // (1 unidad = 40 px de ancho) y se guarda en coordenadas de pieza.
    const float scale = 40.0F;
    pci::vision::PipelineConfig cfg;
    cfg.autoOrient = true;  // el anclaje requiere seguir la rotación de la pieza
    const auto imageA = drawLPiece({640, 480}, {300.0F, 240.0F}, 20.0, scale, 40, 220);
    const auto analysisA = pci::vision::analyzeFrame(imageA, cfg);
    ASSERT_TRUE(analysisA.isOk()) << analysisA.error().message;
    const Fixture fixtureA = analysisA.value().fixture;

    const cv::Point2f scanStartImg = lPointToImage({-0.7F, 2.0F}, {300.0F, 240.0F}, 20.0, scale);
    const cv::Point2f scanEndImg = lPointToImage({1.7F, 2.0F}, {300.0F, 240.0F}, 20.0, scale);

    CaliperGeometry g;
    g.p0 = pci::vision::toPieceCoords(fixtureA, scanStartImg);
    g.p1 = pci::vision::toPieceCoords(fixtureA, scanEndImg);
    g.bandWidth = 5.0F;
    const auto config = makeConfig(ToolType::Caliper, ToolGeometry(g), 35, 45);

    const auto resultA = runTool(imageA, fixtureA, config);
    ASSERT_TRUE(resultA.isOk()) << resultA.error().message;
    ASSERT_TRUE(resultA.value().ok) << resultA.value().detail;
    EXPECT_NEAR(resultA.value().measured, 40.0, 2.0);

    // La misma pieza rotada a 125° y desplazada: la herramienta debe seguirla
    // y medir lo mismo sin tocar la geometría guardada.
    const auto imageB = drawLPiece({640, 480}, {340.0F, 200.0F}, 125.0, scale, 40, 220);
    const auto analysisB = pci::vision::analyzeFrame(imageB, cfg);
    ASSERT_TRUE(analysisB.isOk()) << analysisB.error().message;

    const auto resultB = runTool(imageB, analysisB.value().fixture, config);
    ASSERT_TRUE(resultB.isOk()) << resultB.error().message;
    ASSERT_TRUE(resultB.value().ok) << resultB.value().detail;
    EXPECT_NEAR(resultB.value().measured, resultA.value().measured, 1.5);
}

// --- Pila de deshacer/rehacer ---

TEST(UndoStack, UndoRedoRoundTrip) {
    UndoStack<std::vector<int>> stack;
    std::vector<int> state{1};

    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.undo(state).has_value());

    stack.push(state);       // antes de mutar a {1,2}
    state = {1, 2};
    stack.push(state);       // antes de mutar a {1,2,3}
    state = {1, 2, 3};

    auto previous = stack.undo(state);
    ASSERT_TRUE(previous.has_value());
    EXPECT_EQ(*previous, (std::vector<int>{1, 2}));
    state = *previous;

    auto again = stack.undo(state);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, (std::vector<int>{1}));
    state = *again;

    auto redone = stack.redo(state);
    ASSERT_TRUE(redone.has_value());
    EXPECT_EQ(*redone, (std::vector<int>{1, 2}));
    state = *redone;

    // Una mutación nueva limpia el camino de rehacer.
    stack.push(state);
    state.push_back(9);
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStack, LimitDropsOldest) {
    UndoStack<int> stack(3);
    for (int i = 0; i < 5; ++i) {
        stack.push(i);
    }
    int current = 99;
    EXPECT_EQ(*stack.undo(current), 4);
    EXPECT_EQ(*stack.undo(4), 3);
    EXPECT_EQ(*stack.undo(3), 2);
    EXPECT_FALSE(stack.undo(2).has_value());  // 0 y 1 se descartaron
}

// --- Tolerancias sugeridas ---

TEST(SuggestTolerances, BandsPerToolType) {
    double lo = -1.0;
    double hi = -1.0;

    suggestTolerances(ToolType::Caliper, 40.0, lo, hi);
    EXPECT_DOUBLE_EQ(lo, 36.0);  // ±10%
    EXPECT_DOUBLE_EQ(hi, 44.0);

    suggestTolerances(ToolType::Circle, 10.0, lo, hi);
    EXPECT_DOUBLE_EQ(lo, 8.0);  // banda mínima de ±2 px
    EXPECT_DOUBLE_EQ(hi, 12.0);

    suggestTolerances(ToolType::Blob, 3.0, lo, hi);
    EXPECT_DOUBLE_EQ(lo, 3.0);  // conteo exacto
    EXPECT_DOUBLE_EQ(hi, 3.0);

    suggestTolerances(ToolType::EdgeFlaw, 0.5, lo, hi);
    EXPECT_DOUBLE_EQ(lo, 0.0);
    EXPECT_DOUBLE_EQ(hi, 2.0);  // techo mínimo de 2 px
}

// --- runTools: errores por herramienta controlados ---

TEST(RunTools, CorruptToolBecomesNgNotCrash) {
    const cv::Mat gray(100, 100, CV_8UC1, cv::Scalar(128));

    ToolConfig corrupt;
    corrupt.type = ToolType::Caliper;
    corrupt.name = "rota";
    corrupt.geometryJson = "{basura";

    ToolConfig disabled = corrupt;
    disabled.name = "apagada";
    disabled.enabled = false;

    const auto results = runTools(gray, kIdentity, {corrupt, disabled});
    ASSERT_EQ(results.size(), 1U);  // la deshabilitada no corre
    EXPECT_FALSE(results[0].ok);
    EXPECT_FALSE(results[0].detail.empty());
}
