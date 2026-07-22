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

TEST(LineToLine, JsonRoundTrip) {
    const LineToLineGeometry g{{10.0F, 10.0F}, {90.0F, 10.0F},
                               {10.0F, 40.0F}, {90.0F, 70.0F}};
    auto back = geometryFromJson(ToolType::LineToLine, toJson(ToolGeometry(g)));
    ASSERT_TRUE(back.isOk()) << back.error().message;
    const auto& r = std::get<LineToLineGeometry>(back.value());
    EXPECT_FLOAT_EQ(r.a0.x, 10.0F);
    EXPECT_FLOAT_EQ(r.a1.x, 90.0F);
    EXPECT_FLOAT_EQ(r.b0.y, 40.0F);
    EXPECT_FLOAT_EQ(r.b1.y, 70.0F);
}

TEST(LineToLine, MeasuresAngleBetweenLines) {
    const cv::Mat gray(120, 120, CV_8UC1, cv::Scalar(128));

    // Línea A horizontal; línea B a 45° (sube 1 por cada 1 en x).
    const LineToLineGeometry g{{10.0F, 20.0F}, {100.0F, 20.0F},
                               {10.0F, 100.0F}, {100.0F, 10.0F}};
    auto result = runTool(gray, kIdentity,
                          makeConfig(ToolType::LineToLine, ToolGeometry(g), 43, 47));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_TRUE(result.value().measuredIsAngle);
    EXPECT_NEAR(result.value().measured, 45.0, 1e-3);
    EXPECT_TRUE(result.value().ok);  // 45 dentro de [43, 47].

    // Líneas paralelas: ángulo 0.
    const LineToLineGeometry par{{10.0F, 20.0F}, {100.0F, 20.0F},
                                 {10.0F, 60.0F}, {100.0F, 60.0F}};
    result = runTool(gray, kIdentity,
                     makeConfig(ToolType::LineToLine, ToolGeometry(par), -1, 1));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_NEAR(result.value().measured, 0.0, 1e-3);
}

TEST(Angle, JsonRoundTrip) {
    const AngleGeometry g{{50.0F, 50.0F}, {90.0F, 50.0F}, {50.0F, 10.0F}};
    auto back = geometryFromJson(ToolType::Angle, toJson(ToolGeometry(g)));
    ASSERT_TRUE(back.isOk()) << back.error().message;
    const auto& r = std::get<AngleGeometry>(back.value());
    EXPECT_FLOAT_EQ(r.vertex.x, 50.0F);
    EXPECT_FLOAT_EQ(r.vertex.y, 50.0F);
    EXPECT_FLOAT_EQ(r.end0.x, 90.0F);
    EXPECT_FLOAT_EQ(r.end1.y, 10.0F);
}

TEST(Angle, MeasuresCornerAngle) {
    const cv::Mat gray(120, 120, CV_8UC1, cv::Scalar(128));

    // Esquina recta: un lado hacia +x, el otro hacia -y (arriba) => 90°.
    const AngleGeometry right{{50.0F, 50.0F}, {90.0F, 50.0F}, {50.0F, 10.0F}};
    auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::Angle, ToolGeometry(right), 88, 92));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_TRUE(result.value().measuredIsAngle);
    EXPECT_NEAR(result.value().measured, 90.0, 1e-3);
    EXPECT_TRUE(result.value().ok);

    // Ángulo de 45°: lado hacia +x y lado en diagonal (+x, -y).
    const AngleGeometry diag{{50.0F, 50.0F}, {90.0F, 50.0F}, {90.0F, 10.0F}};
    result = runTool(gray, kIdentity, makeConfig(ToolType::Angle, ToolGeometry(diag), 0, 180));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_NEAR(result.value().measured, 45.0, 1e-3);

    // Ángulo llano (lados opuestos) => 180°.
    const AngleGeometry flat{{50.0F, 50.0F}, {90.0F, 50.0F}, {10.0F, 50.0F}};
    result = runTool(gray, kIdentity, makeConfig(ToolType::Angle, ToolGeometry(flat), 0, 180));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_NEAR(result.value().measured, 180.0, 1e-3);
}

TEST(PolyBlob, JsonRoundTrip) {
    PolyBlobGeometry g;
    g.vertices = {{10.0F, 10.0F}, {90.0F, 15.0F}, {70.0F, 80.0F}, {20.0F, 60.0F}};
    g.minArea = 12.0F;
    g.darkBlobs = false;
    auto back = geometryFromJson(ToolType::PolyBlob, toJson(ToolGeometry(g)));
    ASSERT_TRUE(back.isOk()) << back.error().message;
    const auto& r = std::get<PolyBlobGeometry>(back.value());
    ASSERT_EQ(r.vertices.size(), 4U);
    EXPECT_FLOAT_EQ(r.vertices[0].x, 10.0F);
    EXPECT_FLOAT_EQ(r.vertices[1].y, 15.0F);
    EXPECT_FLOAT_EQ(r.vertices[2].x, 70.0F);
    EXPECT_FLOAT_EQ(r.minArea, 12.0F);
    EXPECT_FALSE(r.darkBlobs);
}

TEST(PolyBlob, RejectsDegeneratePolygon) {
    PolyBlobGeometry g;
    g.vertices = {{10.0F, 10.0F}, {90.0F, 15.0F}};  // solo 2 vértices
    auto back = geometryFromJson(ToolType::PolyBlob, toJson(ToolGeometry(g)));
    EXPECT_FALSE(back.isOk());
}

TEST(PolyBlob, CountsBlobsInsidePolygon) {
    // Fondo claro con dos cuadrados oscuros dentro de una zona pentagonal.
    cv::Mat gray(120, 120, CV_8UC1, cv::Scalar(230));
    cv::rectangle(gray, cv::Rect(30, 30, 12, 12), cv::Scalar(20), cv::FILLED);
    cv::rectangle(gray, cv::Rect(60, 55, 12, 12), cv::Scalar(20), cv::FILLED);
    // Un tercer cuadrado FUERA del polígono no debe contarse.
    cv::rectangle(gray, cv::Rect(100, 100, 12, 12), cv::Scalar(20), cv::FILLED);

    PolyBlobGeometry g;
    g.vertices = {{20.0F, 20.0F}, {85.0F, 20.0F}, {85.0F, 85.0F}, {20.0F, 85.0F}};
    g.minArea = 20.0F;
    g.darkBlobs = true;
    auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::PolyBlob, ToolGeometry(g), 2, 2));
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_EQ(static_cast<int>(result.value().measured), 2);
    EXPECT_TRUE(result.value().ok);
}

TEST(TranslateGeometry, ShiftsAllPointsOfEachType) {
    const cv::Point2f d{15.0F, -10.0F};

    ToolGeometry caliper = CaliperGeometry{{10.0F, 20.0F}, {110.0F, 25.0F}, 8.0F};
    translateGeometry(caliper, d);
    EXPECT_FLOAT_EQ(std::get<CaliperGeometry>(caliper).p0.x, 25.0F);
    EXPECT_FLOAT_EQ(std::get<CaliperGeometry>(caliper).p1.y, 15.0F);

    ToolGeometry circle = CircleGeometry{{50.0F, 50.0F}, 30.0F};
    const float r0 = std::get<CircleGeometry>(circle).radius;
    translateGeometry(circle, d);
    EXPECT_FLOAT_EQ(std::get<CircleGeometry>(circle).center.x, 65.0F);
    EXPECT_FLOAT_EQ(std::get<CircleGeometry>(circle).radius, r0);  // el radio no cambia

    ToolGeometry poly = PolyBlobGeometry{{{0.0F, 0.0F}, {10.0F, 0.0F}, {5.0F, 10.0F}}, 20.0F, true};
    translateGeometry(poly, d);
    const auto& pv = std::get<PolyBlobGeometry>(poly).vertices;
    EXPECT_FLOAT_EQ(pv[0].x, 15.0F);
    EXPECT_FLOAT_EQ(pv[2].y, 0.0F);

    ToolGeometry angle = AngleGeometry{{50.0F, 50.0F}, {90.0F, 50.0F}, {50.0F, 10.0F}};
    translateGeometry(angle, d);
    EXPECT_FLOAT_EQ(std::get<AngleGeometry>(angle).vertex.x, 65.0F);
    EXPECT_FLOAT_EQ(std::get<AngleGeometry>(angle).end1.y, 0.0F);
}

TEST(HomographyScale, LengthToolsUsePerPointHomography) {
    const cv::Mat gray(140, 140, CV_8UC1, cv::Scalar(128));
    // Regla de 100 px en imagen (fixture identidad => coords de pieza = imagen).
    const RulerGeometry g{{10.0F, 10.0F}, {110.0F, 10.0F}};
    // Homografía fronto-paralela: 0.5 mm por px => 100 px = 50 mm.
    const cv::Mat imageToMm =
        (cv::Mat_<double>(3, 3) << 0.5, 0, 0, 0, 0.5, 0, 0, 0, 1);

    auto result = runTool(gray, kIdentity,
                          makeConfig(ToolType::Ruler, ToolGeometry(g), 90, 110), 0.0,
                          LengthUnit::Auto, imageToMm);
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_NEAR(result.value().measured, 100.0, 1e-6);  // el principal sigue en px
    // Los mm salen de la homografía, no de una escala constante.
    EXPECT_NE(result.value().detail.find("50.00mm"), std::string::npos);

    // Sin homografía pero con escala constante 0.25 mm/px => 25 mm.
    result = runTool(gray, kIdentity, makeConfig(ToolType::Ruler, ToolGeometry(g), 90, 110),
                     0.25, LengthUnit::Auto);
    ASSERT_TRUE(result.isOk());
    EXPECT_NE(result.value().detail.find("25.00mm"), std::string::npos);
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
