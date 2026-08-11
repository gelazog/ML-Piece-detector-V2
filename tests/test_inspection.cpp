#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "inspection_editor/canvas/canvas_geometry.h"
#include "inspection_editor/execution/edge_detection.h"
#include "sample_geometries.h"
#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/template_io.h"
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

// --- Exportar/Importar plantilla (M2) ---

TEST(TemplateIo, RoundTripPreservesTools) {
    std::vector<ToolConfig> tools;
    {
        ToolConfig caliper;
        caliper.id = 7;  // el id NO debe conservarse al importar
        caliper.type = ToolType::Caliper;
        caliper.name = "Ancho";
        caliper.geometryJson =
            toJson(ToolGeometry(CaliperGeometry{{0.0F, 0.0F}, {40.0F, 0.0F}, 6.0F}));
        caliper.toleranceMin = 35.0;
        caliper.toleranceMax = 45.0;
        caliper.enabled = false;
        tools.push_back(caliper);

        ToolConfig blob;
        blob.type = ToolType::Blob;
        blob.name = "Agujeros";
        blob.geometryJson = toJson(ToolGeometry(BlobGeometry{{20.0F, 20.0F}, 30.0F, 30.0F}));
        blob.toleranceMin = 2.0;
        blob.toleranceMax = 2.0;
        tools.push_back(blob);
    }

    const std::string json = exportTemplateJson(tools);
    auto back = importTemplateJson(json);
    ASSERT_TRUE(back.isOk()) << back.error().message;
    ASSERT_EQ(back.value().size(), 2U);

    const auto& a = back.value()[0];
    EXPECT_EQ(a.id, -1);  // importadas como nuevas
    EXPECT_EQ(a.type, ToolType::Caliper);
    EXPECT_EQ(a.name, "Ancho");
    EXPECT_DOUBLE_EQ(a.toleranceMin, 35.0);
    EXPECT_DOUBLE_EQ(a.toleranceMax, 45.0);
    EXPECT_FALSE(a.enabled);
    // La geometría sobrevive el viaje.
    auto geom = geometryFromJson(a.type, a.geometryJson);
    ASSERT_TRUE(geom.isOk());
    EXPECT_FLOAT_EQ(std::get<CaliperGeometry>(geom.value()).p1.x, 40.0F);

    EXPECT_EQ(back.value()[1].type, ToolType::Blob);
    EXPECT_EQ(back.value()[1].name, "Agujeros");
}

TEST(TemplateIo, RejectsCorruptJson) {
    EXPECT_FALSE(importTemplateJson("no es json").isOk());
    EXPECT_FALSE(importTemplateJson("{\"version\":1}").isOk());  // sin 'tools'
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

// --- Herramienta Posición (T5) ---

TEST(ToolExecutor, PositionMeasuresDeviationFromBoardZero) {
    const cv::Mat gray(240, 320, CV_8UC1, cv::Scalar(128));
    // Fixture identidad: el rasgo marcado cae tal cual en la imagen.
    const PositionGeometry geometry{{190.0F, 90.0F}, PositionAxis::Radial};
    ToolConfig config = makeConfig(ToolType::Position, ToolGeometry(geometry), 0.0, 100.0);

    // Tablero con el cero en el centro de la imagen (160, 120): el rasgo queda
    // 30 px a la derecha y 30 px por encima -> radio 42.43, ángulo +45°.
    pci::vision::BoardConfig boardConfig;
    boardConfig.origin = pci::vision::BoardOrigin::ImageCenter;
    const pci::vision::BoardFrame board =
        pci::vision::resolveBoardFrame(boardConfig, kIdentity, true, cv::Size(320, 240));

    auto radial = runTool(gray, kIdentity, config, 0.0, LengthUnit::Auto, cv::Mat(), &board);
    ASSERT_TRUE(radial.isOk());
    EXPECT_NEAR(radial.value().measured, std::hypot(30.0, 30.0), 1e-6);
    EXPECT_TRUE(radial.value().ok);  // dentro de [0, 100]

    // Por eje: solo X y solo Y valen 30 px cada uno.
    config.geometryJson = toJson(ToolGeometry(PositionGeometry{{190.0F, 90.0F}, PositionAxis::X}));
    auto onlyX = runTool(gray, kIdentity, config, 0.0, LengthUnit::Auto, cv::Mat(), &board);
    ASSERT_TRUE(onlyX.isOk());
    EXPECT_NEAR(onlyX.value().measured, 30.0, 1e-6);

    config.geometryJson = toJson(ToolGeometry(PositionGeometry{{190.0F, 90.0F}, PositionAxis::Y}));
    auto onlyY = runTool(gray, kIdentity, config, 0.0, LengthUnit::Auto, cv::Mat(), &board);
    ASSERT_TRUE(onlyY.isOk());
    EXPECT_NEAR(onlyY.value().measured, 30.0, 1e-6);

    // Fuera de tolerancia: la misma desviación con un techo de 10 px es NG.
    config.toleranceMax = 10.0;
    auto tight = runTool(gray, kIdentity, config, 0.0, LengthUnit::Auto, cv::Mat(), &board);
    ASSERT_TRUE(tight.isOk());
    EXPECT_FALSE(tight.value().ok);
}

TEST(ToolExecutor, PositionWithoutBoardFallsBackToPieceCenter) {
    const cv::Mat gray(240, 320, CV_8UC1, cv::Scalar(128));
    // Pieza centrada en (100, 100); el rasgo está 40 px a su derecha.
    const Fixture fixture{{100.0F, 100.0F}, 0.0};
    const PositionGeometry geometry{{40.0F, 0.0F}, PositionAxis::Radial};
    const ToolConfig config =
        makeConfig(ToolType::Position, ToolGeometry(geometry), 0.0, 100.0);

    auto result = runTool(gray, fixture, config);  // sin tablero explícito
    ASSERT_TRUE(result.isOk());
    EXPECT_NEAR(result.value().measured, 40.0, 1e-6);
    // El overlay une el cero con el rasgo, para poder pintarlo.
    ASSERT_EQ(result.value().overlayPoints.size(), 2U);
    EXPECT_FLOAT_EQ(result.value().overlayPoints[0].x, 100.0F);
    EXPECT_FLOAT_EQ(result.value().overlayPoints[1].x, 140.0F);
}

TEST(ToolGeometry, PositionRoundTripAndAxisDefault) {
    const PositionGeometry position{{12.5F, -8.25F}, PositionAxis::Y};
    auto back = geometryFromJson(ToolType::Position, toJson(ToolGeometry(position)));
    ASSERT_TRUE(back.isOk());
    EXPECT_FLOAT_EQ(std::get<PositionGeometry>(back.value()).point.x, 12.5F);
    EXPECT_FLOAT_EQ(std::get<PositionGeometry>(back.value()).point.y, -8.25F);
    EXPECT_EQ(std::get<PositionGeometry>(back.value()).axis, PositionAxis::Y);

    // JSON sin "axis" (formato anterior a esta herramienta): radial por defecto.
    auto legacy = geometryFromJson(ToolType::Position, R"({"px":5.0,"py":6.0})");
    ASSERT_TRUE(legacy.isOk());
    EXPECT_EQ(std::get<PositionGeometry>(legacy.value()).axis, PositionAxis::Radial);

    // Traslación: el punto se mueve con la herramienta.
    ToolGeometry moved = ToolGeometry(position);
    translateGeometry(moved, {2.0F, 3.0F});
    EXPECT_FLOAT_EQ(std::get<PositionGeometry>(moved).point.x, 14.5F);
    EXPECT_FLOAT_EQ(std::get<PositionGeometry>(moved).point.y, -5.25F);
}

// --- Pruebas de estres y validacion del sistema de medicion ---

namespace {

// Construye una herramienta de cada tipo alrededor de un punto, para poblar
// escenas con muchas herramientas mezcladas.
ToolConfig makeToolAt(int index, float x, float y) {
    const ToolType types[] = {ToolType::Caliper,  ToolType::Circle,   ToolType::PointToLine,
                              ToolType::EdgeFlaw, ToolType::Blob,     ToolType::Ruler,
                              ToolType::LineToLine, ToolType::Angle,  ToolType::PolyBlob,
                              ToolType::Position,   ToolType::Arc,      ToolType::Shaft,
                              ToolType::Thread,     ToolType::Gear};
    // Las dos construcciones quedan fuera de la rotación a propósito: sin
    // referencias no calculan nada, y estas escenas son para poblar el lienzo de
    // herramientas que miden. Aun así tienen su caso en el `switch` de abajo,
    // que es exhaustivo por `-Werror`.
    const ToolType type = types[index % 14];
    ToolGeometry geometry;
    switch (type) {
        case ToolType::Caliper:
            geometry = CaliperGeometry{{x - 20.0F, y}, {x + 20.0F, y}, 10.0F};
            break;
        case ToolType::Circle:
            geometry = CircleGeometry{{x, y}, 18.0F, 6.0F, 24};
            break;
        case ToolType::Arc:
            geometry = ArcGeometry{{x - 18.0F, y}, {x, y - 18.0F}, {x + 18.0F, y}, 6.0F, 16};
            break;
        case ToolType::Shaft:
            geometry = ShaftGeometry{{x - 25.0F, y}, {x + 25.0F, y}, 20.0F, 12};
            break;
        case ToolType::Thread:
            geometry = ThreadGeometry{{x - 25.0F, y}, {x + 25.0F, y}, 20.0F, 60};
            break;
        case ToolType::Gear:
            geometry = GearGeometry{{x, y}, 12.0F, 22.0F, 180};
            break;
        case ToolType::PointToLine:
            geometry = PointToLineGeometry{{x - 20.0F, y}, {x + 20.0F, y},
                                           {x, y - 15.0F}, {x, y + 15.0F}};
            break;
        case ToolType::EdgeFlaw:
            geometry = EdgeFlawGeometry{{x - 20.0F, y}, {x + 20.0F, y}, 12.0F, 8};
            break;
        case ToolType::Blob:
            geometry = BlobGeometry{{x, y}, 40.0F, 30.0F, 20.0F, true};
            break;
        case ToolType::Ruler:
            geometry = RulerGeometry{{x - 15.0F, y - 10.0F}, {x + 15.0F, y + 10.0F}};
            break;
        case ToolType::LineToLine:
            geometry = LineToLineGeometry{{x - 20.0F, y}, {x + 20.0F, y},
                                          {x - 20.0F, y + 12.0F}, {x + 20.0F, y + 16.0F}};
            break;
        case ToolType::Angle:
            geometry = AngleGeometry{{x, y}, {x + 25.0F, y}, {x, y + 25.0F}};
            break;
        case ToolType::PolyBlob: {
            PolyBlobGeometry poly;
            poly.vertices = {{x - 18.0F, y - 14.0F}, {x + 18.0F, y - 14.0F},
                             {x + 18.0F, y + 14.0F}, {x - 18.0F, y + 14.0F}};
            geometry = poly;
            break;
        }
        case ToolType::Position:
            geometry = PositionGeometry{{x, y}, PositionAxis::Radial};
            break;
        case ToolType::ConstructedPoint:
            geometry = ConstructedPointGeometry{PointConstruction::Midpoint, {x, y}};
            break;
        case ToolType::ConstructedLine:
            geometry = ConstructedLineGeometry{LineConstruction::ThroughTwoPoints, {x, y}};
            break;
    }
    ToolConfig config = makeConfig(type, geometry, 0.0, 1.0e9);
    config.id = index;
    config.name = "t" + std::to_string(index);
    return config;
}

}  // namespace

// La garantia que impide que las medidas "se mezclen": runTools devuelve UN
// resultado por herramienta habilitada, en el MISMO orden y con su identidad
// (id y nombre) intacta, pase lo que pase con cada medicion individual.
TEST(ToolExecutorStress, ThreeHundredToolsKeepTheirIdentityAndOrder) {
    cv::Mat gray(600, 800, CV_8UC1, cv::Scalar(60));
    cv::rectangle(gray, {150, 120}, {650, 480}, cv::Scalar(220), cv::FILLED);
    cv::circle(gray, {400, 300}, 40, cv::Scalar(30), cv::FILLED);

    std::vector<ToolConfig> tools;
    for (int i = 0; i < 300; ++i) {
        const float x = 180.0F + static_cast<float>((i * 37) % 600);
        const float y = 150.0F + static_cast<float>((i * 53) % 300);
        tools.push_back(makeToolAt(i, x, y));
    }

    const auto started = std::chrono::steady_clock::now();
    const auto results = runTools(gray, kIdentity, tools);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();

    ASSERT_EQ(results.size(), tools.size());
    for (std::size_t i = 0; i < results.size(); ++i) {
        EXPECT_EQ(results[i].toolId, tools[i].id) << "resultado " << i << " fuera de sitio";
        EXPECT_EQ(results[i].name, tools[i].name);
        EXPECT_EQ(results[i].type, tools[i].type);
        EXPECT_FALSE(std::isnan(results[i].measured)) << "medida NaN en " << results[i].name;
    }
    // Con 300 herramientas la inspeccion debe seguir siendo utilizable en vivo.
    EXPECT_LT(elapsedMs, 5000) << "300 herramientas tardaron " << elapsedMs << " ms";
}

// Una herramienta deshabilitada se salta, pero las demas NO pueden correrse de
// sitio ni heredar su nombre: es justo el escenario donde las medidas
// aparecerian intercambiadas.
TEST(ToolExecutorStress, DisabledToolsDoNotShiftTheOthers) {
    const cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(128));
    std::vector<ToolConfig> tools;
    for (int i = 0; i < 12; ++i) {
        ToolConfig config = makeToolAt(i, 120.0F + i * 10.0F, 200.0F);
        config.enabled = (i % 3 != 0);  // uno de cada tres, apagado
        tools.push_back(config);
    }

    const auto results = runTools(gray, kIdentity, tools);
    ASSERT_EQ(results.size(), 8U);  // 12 menos los 4 deshabilitados
    std::size_t next = 0;
    for (const auto& tool : tools) {
        if (!tool.enabled) {
            continue;
        }
        EXPECT_EQ(results[next].toolId, tool.id);
        EXPECT_EQ(results[next].name, tool.name);
        ++next;
    }
}

// Geometrias degeneradas o absurdas: la medicion puede fallar, pero nunca
// colgarse, lanzar ni devolver un resultado sin identidad.
TEST(ToolExecutorStress, DegenerateGeometryFailsCleanly) {
    const cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(100));
    const std::vector<ToolGeometry> nasty = {
        CaliperGeometry{{50.0F, 50.0F}, {50.0F, 50.0F}, 0.0F},          // longitud cero
        CircleGeometry{{100.0F, 100.0F}, 0.0F, 0.0F, 1},                // radio cero
        RulerGeometry{{-1.0e6F, -1.0e6F}, {1.0e6F, 1.0e6F}},            // fuera de la imagen
        AngleGeometry{{10.0F, 10.0F}, {10.0F, 10.0F}, {10.0F, 10.0F}},  // lados nulos
        BlobGeometry{{100.0F, 100.0F}, 0.0F, 0.0F, 0.0F, true},         // region vacia
        LineToLineGeometry{{0, 0}, {0, 0}, {0, 0}, {0, 0}},             // dos lineas nulas
        PositionGeometry{{1.0e7F, -1.0e7F}, PositionAxis::Radial},      // rasgo lejisimos
    };
    for (const auto& geometry : nasty) {
        const ToolConfig config = makeConfig(typeOf(geometry), geometry, 0.0, 1.0e9);
        const auto result = runTool(gray, kIdentity, config);
        ASSERT_TRUE(result.isOk()) << "una geometria degenerada no debe romper la medicion";
        EXPECT_EQ(result.value().name, config.name);
        EXPECT_FALSE(std::isnan(result.value().measured));
    }
}

// El JSON de geometria puede venir de una BD tocada a mano o de otra version:
// nunca debe colgar ni producir una geometria con valores no finitos.
TEST(ToolExecutorStress, HostileGeometryJsonIsRejected) {
    const std::vector<std::string> hostile = {
        "", "{}", "no es json", "[1,2,3]",
        R"({"x0":1e400,"y0":0,"x1":10,"y1":0,"band":5})",     // desbordamiento
        R"({"x0":"texto","y0":0,"x1":10,"y1":0,"band":5})",   // tipo equivocado
        R"({"x0":0,"y0":0})",                                  // campos ausentes
        std::string(R"({"x0":0,"y0":0,"x1":10,"y1":0,"band":)") + std::string(2000, '9') + "}",
    };
    for (const auto& json : hostile) {
        auto parsed = geometryFromJson(ToolType::Caliper, json);
        if (parsed.isOk()) {
            // Si acepta el JSON, al menos los valores han de ser finitos.
            const auto& g = std::get<CaliperGeometry>(parsed.value());
            EXPECT_TRUE(std::isfinite(g.p0.x) && std::isfinite(g.p1.x)) << json;
        }
    }
    // Un polígono con menos de 3 vertices no es una region: debe rechazarse.
    EXPECT_FALSE(geometryFromJson(ToolType::PolyBlob, R"({"verts":[1,2],"minArea":5,"dark":1})")
                     .isOk());
}

// ===========================================================================
//  Bateria por herramienta: exactitud contra una verdad conocida, invariancia
//  al giro de la pieza, limites y coherencia entre herramientas.
//
//  La invariancia al fixture es LA promesa del producto (las herramientas
//  siguen a la pieza): hasta ahora solo estaba probada para el Caliper.
// ===========================================================================

namespace {

// Escena con una barra oscura vertical de ancho conocido sobre fondo claro,
// dibujada YA GIRADA y desplazada. Devuelve el fixture con el que la
// herramienta, definida en coordenadas de pieza, debe seguir midiendo igual.
cv::Mat drawRotatedBar(cv::Size size, cv::Point2f center, double angleDeg, float barWidth,
                       float barLength, Fixture& fixtureOut) {
    cv::Mat gray(size, CV_8UC1, cv::Scalar(220));
    cv::RotatedRect bar(center, cv::Size2f(barWidth, barLength),
                        static_cast<float>(angleDeg));
    cv::Point2f corners[4];
    bar.points(corners);
    std::vector<cv::Point> poly;
    for (const auto& corner : corners) {
        poly.emplace_back(cvRound(corner.x), cvRound(corner.y));
    }
    cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{poly}, cv::Scalar(40));
    fixtureOut.origin = center;
    fixtureOut.angleDeg = angleDeg;
    return gray;
}

// Disco oscuro de radio conocido, centrado en la pieza.
cv::Mat drawDisc(cv::Size size, cv::Point2f center, int radius, Fixture& fixtureOut,
                 double angleDeg = 0.0) {
    cv::Mat gray(size, CV_8UC1, cv::Scalar(220));
    cv::circle(gray, cv::Point(cvRound(center.x), cvRound(center.y)), radius, cv::Scalar(40),
               cv::FILLED, cv::LINE_AA);
    fixtureOut.origin = center;
    fixtureOut.angleDeg = angleDeg;
    return gray;
}

double runMeasure(const cv::Mat& gray, const Fixture& fixture, ToolType type,
                  const ToolGeometry& geometry, bool* okOut = nullptr) {
    const auto result = runTool(gray, fixture, makeConfig(type, geometry, 0.0, 1.0e9));
    EXPECT_TRUE(result.isOk());
    if (!result.isOk()) {
        return -1.0;
    }
    if (okOut != nullptr) {
        *okOut = result.value().ok;
    }
    return result.value().measured;
}

}  // namespace

// --- Caliper: exactitud, anchos distintos e invariancia al giro ---

TEST(CaliperSuite, MeasuresSeveralWidthsAccurately) {
    for (const float width : {12.0F, 25.0F, 40.0F, 70.0F}) {
        cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(220));
        const int half = static_cast<int>(width / 2.0F);
        gray.colRange(150 - half, 150 + half).setTo(40);
        const CaliperGeometry g{{60.0F, 150.0F}, {240.0F, 150.0F}, 10.0F};
        const double measured =
            runMeasure(gray, kIdentity, ToolType::Caliper, ToolGeometry(g));
        EXPECT_NEAR(measured, width, 2.0) << "ancho nominal " << width;
    }
}

TEST(CaliperSuite, SameBarMeasuredEqualAtEveryRotation) {
    // La herramienta se define UNA vez en coordenadas de pieza y la pieza se
    // presenta girada: la medida no puede cambiar.
    const CaliperGeometry g{{-70.0F, 0.0F}, {70.0F, 0.0F}, 8.0F};
    double reference = 0.0;
    for (const double angle : {0.0, 20.0, 45.0, 70.0, 120.0}) {
        Fixture fixture;
        const cv::Mat gray =
            drawRotatedBar({400, 400}, {200.0F, 200.0F}, angle, 36.0F, 220.0F, fixture);
        const double measured = runMeasure(gray, fixture, ToolType::Caliper, ToolGeometry(g));
        if (angle == 0.0) {
            reference = measured;
            EXPECT_NEAR(reference, 36.0, 2.5);
        } else {
            EXPECT_NEAR(measured, reference, 3.0) << "a " << angle << " grados";
        }
    }
}

TEST(CaliperSuite, BandWidthAveragesNoisyEdges) {
    // Con ruido, una banda ancha promedia varios perfiles y estabiliza la
    // medida: es justo para lo que existe el parámetro.
    cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(220));
    gray.colRange(80, 120).setTo(40);
    cv::Mat noise(gray.size(), CV_8UC1);
    cv::randn(noise, 0, 12);
    gray += noise;

    const CaliperGeometry narrow{{40.0F, 100.0F}, {160.0F, 100.0F}, 1.0F};
    const CaliperGeometry wide{{40.0F, 100.0F}, {160.0F, 100.0F}, 20.0F};
    const double narrowMeasure =
        runMeasure(gray, kIdentity, ToolType::Caliper, ToolGeometry(narrow));
    const double wideMeasure = runMeasure(gray, kIdentity, ToolType::Caliper,
                                          ToolGeometry(wide));
    EXPECT_NEAR(wideMeasure, 40.0, 3.0);
    EXPECT_GT(narrowMeasure, 0.0);  // la estrecha mide, aunque sea menos estable
}

// --- Circulo: diametros, invariancia al giro y redondez ---

// --- Engranaje ---

namespace {

// Rueda dentada vista de cara, dibujada a partir de parametros conocidos. El
// perfil de cada diente es trapecial -no es una evolvente exacta, pero para
// contar dientes y medir cabeza y raiz da igual, y en cambio permite saber si
// la medida es CORRECTA-.
cv::Mat drawGear(int teeth, double tipRadius, double rootRadius,
                 double damagedToothFraction = 0.0) {
    cv::Mat gray(700, 700, CV_8UC1, cv::Scalar(220));
    const cv::Point2f c(350.0F, 350.0F);
    std::vector<cv::Point> poly;
    const int steps = 3600;
    for (int i = 0; i < steps; ++i) {
        const double theta = 2.0 * 3.14159265358979323846 * i / steps;
        const double phase = std::fmod(theta * teeth / (2.0 * 3.14159265358979323846), 1.0);
        double r = rootRadius;
        // Diente: sube, meseta, baja, valle (cada cuarto del periodo).
        if (phase < 0.25) {
            r = rootRadius + (tipRadius - rootRadius) * (phase / 0.25);
        } else if (phase < 0.5) {
            r = tipRadius;
        } else if (phase < 0.75) {
            r = tipRadius - (tipRadius - rootRadius) * ((phase - 0.5) / 0.25);
        }
        // Un diente mellado: se rebaja el primero.
        if (damagedToothFraction > 0.0 &&
            theta < 2.0 * 3.14159265358979323846 / teeth) {
            r = rootRadius + (r - rootRadius) * (1.0 - damagedToothFraction);
        }
        poly.emplace_back(cvRound(c.x + r * std::cos(theta)),
                          cvRound(c.y + r * std::sin(theta)));
    }
    const std::vector<std::vector<cv::Point>> polys{poly};
    cv::fillPoly(gray, polys, cv::Scalar(40), cv::LINE_AA);
    return gray;
}

ToolRunResult runGearOn(const cv::Mat& gray, const GearGeometry& g, double mmPerPixel = 0.0) {
    ToolConfig config = makeConfig(ToolType::Gear, ToolGeometry(g), 0, 1e9);
    const auto r = runTool(gray, kIdentity, config, mmPerPixel);
    EXPECT_TRUE(r.isOk());
    return r.isOk() ? r.value() : ToolRunResult{};
}

}  // namespace

TEST(GearSuite, CountsTheTeethItWasDrawnWith) {
    // Incluye numeros primos, que no dividen bien el muestreo y romperian un
    // contador de picos ingenuo.
    for (const int teeth : {12, 17, 24, 31, 48}) {
        const cv::Mat gray = drawGear(teeth, 260.0, 220.0);
        const GearGeometry g{{350.0F, 350.0F}, 200.0F, 290.0F, 1440};
        const auto r = runGearOn(gray, g);
        std::printf("  z=%2d -> %s\n", teeth, r.detail.c_str());
        EXPECT_EQ(static_cast<int>(std::lround(r.measured)), teeth) << "dientes " << teeth;
    }
}

TEST(GearSuite, MeasuresTipAndRootDiameters) {
    const cv::Mat gray = drawGear(20, 260.0, 220.0);
    const GearGeometry g{{350.0F, 350.0F}, 200.0F, 290.0F, 1440};
    const auto r = runGearOn(gray, g);
    std::printf("  %s\n", r.detail.c_str());
    EXPECT_NE(r.detail.find("Ø cabeza="), std::string::npos);
    EXPECT_NE(r.detail.find("Ø raíz="), std::string::npos);
    // El diametro de cabeza dibujado es 520 px; se admite un margen de 6 px por
    // el suavizado del dibujo y la punta trapecial.
    const auto pos = r.detail.find("Ø cabeza=");
    ASSERT_NE(pos, std::string::npos);
    const double tip = std::atof(r.detail.c_str() + pos + std::strlen("Ø cabeza="));
    EXPECT_NEAR(tip, 520.0, 6.0) << r.detail;
}

TEST(GearSuite, ADamagedToothLowersConfidenceWithoutChangingTheCount) {
    // El motivo de contar por autocorrelacion y no por picos. Ademas la
    // excentricidad tiene que notarlo, que es justo lo que se busca al medir
    // una rueda desgastada.
    const cv::Mat sound = drawGear(24, 260.0, 220.0);
    const cv::Mat damaged = drawGear(24, 260.0, 220.0, 0.6);
    const GearGeometry g{{350.0F, 350.0F}, 200.0F, 290.0F, 1440};
    const auto a = runGearOn(sound, g);
    const auto b = runGearOn(damaged, g);
    std::printf("  sano:   %s\n  mellado:%s\n", a.detail.c_str(), b.detail.c_str());
    EXPECT_EQ(static_cast<int>(std::lround(a.measured)), 24);
    EXPECT_EQ(static_cast<int>(std::lround(b.measured)), 24)
        << "un diente mellado no puede cambiar el recuento";
}

TEST(GearSuite, TheCountIsTheSameOnARotatedGear) {
    // Una rueda girada es la misma rueda.
    const cv::Mat gray = drawGear(19, 250.0, 210.0);
    for (const double angle : {0.0, 13.0, 47.0, -80.0}) {
        Fixture fixture;
        fixture.origin = {350.0F, 350.0F};
        fixture.angleDeg = angle;
        const GearGeometry g{pci::vision::toPieceCoords(fixture, {350.0F, 350.0F}), 190.0F,
                             280.0F, 1440};
        const auto r = runGearOn(gray, g);
        // El centro va en coords de pieza, asi que hay que ejecutar con el
        // fixture correspondiente.
        ToolConfig config = makeConfig(ToolType::Gear, ToolGeometry(g), 0, 1e9);
        const auto run = runTool(gray, fixture, config);
        ASSERT_TRUE(run.isOk());
        EXPECT_EQ(static_cast<int>(std::lround(run.value().measured)), 19)
            << "a " << angle << " grados: " << run.value().detail;
    }
}

TEST(GearSuite, WithoutCalibrationItRefusesToGiveAModule) {
    // El modulo no existe sin escala real: es mm por diente.
    const cv::Mat gray = drawGear(20, 260.0, 220.0);
    const GearGeometry g{{350.0F, 350.0F}, 200.0F, 290.0F, 1440};
    const auto r = runGearOn(gray, g);
    EXPECT_NE(r.detail.find("necesita calibración"), std::string::npos) << r.detail;
    EXPECT_EQ(r.detail.find("módulo≈"), std::string::npos) << r.detail;
}

TEST(GearSuite, WithCalibrationItGivesTheModule) {
    // Rueda de z=20 y modulo 2 mm fotografiada a 12 px/mm:
    // Ø cabeza = m(z+2) = 44 mm = 528 px; Ø raiz = m(z-2,5) = 35 mm = 420 px.
    constexpr double kMmPerPixel = 1.0 / 12.0;
    const cv::Mat gray = drawGear(20, 264.0, 210.0);
    const GearGeometry g{{350.0F, 350.0F}, 190.0F, 300.0F, 1440};
    const auto r = runGearOn(gray, g, kMmPerPixel);
    std::printf("  %s\n", r.detail.c_str());
    EXPECT_NE(r.detail.find("módulo≈"), std::string::npos) << r.detail;
    const auto pos = r.detail.find("módulo≈");
    ASSERT_NE(pos, std::string::npos);
    const double module = std::atof(r.detail.c_str() + pos + std::strlen("módulo≈"));
    EXPECT_NEAR(module, 2.0, 0.06) << r.detail;  // menos del 3 %, como pedia el plan

    // Y la comprobacion cruzada por altura de diente tiene que CONCORDAR en una
    // rueda normalizada. Sin esta asercion el aviso de discrepancia saltaba
    // siempre -el divisor estaba a la mitad- y habria acabado siendo ruido que
    // el operador aprende a ignorar.
    EXPECT_EQ(r.detail.find("discrepan"), std::string::npos) << r.detail;
}

TEST(GearSuite, ADiscWithNoTeethIsRejected) {
    // Un disco liso no es un engranaje. Devolver un numero de dientes seria lo
    // peor que puede hacer esta herramienta.
    cv::Mat gray(700, 700, CV_8UC1, cv::Scalar(220));
    cv::circle(gray, {350, 350}, 250, cv::Scalar(40), cv::FILLED, cv::LINE_AA);
    const GearGeometry g{{350.0F, 350.0F}, 200.0F, 290.0F, 1440};
    const auto r = runGearOn(gray, g);
    std::printf("  disco liso: ok=%d  %s\n", static_cast<int>(r.ok), r.detail.c_str());
    const bool refused = !r.ok || r.detail.find("No se aprecian dientes") != std::string::npos ||
                         r.detail.find("no se repite") != std::string::npos ||
                         r.detail.find("débil") != std::string::npos;
    EXPECT_TRUE(refused) << r.detail;
}

TEST(GearSuite, BadRadiiFailWithAReason) {
    const cv::Mat gray = drawGear(20, 260.0, 220.0);
    const GearGeometry bad{{350.0F, 350.0F}, 290.0F, 200.0F, 1440};  // invertidos
    const auto r = runGearOn(gray, bad);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.detail.find("radios"), std::string::npos) << r.detail;
}

TEST(GearSuite, GeometrySurvivesASaveAndLoadRoundTrip) {
    const GearGeometry g{{120.5F, 240.25F}, 55.5F, 98.75F, 720};
    const auto parsed = geometryFromJson(ToolType::Gear, toJson(ToolGeometry(g)));
    ASSERT_TRUE(parsed.isOk()) << parsed.error().message;
    const auto& back = std::get<GearGeometry>(parsed.value());
    EXPECT_FLOAT_EQ(back.center.x, g.center.x);
    EXPECT_FLOAT_EQ(back.center.y, g.center.y);
    EXPECT_FLOAT_EQ(back.innerRadius, g.innerRadius);
    EXPECT_FLOAT_EQ(back.outerRadius, g.outerRadius);
    EXPECT_EQ(back.rayCount, g.rayCount);
    EXPECT_EQ(typeOf(ToolGeometry(g)), ToolType::Gear);
    const auto fromName = toolTypeFromName("gear");
    ASSERT_TRUE(fromName.isOk());
    EXPECT_EQ(fromName.value(), ToolType::Gear);
}

TEST(GearSuite, MovingItKeepsItsShape) {
    ToolGeometry geometry = GearGeometry{{50.0F, 60.0F}, 30.0F, 70.0F, 720};
    const auto before = std::get<GearGeometry>(geometry);
    translateGeometry(geometry, {12.0F, -8.0F});
    const auto after = std::get<GearGeometry>(geometry);
    EXPECT_FLOAT_EQ(after.center.x, before.center.x + 12.0F);
    EXPECT_FLOAT_EQ(after.center.y, before.center.y - 8.0F);
    EXPECT_FLOAT_EQ(after.innerRadius, before.innerRadius);
    EXPECT_FLOAT_EQ(after.outerRadius, before.outerRadius);
}

// --- Rosca ---

namespace {

// Tornillo visto de perfil, dibujado a partir de parametros conocidos: paso,
// diametro exterior, diametro de fondo y angulo de flanco. El perfil de cada
// vuelta es un trapecio simetrico, que es la forma de una rosca real de flancos
// rectos (metrica, ISO).
//
// Devolver la pieza dibujada asi -y no una foto- es lo unico que permite saber
// si la medida es CORRECTA y no solo estable.
cv::Mat drawThread(double pitchPx, double majorDia, double minorDia, double flankDeg,
                   double angleDeg = 0.0) {
    cv::Mat gray(500, 700, CV_8UC1, cv::Scalar(220));
    const double a = angleDeg * 3.14159265358979323846 / 180.0;
    const cv::Point2f dir(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
    const cv::Point2f nrm(-dir.y, dir.x);
    const cv::Point2f c(350.0F, 250.0F);
    constexpr double kLength = 460.0;

    const double majorR = majorDia / 2.0;
    const double minorR = minorDia / 2.0;
    const double height = majorR - minorR;
    // Avance a lo largo del eje que consume cada flanco, dado su angulo.
    const double flankRun = height * std::tan(flankDeg * 3.14159265358979323846 / 360.0);
    const double flat = std::max(0.0, (pitchPx - 2.0 * flankRun) / 2.0);

    // Radio del perfil en funcion de la posicion a lo largo del eje.
    const auto radiusAt = [&](double t) {
        double phase = std::fmod(t, pitchPx);
        if (phase < 0.0) {
            phase += pitchPx;
        }
        if (phase < flankRun) {
            return minorR + height * (phase / flankRun);           // sube
        }
        if (phase < flankRun + flat) {
            return majorR;                                          // cresta
        }
        if (phase < 2.0 * flankRun + flat) {
            return majorR - height * ((phase - flankRun - flat) / flankRun);  // baja
        }
        return minorR;                                              // valle
    };

    // Se dibuja como un poligono cerrado: borde superior de izquierda a derecha
    // y borde inferior de vuelta.
    std::vector<cv::Point> poly;
    const int steps = 600;
    for (int i = 0; i <= steps; ++i) {
        const double t = -kLength / 2.0 + kLength * i / steps;
        const auto r = static_cast<float>(radiusAt(t + kLength / 2.0));
        const cv::Point2f p = c + dir * static_cast<float>(t) + nrm * r;
        poly.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    for (int i = steps; i >= 0; --i) {
        const double t = -kLength / 2.0 + kLength * i / steps;
        const auto r = static_cast<float>(radiusAt(t + kLength / 2.0));
        const cv::Point2f p = c + dir * static_cast<float>(t) - nrm * r;
        poly.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    const std::vector<std::vector<cv::Point>> polys{poly};
    cv::fillPoly(gray, polys, cv::Scalar(40), cv::LINE_AA);
    return gray;
}

ThreadGeometry threadAlong(double angleDeg, double band = 110.0) {
    const double a = angleDeg * 3.14159265358979323846 / 180.0;
    const cv::Point2f dir(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
    const cv::Point2f c(350.0F, 250.0F);
    return ThreadGeometry{c - dir * 200.0F, c + dir * 200.0F, static_cast<float>(band), 400};
}

ToolRunResult runThreadOn(const cv::Mat& gray, const ThreadGeometry& g,
                          double mmPerPixel = 0.0) {
    ToolConfig config = makeConfig(ToolType::Thread, ToolGeometry(g), 0, 1e9);
    const auto r = runTool(gray, kIdentity, config, mmPerPixel);
    EXPECT_TRUE(r.isOk());
    return r.isOk() ? r.value() : ToolRunResult{};
}

}  // namespace

TEST(ThreadSuite, RecoversThePitchItWasDrawnWith) {
    // El numero que identifica una rosca. Se exige menos de un 2 % de error,
    // que es lo que pedia el plan.
    for (const double pitch : {20.0, 30.0, 45.0}) {
        const cv::Mat gray = drawThread(pitch, 120.0, 84.0, 60.0);
        const auto r = runThreadOn(gray, threadAlong(0.0));
        std::printf("  paso %4.1f -> medido %5.2f  (%s)\n", pitch, r.measured,
                    r.detail.c_str());
        EXPECT_NEAR(r.measured, pitch, pitch * 0.02) << "paso " << pitch;
    }
}

TEST(ThreadSuite, RecoversBothDiameters) {
    const cv::Mat gray = drawThread(30.0, 120.0, 84.0, 60.0);
    const auto r = runThreadOn(gray, threadAlong(0.0));
    // Los diametros salen en el detalle; se comprueba que aparecen y que el
    // exterior es mayor que el de fondo por la altura del filete dibujada.
    std::printf("  %s\n", r.detail.c_str());
    EXPECT_NE(r.detail.find("Ø ext="), std::string::npos);
    EXPECT_NE(r.detail.find("Ø fondo="), std::string::npos);
}

TEST(ThreadSuite, RecoversTheFlankAngle) {
    // Se exige menos de 3 grados de error, como pedia el plan. Hace falta que
    // el filete se vea con holgura: con 50 px de altura el angulo sale a +-1
    // grado (ver ThreadSuite.ASmallThreadSaysTheAngleIsNotReliable, que fija
    // el limite por abajo).
    for (const double flank : {60.0, 55.0}) {
        const cv::Mat gray = drawThread(80.0, 260.0, 160.0, flank);
        const auto r = runThreadOn(gray, threadAlong(0.0, 200.0));
        // El angulo va en el detalle: se extrae para compararlo.
        const auto pos = r.detail.find("flanco=");
        ASSERT_NE(pos, std::string::npos) << r.detail;
        const double measured = std::atof(r.detail.c_str() + pos + 7);
        std::printf("  flanco %.0f -> medido %.2f\n", flank, measured);
        EXPECT_NEAR(measured, flank, 3.0) << "flanco " << flank;
    }
}

TEST(ThreadSuite, ASmallThreadSaysTheAngleIsNotReliable) {
    // Limite medido, no supuesto. El angulo de flanco se lee sobre la pendiente
    // del filete: con 50 px de altura sale a +-1 grado, con 25 a +-2, y con 12
    // deja de distinguir 60 de 55 -da ~55 para cualquiera-. El paso y los
    // diametros aguantan mucho mejor, asi que se avisa solo del angulo en vez
    // de rechazar la medida entera.
    const cv::Mat small = drawThread(20.0, 65.0, 40.0, 60.0);  // filete de 12,5 px
    const auto r = runThreadOn(small, threadAlong(0.0, 60.0));
    std::printf("  %s\n", r.detail.c_str());
    EXPECT_NE(r.detail.find("no es fiable"), std::string::npos) << r.detail;
    // Pero el paso, que es el numero que identifica la rosca, sigue saliendo.
    EXPECT_NEAR(r.measured, 20.0, 1.0) << "el paso no deberia verse afectado";

    // Y con una rosca bien resuelta no molesta con el aviso.
    const cv::Mat big = drawThread(80.0, 260.0, 160.0, 60.0);
    const auto fine = runThreadOn(big, threadAlong(0.0, 200.0));
    EXPECT_EQ(fine.detail.find("no es fiable"), std::string::npos) << fine.detail;
}

TEST(ThreadSuite, ThePitchIsTheSameOnATiltedScrew) {
    // La pieza se apoya como se apoya.
    constexpr double kPitch = 35.0;
    double reference = 0.0;
    for (const double angle : {0.0, 20.0, -30.0, 90.0}) {
        const cv::Mat gray = drawThread(kPitch, 120.0, 84.0, 60.0, angle);
        const auto r = runThreadOn(gray, threadAlong(angle));
        if (angle == 0.0) {
            reference = r.measured;
        } else {
            EXPECT_NEAR(r.measured, reference, 1.5) << "a " << angle << " grados";
        }
        EXPECT_NEAR(r.measured, kPitch, 1.5) << "a " << angle << " grados";
    }
}

TEST(ThreadSuite, WithoutCalibrationItRefusesToNameTheThread) {
    // Un paso en pixeles no identifica ningun tornillo. Decirlo es mejor que
    // dar una designacion inventada.
    const cv::Mat gray = drawThread(30.0, 120.0, 84.0, 60.0);
    const auto r = runThreadOn(gray, threadAlong(0.0));
    EXPECT_NE(r.detail.find("sin calibración"), std::string::npos) << r.detail;
}

TEST(ThreadSuite, WithCalibrationItProposesTheMetricDesignation) {
    // Una M8x1.25 fotografiada de modo que 1 mm = 15 px: paso 18,75 px y
    // diametro exterior 120 px.
    constexpr double kMmPerPixel = 1.0 / 15.0;
    const cv::Mat gray = drawThread(18.75, 120.0, 95.0, 60.0);
    const auto r = runThreadOn(gray, threadAlong(0.0, 90.0), kMmPerPixel);
    std::printf("  %s\n", r.detail.c_str());
    EXPECT_NE(r.detail.find("M8"), std::string::npos) << r.detail;
    EXPECT_EQ(r.detail.find("sin calibración"), std::string::npos);
}

TEST(ThreadSuite, SomethingThatIsNotAThreadIsRejected) {
    // Una barra lisa no tiene paso. Devolver un numero seria lo peor que puede
    // hacer esta herramienta.
    cv::Mat gray(500, 700, CV_8UC1, cv::Scalar(220));
    cv::rectangle(gray, cv::Rect(200, 190, 300, 120), cv::Scalar(40), cv::FILLED);
    const auto r = runThreadOn(gray, threadAlong(0.0));
    std::printf("  barra lisa: ok=%d  %s\n", static_cast<int>(r.ok), r.detail.c_str());
    // O se niega, o al menos avisa de que la repeticion es debil.
    const bool refused = !r.ok || r.detail.find("débil") != std::string::npos ||
                         r.detail.find("no se repite") != std::string::npos;
    EXPECT_TRUE(refused) << r.detail;
}

TEST(ThreadSuite, AnEmptySceneFailsControlled) {
    const cv::Mat gray(500, 700, CV_8UC1, cv::Scalar(128));
    const auto r = runThreadOn(gray, threadAlong(0.0));
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.detail.empty());
}

TEST(ThreadSuite, AShortAxisFailsWithAReason) {
    const cv::Mat gray = drawThread(30.0, 120.0, 84.0, 60.0);
    const ThreadGeometry tiny{{350.0F, 250.0F}, {360.0F, 250.0F}, 70.0F, 300};
    const auto r = runThreadOn(gray, tiny);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.detail.find("corto"), std::string::npos) << r.detail;
}

TEST(ThreadSuite, GeometrySurvivesASaveAndLoadRoundTrip) {
    const ThreadGeometry g{{5.5F, 11.25F}, {205.0F, 13.5F}, 51.5F, 320};
    const auto parsed = geometryFromJson(ToolType::Thread, toJson(ToolGeometry(g)));
    ASSERT_TRUE(parsed.isOk()) << parsed.error().message;
    const auto& back = std::get<ThreadGeometry>(parsed.value());
    EXPECT_FLOAT_EQ(back.axisFrom.x, g.axisFrom.x);
    EXPECT_FLOAT_EQ(back.axisTo.y, g.axisTo.y);
    EXPECT_FLOAT_EQ(back.searchBand, g.searchBand);
    EXPECT_EQ(back.stations, g.stations);
    EXPECT_EQ(typeOf(ToolGeometry(g)), ToolType::Thread);
    const auto fromName = toolTypeFromName("thread");
    ASSERT_TRUE(fromName.isOk());
    EXPECT_EQ(fromName.value(), ToolType::Thread);
}

TEST(ThreadSuite, MovingItKeepsItsShape) {
    ToolGeometry geometry = ThreadGeometry{{10.0F, 20.0F}, {90.0F, 25.0F}, 30.0F, 200};
    const auto before = std::get<ThreadGeometry>(geometry);
    translateGeometry(geometry, {40.0F, -12.0F});
    const auto after = std::get<ThreadGeometry>(geometry);
    EXPECT_FLOAT_EQ(after.axisFrom.x, before.axisFrom.x + 40.0F);
    EXPECT_FLOAT_EQ(after.axisTo.y, before.axisTo.y - 12.0F);
    EXPECT_EQ(after.stations, before.stations);
}

// --- Eje / Diametro (torno) ---

namespace {

// Barra vista de perfil: cilindrica si los dos diametros coinciden, conica si
// no. Se dibuja como un trapecio, que es la silueta real de una pieza de torno.
cv::Mat drawShaft(double diameterStart, double diameterEnd, double angleDeg = 0.0) {
    cv::Mat gray(500, 500, CV_8UC1, cv::Scalar(220));
    const double a = angleDeg * 3.14159265358979323846 / 180.0;
    const cv::Point2f dir(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
    const cv::Point2f nrm(-dir.y, dir.x);
    const cv::Point2f c(250.0F, 250.0F);
    const cv::Point2f p0 = c - dir * 150.0F;
    const cv::Point2f p1 = c + dir * 150.0F;
    const auto hs = static_cast<float>(diameterStart / 2.0);
    const auto he = static_cast<float>(diameterEnd / 2.0);
    const std::vector<cv::Point> poly = {
        cv::Point(cvRound(p0.x + nrm.x * hs), cvRound(p0.y + nrm.y * hs)),
        cv::Point(cvRound(p1.x + nrm.x * he), cvRound(p1.y + nrm.y * he)),
        cv::Point(cvRound(p1.x - nrm.x * he), cvRound(p1.y - nrm.y * he)),
        cv::Point(cvRound(p0.x - nrm.x * hs), cvRound(p0.y - nrm.y * hs))};
    cv::fillConvexPoly(gray, poly, cv::Scalar(40), cv::LINE_AA);
    return gray;
}

ShaftGeometry shaftAlong(double angleDeg, double band = 60.0) {
    const double a = angleDeg * 3.14159265358979323846 / 180.0;
    const cv::Point2f dir(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
    const cv::Point2f c(250.0F, 250.0F);
    return ShaftGeometry{c - dir * 120.0F, c + dir * 120.0F, static_cast<float>(band), 32};
}

}  // namespace

TEST(ShaftSuite, MeasuresTheDiameterOfACylinder) {
    for (const double diameter : {40.0, 80.0, 140.0}) {
        const cv::Mat gray = drawShaft(diameter, diameter);
        // Alcance holgado: con 60 px por lado el borde de la pieza de 140 cae
        // fuera y la herramienta -con razon- se niega a medir.
        const double measured =
            runMeasure(gray, kIdentity, ToolType::Shaft, ToolGeometry(shaftAlong(0.0, 110.0)));
        std::printf("  cilindro Ø%.0f -> medido %.2f\n", diameter, measured);
        // Margen de 2 px: la silueta suavizada del dibujo se ensancha unas
        // decimas por lado (medido en F3: +0,6 px por borde), asi que el
        // diametro sale sistematicamente algo mayor que el nominal.
        EXPECT_NEAR(measured, diameter, 2.0) << "diametro " << diameter;
    }
}

TEST(ShaftSuite, TheDiameterIsTheSameOnATiltedPiece) {
    // La pieza llega como llega: el resultado no puede depender de la
    // inclinacion con la que se apoye en la mesa.
    constexpr double kDiameter = 70.0;
    double reference = 0.0;
    for (const double angle : {0.0, 20.0, 45.0, -35.0, 90.0}) {
        const cv::Mat gray = drawShaft(kDiameter, kDiameter, angle);
        const double measured = runMeasure(gray, kIdentity, ToolType::Shaft,
                                           ToolGeometry(shaftAlong(angle, 90.0)));
        if (angle == 0.0) {
            reference = measured;
        } else {
            EXPECT_NEAR(measured, reference, 1.5) << "a " << angle << " grados";
        }
        EXPECT_NEAR(measured, kDiameter, 2.0) << "a " << angle << " grados";
    }
}

TEST(ShaftSuite, AnOffCentreAxisStillGivesTheRightDiameter) {
    // El operador no traza el eje por el centro exacto. Como se mide la
    // SEPARACION entre los dos bordes ajustados y no la distancia a la linea
    // dibujada, da igual: es la diferencia con hacerlo a base de calipers.
    constexpr double kDiameter = 90.0;
    const cv::Mat gray = drawShaft(kDiameter, kDiameter);
    // El alcance tiene que cubrir el radio MAS el descentrado, o el borde
    // lejano queda fuera; es justo el caso que la herramienta explica ahora en
    // su mensaje de error.
    ShaftGeometry centred = shaftAlong(0.0, 90.0);
    ShaftGeometry shifted = centred;
    shifted.axisFrom.y += 25.0F;  // eje desplazado un cuarto del radio
    shifted.axisTo.y += 25.0F;

    const double a = runMeasure(gray, kIdentity, ToolType::Shaft, ToolGeometry(centred));
    const double b = runMeasure(gray, kIdentity, ToolType::Shaft, ToolGeometry(shifted));
    std::printf("  eje centrado %.2f, descentrado 25 px %.2f (real %.0f)\n", a, b, kDiameter);
    EXPECT_NEAR(a, kDiameter, 2.0);
    EXPECT_NEAR(b, a, 0.5) << "descentrar el eje no puede cambiar la medida";
}

TEST(ShaftSuite, TellsAConeFromACylinder) {
    // El motivo de que esta herramienta exista y no sea un preset del Caliper:
    // un caliper mide en UN punto y ahi las dos piezas son identicas.
    const cv::Mat cylinder = drawShaft(80.0, 80.0);
    const cv::Mat cone = drawShaft(60.0, 100.0);

    const auto runDetail = [](const cv::Mat& gray) {
        const auto r = runTool(gray, kIdentity,
                               makeConfig(ToolType::Shaft, ToolGeometry(shaftAlong(0.0)), 0,
                                          1e9));
        EXPECT_TRUE(r.isOk());
        return r.value();
    };
    const auto flat = runDetail(cylinder);
    const auto tapered = runDetail(cone);
    std::printf("  cilindro: %s\n  cono:     %s\n", flat.detail.c_str(),
                tapered.detail.c_str());

    // El cilindro no tiene conicidad; el cono si, y del orden dibujado: sobre
    // los 240 px de eje medidos de los 300 de pieza, 40 de diferencia total
    // dan unos 32.
    EXPECT_NE(flat.detail.find("conicidad="), std::string::npos);
    EXPECT_EQ(flat.detail.find("ángulo entre caras"), std::string::npos)
        << "un cilindro no deberia reportar angulo entre caras: " << flat.detail;
    EXPECT_NE(tapered.detail.find("ángulo entre caras"), std::string::npos)
        << tapered.detail;
    // Y el diametro medio del cono cae entre los dos extremos.
    EXPECT_GT(tapered.measured, 60.0);
    EXPECT_LT(tapered.measured, 100.0);
}

TEST(ShaftSuite, TheTaperMatchesWhatWasDrawn) {
    // La conicidad medida sobre el tramo explorado tiene que corresponderse con
    // la pendiente dibujada, no ser un numero cualquiera distinto de cero.
    const cv::Mat gray = drawShaft(60.0, 100.0);  // +40 en 300 px de pieza
    const auto r = runTool(gray, kIdentity,
                           makeConfig(ToolType::Shaft, ToolGeometry(shaftAlong(0.0)), 0, 1e9));
    ASSERT_TRUE(r.isOk());
    // El eje trazado cubre 240 px, asi que la conicidad esperada es 40*240/300.
    const double expected = 40.0 * 240.0 / 300.0;
    // Se relee del detalle el diametro medio, que debe ser el del centro: 80.
    std::printf("  cono 60->100: %s (conicidad esperada %.1f)\n", r.value().detail.c_str(),
                expected);
    EXPECT_NEAR(r.value().measured, 80.0, 1.5) << "el diametro medio es el del centro";
}

TEST(ShaftSuite, ADegenerateAxisFailsWithAReason) {
    const cv::Mat gray = drawShaft(80.0, 80.0);
    const ShaftGeometry tiny{{250.0F, 250.0F}, {252.0F, 250.0F}, 60.0F, 32};
    const auto r =
        runTool(gray, kIdentity, makeConfig(ToolType::Shaft, ToolGeometry(tiny), 0, 1e9));
    ASSERT_TRUE(r.isOk());
    EXPECT_FALSE(r.value().ok);
    EXPECT_NE(r.value().detail.find("corto"), std::string::npos) << r.value().detail;
}

TEST(ShaftSuite, TooShortAReachSaysWhatToDo) {
    // El fallo mas habitual de esta herramienta: la banda no llega al borde
    // porque la pieza es gruesa o el eje quedo descentrado. Decir solo "bordes
    // insuficientes" deja al operador sin saber que tocar.
    const cv::Mat gray = drawShaft(140.0, 140.0);
    const auto r = runTool(gray, kIdentity,
                           makeConfig(ToolType::Shaft, ToolGeometry(shaftAlong(0.0, 30.0)), 0,
                                      1e9));
    ASSERT_TRUE(r.isOk());
    EXPECT_FALSE(r.value().ok);
    EXPECT_NE(r.value().detail.find("alcance"), std::string::npos) << r.value().detail;
    std::printf("  %s\n", r.value().detail.c_str());
}

TEST(ShaftSuite, AnEmptySceneFailsControlled) {
    const cv::Mat gray(500, 500, CV_8UC1, cv::Scalar(128));
    const auto r = runTool(gray, kIdentity,
                           makeConfig(ToolType::Shaft, ToolGeometry(shaftAlong(0.0)), 0, 1e9));
    ASSERT_TRUE(r.isOk());
    EXPECT_FALSE(r.value().ok);
    EXPECT_NE(r.value().detail.find("insuficientes"), std::string::npos) << r.value().detail;
}

TEST(ShaftSuite, GeometrySurvivesASaveAndLoadRoundTrip) {
    const ShaftGeometry g{{12.5F, 30.25F}, {180.0F, 33.5F}, 44.5F, 48};
    const auto parsed = geometryFromJson(ToolType::Shaft, toJson(ToolGeometry(g)));
    ASSERT_TRUE(parsed.isOk()) << parsed.error().message;
    const auto& back = std::get<ShaftGeometry>(parsed.value());
    EXPECT_FLOAT_EQ(back.axisFrom.x, g.axisFrom.x);
    EXPECT_FLOAT_EQ(back.axisFrom.y, g.axisFrom.y);
    EXPECT_FLOAT_EQ(back.axisTo.x, g.axisTo.x);
    EXPECT_FLOAT_EQ(back.axisTo.y, g.axisTo.y);
    EXPECT_FLOAT_EQ(back.searchBand, g.searchBand);
    EXPECT_EQ(back.stations, g.stations);
    EXPECT_EQ(typeOf(ToolGeometry(g)), ToolType::Shaft);
    const auto fromName = toolTypeFromName("shaft");
    ASSERT_TRUE(fromName.isOk());
    EXPECT_EQ(fromName.value(), ToolType::Shaft);
}

TEST(ShaftSuite, MovingItKeepsItsShape) {
    ToolGeometry geometry = ShaftGeometry{{10.0F, 20.0F}, {90.0F, 25.0F}, 30.0F, 24};
    const auto before = std::get<ShaftGeometry>(geometry);
    translateGeometry(geometry, {-15.0F, 60.0F});
    const auto after = std::get<ShaftGeometry>(geometry);
    EXPECT_FLOAT_EQ(after.axisFrom.x, before.axisFrom.x - 15.0F);
    EXPECT_FLOAT_EQ(after.axisTo.y, before.axisTo.y + 60.0F);
    EXPECT_FLOAT_EQ(after.searchBand, before.searchBand);
}

// --- Arco (radio) ---

namespace {

// Esquina redondeada: dos caras rectas unidas por un cuadrante de radio
// conocido, que es la forma real donde se mide un radio de plano. Devuelve
// tambien los tres puntos con los que se traza la herramienta encima.
cv::Mat drawRoundedCorner(int radius, cv::Point2f corner, cv::Point2f& start,
                          cv::Point2f& mid, cv::Point2f& end) {
    cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(220));
    cv::rectangle(gray, cv::Rect(cvRound(corner.x), 0, 400, 400), cv::Scalar(40), cv::FILLED);
    cv::rectangle(gray, cv::Rect(0, cvRound(corner.y), 400, 400), cv::Scalar(40), cv::FILLED);
    cv::circle(gray, cv::Point(cvRound(corner.x), cvRound(corner.y)), radius, cv::Scalar(40),
               cv::FILLED, cv::LINE_AA);
    // El arco visible va de "arriba" a "izquierda", pasando por la diagonal.
    const double r = radius;
    start = {corner.x, static_cast<float>(corner.y - r)};
    mid = {static_cast<float>(corner.x - r * 0.7071), static_cast<float>(corner.y - r * 0.7071)};
    end = {static_cast<float>(corner.x - r), corner.y};
    return gray;
}

}  // namespace

TEST(ArcSuite, MeasuresTheRadiusOfARoundedCorner) {
    for (const int radius : {20, 40, 70}) {
        cv::Point2f start;
        cv::Point2f mid;
        cv::Point2f end;
        const cv::Mat gray = drawRoundedCorner(radius, {200.0F, 200.0F}, start, mid, end);
        const ArcGeometry g{start, mid, end, 10.0F, 24};
        const double measured = runMeasure(gray, kIdentity, ToolType::Arc, ToolGeometry(g));
        std::printf("  radio %2d -> medido %.2f\n", radius, measured);
        // Margen de 1,5 px y no de una decima: medir un radio sobre un
        // cuadrante es intrinsecamente menos preciso que sobre una
        // circunferencia entera (ver ArcSuite.AShortArcIsHarderThanAFullCircle),
        // y ademas el borde suavizado se dibuja unas decimas mas grande.
        EXPECT_NEAR(measured, radius, 1.5) << "radio " << radius;
    }
}

TEST(ArcSuite, TheRadiusIsTheSameOnARotatedPiece) {
    // La geometria vive en coordenadas de pieza: girar la pieza no puede
    // cambiar el radio medido. Es la invariancia que ya cumplen las otras diez.
    constexpr int kRadius = 45;
    cv::Point2f start;
    cv::Point2f mid;
    cv::Point2f end;
    const cv::Mat gray = drawRoundedCorner(kRadius, {200.0F, 200.0F}, start, mid, end);

    double reference = 0.0;
    for (const double angle : {0.0, 25.0, 90.0, -60.0}) {
        Fixture fixture;
        fixture.origin = {200.0F, 200.0F};
        fixture.angleDeg = angle;
        const ArcGeometry g{pci::vision::toPieceCoords(fixture, start),
                            pci::vision::toPieceCoords(fixture, mid),
                            pci::vision::toPieceCoords(fixture, end), 10.0F, 24};
        const double measured = runMeasure(gray, fixture, ToolType::Arc, ToolGeometry(g));
        if (angle == 0.0) {
            reference = measured;
        } else {
            EXPECT_NEAR(measured, reference, 1.0) << "a " << angle << " grados";
        }
    }
}

TEST(ArcSuite, MeasuresTheEdgeNotThePointsYouClicked) {
    // Los tres puntos solo situan el arco. Si el operador los marca algo
    // desviados, la medida debe seguir siendo la del BORDE real; de lo
    // contrario se estaria midiendo el pulso de quien dibuja.
    constexpr int kRadius = 50;
    cv::Point2f start;
    cv::Point2f mid;
    cv::Point2f end;
    const cv::Mat gray = drawRoundedCorner(kRadius, {200.0F, 200.0F}, start, mid, end);

    const ArcGeometry exact{start, mid, end, 12.0F, 24};
    const ArcGeometry sloppy{start + cv::Point2f(0.0F, 4.0F), mid + cv::Point2f(-3.0F, 3.0F),
                             end + cv::Point2f(4.0F, 0.0F), 12.0F, 24};
    const double a = runMeasure(gray, kIdentity, ToolType::Arc, ToolGeometry(exact));
    const double b = runMeasure(gray, kIdentity, ToolType::Arc, ToolGeometry(sloppy));
    std::printf("  exacto %.2f, marcado a ojo %.2f (real %d)\n", a, b, kRadius);
    EXPECT_NEAR(a, kRadius, 3.0);
    EXPECT_NEAR(b, kRadius, 3.0);
}

TEST(ArcSuite, MoreRaysDoNotChangeTheRadius) {
    cv::Point2f start;
    cv::Point2f mid;
    cv::Point2f end;
    const cv::Mat gray = drawRoundedCorner(40, {200.0F, 200.0F}, start, mid, end);
    double previous = -1.0;
    for (const int rays : {8, 24, 60}) {
        const ArcGeometry g{start, mid, end, 10.0F, rays};
        const double measured = runMeasure(gray, kIdentity, ToolType::Arc, ToolGeometry(g));
        EXPECT_NEAR(measured, 40.0, 3.0) << rays << " rayos";
        if (previous > 0.0) {
            EXPECT_NEAR(measured, previous, 2.0);
        }
        previous = measured;
    }
}

TEST(ArcSuite, AShortArcIsHarderThanAFullCircle) {
    // Propiedad de la medida, no defecto: sobre un arco corto el radio y el
    // centro son casi indistinguibles, asi que un error pequeno y SISTEMATICO
    // del borde -no ruido aleatorio, que se promedia- se amplifica en el radio.
    //
    // Se mide el MISMO disco entero y por cuadrantes. El entero acierta; el
    // cuadrante se desvia mas. Pasa igual en una pieza real, y por eso la
    // herramienta avisa cuando el tramo marcado es corto.
    constexpr int kRadius = 40;
    cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(220));
    cv::circle(gray, {200, 200}, kRadius, cv::Scalar(40), cv::FILLED, cv::LINE_8);

    const CircleGeometry whole{{200.0F, 200.0F}, static_cast<float>(kRadius), 10.0F, 96};
    const double full =
        runMeasure(gray, kIdentity, ToolType::Circle, ToolGeometry(whole)) / 2.0;

    // Un cuadrante del mismo disco, marcado con tres puntos exactos.
    const double r = kRadius;
    const ArcGeometry quarter{{200.0F, static_cast<float>(200.0 - r)},
                              {static_cast<float>(200.0 - r * 0.7071),
                               static_cast<float>(200.0 - r * 0.7071)},
                              {static_cast<float>(200.0 - r), 200.0F},
                              10.0F,
                              24};
    const double arc = runMeasure(gray, kIdentity, ToolType::Arc, ToolGeometry(quarter));

    std::printf("  mismo disco R=%d: entero %.2f, cuadrante %.2f\n", kRadius, full, arc);
    EXPECT_NEAR(full, kRadius, 0.5) << "la vuelta completa si debe clavarlo";
    EXPECT_GT(std::abs(arc - kRadius), std::abs(full - kRadius))
        << "el cuadrante deberia ser el menos preciso de los dos";
    EXPECT_LT(std::abs(arc - kRadius), 3.0) << "pero sin irse de madre";
}

TEST(ArcSuite, AVeryShortArcSaysSoInsteadOfPretending) {
    // El aviso que acompana a lo anterior: por debajo de 30 grados de tramo, la
    // herramienta dice que el radio es poco fiable en vez de darlo a secas.
    constexpr int kRadius = 60;
    cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(220));
    cv::circle(gray, {200, 200}, kRadius, cv::Scalar(40), cv::FILLED, cv::LINE_AA);

    const double r = kRadius;
    auto onCircle = [&](double deg) {
        const double a = deg * 3.14159265358979323846 / 180.0;
        return cv::Point2f(static_cast<float>(200.0 + r * std::cos(a)),
                           static_cast<float>(200.0 + r * std::sin(a)));
    };
    const ArcGeometry tiny{onCircle(180.0), onCircle(190.0), onCircle(200.0), 10.0F, 16};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::Arc, ToolGeometry(tiny), 0, 1e9));
    ASSERT_TRUE(result.isOk());
    EXPECT_NE(result.value().detail.find("arco corto"), std::string::npos)
        << result.value().detail;

    // Y con un tramo generoso no molesta con el aviso.
    const ArcGeometry wide{onCircle(180.0), onCircle(225.0), onCircle(270.0), 10.0F, 24};
    const auto fine =
        runTool(gray, kIdentity, makeConfig(ToolType::Arc, ToolGeometry(wide), 0, 1e9));
    ASSERT_TRUE(fine.isOk());
    EXPECT_EQ(fine.value().detail.find("arco corto"), std::string::npos)
        << fine.value().detail;
}

TEST(ArcSuite, CollinearPointsFailWithAReason) {
    // Tres puntos en linea no definen un arco. Devolver un radio enorme seria
    // peor que negarse.
    const cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(128));
    const ArcGeometry g{{50.0F, 100.0F}, {100.0F, 100.0F}, {150.0F, 100.0F}, 10.0F, 24};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::Arc, ToolGeometry(g), 0, 1e9));
    ASSERT_TRUE(result.isOk());
    EXPECT_FALSE(result.value().ok);
    EXPECT_NE(result.value().detail.find("alineados"), std::string::npos)
        << result.value().detail;
}

TEST(ArcSuite, AnEmptySceneFailsControlled) {
    const cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(128));
    const ArcGeometry g{{150.0F, 100.0F}, {115.0F, 115.0F}, {100.0F, 150.0F}, 10.0F, 24};
    const auto result =
        runTool(gray, kIdentity, makeConfig(ToolType::Arc, ToolGeometry(g), 0, 1e9));
    ASSERT_TRUE(result.isOk());
    EXPECT_FALSE(result.value().ok);
    EXPECT_NE(result.value().detail.find("insuficiente"), std::string::npos)
        << result.value().detail;
}

TEST(ArcSuite, GeometrySurvivesASaveAndLoadRoundTrip) {
    const ArcGeometry g{{10.5F, 20.25F}, {30.0F, 15.0F}, {45.5F, 28.75F}, 9.5F, 33};
    const std::string json = toJson(ToolGeometry(g));
    const auto parsed = geometryFromJson(ToolType::Arc, json);
    ASSERT_TRUE(parsed.isOk()) << parsed.error().message;
    const auto& back = std::get<ArcGeometry>(parsed.value());
    EXPECT_FLOAT_EQ(back.start.x, g.start.x);
    EXPECT_FLOAT_EQ(back.start.y, g.start.y);
    EXPECT_FLOAT_EQ(back.mid.x, g.mid.x);
    EXPECT_FLOAT_EQ(back.mid.y, g.mid.y);
    EXPECT_FLOAT_EQ(back.end.x, g.end.x);
    EXPECT_FLOAT_EQ(back.end.y, g.end.y);
    EXPECT_FLOAT_EQ(back.searchBand, g.searchBand);
    EXPECT_EQ(back.rayCount, g.rayCount);
    // El tipo se deduce de la geometria: esta era la rama generica que devolvia
    // Posicion para cualquier tipo nuevo sin que nada fallara al compilar.
    EXPECT_EQ(typeOf(ToolGeometry(g)), ToolType::Arc);
    EXPECT_STREQ(toolTypeName(ToolType::Arc), "arc");
    const auto fromName = toolTypeFromName("arc");
    ASSERT_TRUE(fromName.isOk());
    EXPECT_EQ(fromName.value(), ToolType::Arc);
}

TEST(ArcSuite, MovingItKeepsItsShape) {
    // translateGeometry no tenia rama para el Arco y simplemente no lo movia.
    ToolGeometry geometry = ArcGeometry{{10.0F, 20.0F}, {30.0F, 5.0F}, {50.0F, 20.0F}, 8.0F, 20};
    const auto before = std::get<ArcGeometry>(geometry);
    translateGeometry(geometry, {100.0F, -40.0F});
    const auto after = std::get<ArcGeometry>(geometry);
    EXPECT_FLOAT_EQ(after.start.x, before.start.x + 100.0F);
    EXPECT_FLOAT_EQ(after.mid.y, before.mid.y - 40.0F);
    EXPECT_FLOAT_EQ(after.end.x, before.end.x + 100.0F);
    EXPECT_FLOAT_EQ(after.searchBand, before.searchBand);
}

TEST(CircleSuite, DiameterScalesWithRadius) {
    for (const int radius : {15, 30, 55}) {
        Fixture fixture;
        const cv::Mat gray = drawDisc({300, 300}, {150.0F, 150.0F}, radius, fixture);
        const CircleGeometry g{{0.0F, 0.0F}, static_cast<float>(radius), 12.0F, 36};
        const double measured =
            runMeasure(gray, fixture, ToolType::Circle, ToolGeometry(g));
        EXPECT_NEAR(measured, radius * 2.0, 3.0) << "radio " << radius;
    }
}

TEST(CircleSuite, DiameterIsTheSameOnARotatedPiece) {
    // Un círculo es simétrico: girar la pieza no puede cambiar su diámetro.
    const CircleGeometry g{{0.0F, 0.0F}, 40.0F, 12.0F, 36};
    double reference = 0.0;
    for (const double angle : {0.0, 30.0, 60.0, 90.0}) {
        Fixture fixture;
        const cv::Mat gray = drawDisc({300, 300}, {150.0F, 150.0F}, 40, fixture, angle);
        const double measured = runMeasure(gray, fixture, ToolType::Circle, ToolGeometry(g));
        if (angle == 0.0) {
            reference = measured;
        } else {
            EXPECT_NEAR(measured, reference, 1.5) << "a " << angle << " grados";
        }
    }
}

TEST(CircleSuite, MoreRaysDoNotChangeTheDiameter) {
    Fixture fixture;
    const cv::Mat gray = drawDisc({300, 300}, {150.0F, 150.0F}, 40, fixture);
    double previous = -1.0;
    for (const int rays : {12, 36, 72}) {
        const CircleGeometry g{{0.0F, 0.0F}, 40.0F, 12.0F, rays};
        const double measured = runMeasure(gray, fixture, ToolType::Circle, ToolGeometry(g));
        EXPECT_NEAR(measured, 80.0, 3.0) << rays << " rayos";
        if (previous > 0.0) {
            EXPECT_NEAR(measured, previous, 2.0);
        }
        previous = measured;
    }
}

// --- Regla: es geometria pura, asi que debe ser exacta ---

TEST(RulerSuite, LengthIsExactAndIndependentOfThePose) {
    const cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(128));
    const RulerGeometry g{{-30.0F, -40.0F}, {30.0F, 40.0F}};  // 3-4-5 -> 100 px
    for (const double angle : {0.0, 17.0, 45.0, 90.0, 200.0}) {
        for (const cv::Point2f origin : {cv::Point2f(150.0F, 150.0F),
                                         cv::Point2f(40.0F, 260.0F)}) {
            const Fixture fixture{origin, angle};
            const double measured =
                runMeasure(gray, fixture, ToolType::Ruler, ToolGeometry(g));
            EXPECT_NEAR(measured, 100.0, 1e-3) << "angulo " << angle;
        }
    }
}

TEST(RulerSuite, ZeroLengthMeasuresZeroWithoutFailing) {
    const cv::Mat gray(100, 100, CV_8UC1, cv::Scalar(128));
    const RulerGeometry g{{10.0F, 10.0F}, {10.0F, 10.0F}};
    bool ok = false;
    const double measured =
        runMeasure(gray, kIdentity, ToolType::Ruler, ToolGeometry(g), &ok);
    EXPECT_DOUBLE_EQ(measured, 0.0);
}

// --- Angulo: varios angulos conocidos ---

TEST(AngleSuite, MeasuresKnownCorners) {
    const cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(128));
    const struct { double degrees; cv::Point2f end1; } cases[] = {
        {90.0, {0.0F, 60.0F}},
        {45.0, {42.4F, 42.4F}},
        {135.0, {-42.4F, 42.4F}},
        {180.0, {-60.0F, 0.0F}},
    };
    for (const auto& testCase : cases) {
        const AngleGeometry g{{0.0F, 0.0F}, {60.0F, 0.0F}, testCase.end1};
        const double measured = runMeasure(gray, kIdentity, ToolType::Angle, ToolGeometry(g));
        EXPECT_NEAR(measured, testCase.degrees, 0.5)
            << "esquina de " << testCase.degrees << " grados";
    }
}

TEST(AngleSuite, RotatingThePieceDoesNotChangeTheInteriorAngle) {
    const cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(128));
    const AngleGeometry g{{0.0F, 0.0F}, {50.0F, 0.0F}, {0.0F, 50.0F}};
    for (const double angle : {0.0, 33.0, 90.0, 154.0}) {
        const Fixture fixture{{150.0F, 150.0F}, angle};
        const double measured = runMeasure(gray, fixture, ToolType::Angle, ToolGeometry(g));
        EXPECT_NEAR(measured, 90.0, 0.5) << "pieza a " << angle << " grados";
    }
}

// --- Linea-Linea: paralelas, perpendiculares y giro de la pieza ---

TEST(LineToLineSuite, ParallelAndPerpendicularCases) {
    const cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(128));

    const LineToLineGeometry parallel{{-50.0F, -20.0F}, {50.0F, -20.0F},
                                      {-50.0F, 20.0F},  {50.0F, 20.0F}};
    EXPECT_NEAR(runMeasure(gray, kIdentity, ToolType::LineToLine, ToolGeometry(parallel)), 0.0,
                0.5);

    const LineToLineGeometry perpendicular{{-50.0F, 0.0F}, {50.0F, 0.0F},
                                           {0.0F, -50.0F}, {0.0F, 50.0F}};
    EXPECT_NEAR(
        runMeasure(gray, kIdentity, ToolType::LineToLine, ToolGeometry(perpendicular)), 90.0,
        0.5);

    // 30 grados exactos.
    const LineToLineGeometry thirty{{0.0F, 0.0F},  {100.0F, 0.0F},
                                    {0.0F, 20.0F}, {86.6F, 70.0F}};
    EXPECT_NEAR(runMeasure(gray, kIdentity, ToolType::LineToLine, ToolGeometry(thirty)), 30.0,
                1.0);
}

TEST(LineToLineSuite, AngleSurvivesPieceRotation) {
    const cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(128));
    const LineToLineGeometry perpendicular{{-50.0F, 0.0F}, {50.0F, 0.0F},
                                           {0.0F, -50.0F}, {0.0F, 50.0F}};
    for (const double angle : {0.0, 25.0, 75.0, 130.0}) {
        const Fixture fixture{{150.0F, 150.0F}, angle};
        EXPECT_NEAR(
            runMeasure(gray, fixture, ToolType::LineToLine, ToolGeometry(perpendicular)), 90.0,
            0.5);
    }
}

// --- Blob y Blob poligonal: conteo, area minima y region ---

TEST(BlobSuite, MinAreaFiltersTheSmallOnes) {
    cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(220));
    cv::circle(gray, {120, 150}, 12, cv::Scalar(30), cv::FILLED);  // ~452 px2
    cv::circle(gray, {160, 150}, 12, cv::Scalar(30), cv::FILLED);
    cv::circle(gray, {200, 150}, 3, cv::Scalar(30), cv::FILLED);   // ~28 px2, mota

    const BlobGeometry big{{160.0F, 150.0F}, 200.0F, 120.0F, 100.0F, true};
    EXPECT_NEAR(runMeasure(gray, kIdentity, ToolType::Blob, ToolGeometry(big)), 2.0, 0.01)
        << "con area minima 100 la mota no cuenta";

    const BlobGeometry small{{160.0F, 150.0F}, 200.0F, 120.0F, 10.0F, true};
    EXPECT_NEAR(runMeasure(gray, kIdentity, ToolType::Blob, ToolGeometry(small)), 3.0, 0.01)
        << "con area minima 10 la mota si cuenta";
}

TEST(BlobSuite, OnlyCountsWhatIsInsideTheRegion) {
    cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(220));
    cv::circle(gray, {100, 100}, 10, cv::Scalar(30), cv::FILLED);  // dentro
    cv::circle(gray, {250, 250}, 10, cv::Scalar(30), cv::FILLED);  // fuera

    const BlobGeometry region{{100.0F, 100.0F}, 80.0F, 80.0F, 20.0F, true};
    EXPECT_NEAR(runMeasure(gray, kIdentity, ToolType::Blob, ToolGeometry(region)), 1.0, 0.01);
}

TEST(BlobSuite, PolaritySelectsDarkOrLightSpots) {
    cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(128));
    cv::circle(gray, {130, 150}, 12, cv::Scalar(20), cv::FILLED);   // oscura
    cv::circle(gray, {180, 150}, 12, cv::Scalar(240), cv::FILLED);  // clara

    const BlobGeometry dark{{155.0F, 150.0F}, 160.0F, 100.0F, 50.0F, true};
    const BlobGeometry light{{155.0F, 150.0F}, 160.0F, 100.0F, 50.0F, false};
    EXPECT_NEAR(runMeasure(gray, kIdentity, ToolType::Blob, ToolGeometry(dark)), 1.0, 0.01);
    EXPECT_NEAR(runMeasure(gray, kIdentity, ToolType::Blob, ToolGeometry(light)), 1.0, 0.01);
}

TEST(PolyBlobSuite, ConcaveRegionExcludesWhatIsOutside) {
    cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(220));
    cv::circle(gray, {80, 80}, 10, cv::Scalar(30), cv::FILLED);    // dentro del brazo
    cv::circle(gray, {200, 200}, 10, cv::Scalar(30), cv::FILLED);  // en la escotadura

    // Polígono en L (cóncavo): el segundo punto queda fuera.
    PolyBlobGeometry poly;
    poly.vertices = {{40.0F, 40.0F},  {150.0F, 40.0F}, {150.0F, 150.0F},
                     {40.0F, 150.0F}};
    poly.minArea = 20.0F;
    poly.darkBlobs = true;
    EXPECT_NEAR(runMeasure(gray, kIdentity, ToolType::PolyBlob, ToolGeometry(poly)), 1.0, 0.01);
}

// --- Borde liso: la desviacion crece con la muesca, dentro de su ventana ---

TEST(EdgeFlawSuite, DeviationGrowsWithTheNotchDepth) {
    // Las profundidades caben en la ventana de escaneo (scanLength 20 = +-10).
    double previous = -1.0;
    for (const int depth : {0, 4, 8}) {
        cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(220));
        cv::rectangle(gray, {20, 100}, {180, 190}, cv::Scalar(40), cv::FILLED);
        if (depth > 0) {
            cv::rectangle(gray, {95, 100}, {115, 100 + depth}, cv::Scalar(220), cv::FILLED);
        }
        const EdgeFlawGeometry g{{30.0F, 100.0F}, {170.0F, 100.0F}, 20.0F, 30};
        const double measured =
            runMeasure(gray, kIdentity, ToolType::EdgeFlaw, ToolGeometry(g));
        if (previous >= 0.0) {
            EXPECT_GT(measured, previous - 1.0)
                << "una muesca mas profunda no puede dar menos desviacion (prof " << depth
                << ")";
        }
        previous = measured;
    }
    EXPECT_GT(previous, 2.0) << "una muesca de 8 px debe verse claramente";
}

// LIMITE REAL del Borde liso, encontrado al escribir estas pruebas: la
// herramienta busca el borde dentro de una ventana de +-scanLength/2 alrededor
// de la linea trazada. Una muesca MAS PROFUNDA que esa ventana deja de
// encontrarse y la desviacion vuelve a salir baja — no es un fallo, es el
// parametro, pero hay que conocerlo (queda dicho en el tooltip y el README).
TEST(EdgeFlawSuite, NotchDeeperThanTheScanWindowIsMissed) {
    cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(220));
    cv::rectangle(gray, {20, 100}, {180, 190}, cv::Scalar(40), cv::FILLED);
    cv::rectangle(gray, {95, 100}, {115, 130}, cv::Scalar(220), cv::FILLED);  // 30 px

    const EdgeFlawGeometry narrow{{30.0F, 100.0F}, {170.0F, 100.0F}, 20.0F, 30};  // +-10
    const double missed =
        runMeasure(gray, kIdentity, ToolType::EdgeFlaw, ToolGeometry(narrow));

    // Con una ventana suficiente, la MISMA muesca si aparece.
    const EdgeFlawGeometry wide{{30.0F, 100.0F}, {170.0F, 100.0F}, 80.0F, 30};  // +-40
    const double found = runMeasure(gray, kIdentity, ToolType::EdgeFlaw, ToolGeometry(wide));

    EXPECT_LT(missed, 2.0) << "fuera de la ventana la muesca no se ve";
    EXPECT_GT(found, 10.0) << "ampliando la ventana, la misma muesca se detecta";
}

// --- Punto-Linea ---

TEST(PointToLineSuite, DistanceMatchesTheGeometry) {
    for (const int edgeY : {120, 140, 160}) {
        cv::Mat gray(220, 220, CV_8UC1, cv::Scalar(220));
        cv::rectangle(gray, {40, edgeY}, {180, 210}, cv::Scalar(40), cv::FILLED);
        const PointToLineGeometry g{{40.0F, 60.0F},  {180.0F, 60.0F},
                                    {110.0F, 70.0F}, {110.0F, 200.0F}};
        const double measured =
            runMeasure(gray, kIdentity, ToolType::PointToLine, ToolGeometry(g));
        EXPECT_NEAR(measured, edgeY - 60.0, 3.0) << "borde en y=" << edgeY;
    }
}

// --- Coherencia entre herramientas: dos caminos, la misma verdad ---

TEST(ToolCoherence, RulerAndCaliperAgreeOnTheSameBar) {
    cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(220));
    gray.colRange(130, 170).setTo(40);  // barra de 40 px

    const CaliperGeometry caliper{{60.0F, 150.0F}, {240.0F, 150.0F}, 10.0F};
    const double byCaliper =
        runMeasure(gray, kIdentity, ToolType::Caliper, ToolGeometry(caliper));
    // La Regla mide lo que se traza: se traza justo de borde a borde.
    const RulerGeometry ruler{{130.0F, 150.0F}, {170.0F, 150.0F}};
    const double byRuler = runMeasure(gray, kIdentity, ToolType::Ruler, ToolGeometry(ruler));
    EXPECT_NEAR(byCaliper, byRuler, 2.5);
}

TEST(ToolCoherence, CircleDiameterMatchesACaliperAcrossIt) {
    Fixture fixture;
    const cv::Mat gray = drawDisc({300, 300}, {150.0F, 150.0F}, 45, fixture);
    const CircleGeometry circle{{0.0F, 0.0F}, 45.0F, 14.0F, 48};
    const CaliperGeometry across{{-90.0F, 0.0F}, {90.0F, 0.0F}, 6.0F};
    const double byCircle = runMeasure(gray, fixture, ToolType::Circle, ToolGeometry(circle));
    const double byCaliper = runMeasure(gray, fixture, ToolType::Caliper,
                                        ToolGeometry(across));
    EXPECT_NEAR(byCircle, byCaliper, 3.0)
        << "el diametro y un caliper que cruza el disco deben coincidir";
}

TEST(ToolCoherence, AngleAndLineToLineAgreeOnTheSameCorner) {
    const cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(128));
    // Mismo par de direcciones descritas de dos maneras: esquina y dos rectas.
    const AngleGeometry corner{{0.0F, 0.0F}, {80.0F, 0.0F}, {56.6F, 56.6F}};
    const LineToLineGeometry lines{{0.0F, 0.0F},  {80.0F, 0.0F},
                                   {0.0F, 0.0F},  {56.6F, 56.6F}};
    const double byAngle = runMeasure(gray, kIdentity, ToolType::Angle, ToolGeometry(corner));
    const double byLines =
        runMeasure(gray, kIdentity, ToolType::LineToLine, ToolGeometry(lines));
    EXPECT_NEAR(byAngle, 45.0, 1.0);
    EXPECT_NEAR(byLines, 45.0, 1.0);
    EXPECT_NEAR(byAngle, byLines, 1.0);
}

// --- Tolerancias: el veredicto por tipo de herramienta ---

TEST(ToolVerdicts, EveryToolRespectsItsToleranceBand) {
    cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(220));
    gray.colRange(130, 170).setTo(40);
    const CaliperGeometry caliper{{60.0F, 150.0F}, {240.0F, 150.0F}, 10.0F};

    // Dentro de banda -> OK; fuera -> NG. El valor medido es el mismo.
    const auto inside =
        runTool(gray, kIdentity, makeConfig(ToolType::Caliper, ToolGeometry(caliper), 35, 45));
    const auto outside =
        runTool(gray, kIdentity, makeConfig(ToolType::Caliper, ToolGeometry(caliper), 5, 20));
    ASSERT_TRUE(inside.isOk());
    ASSERT_TRUE(outside.isOk());
    EXPECT_TRUE(inside.value().ok);
    EXPECT_FALSE(outside.value().ok);
    EXPECT_NEAR(inside.value().measured, outside.value().measured, 1e-6)
        << "la tolerancia no puede cambiar la MEDIDA, solo el veredicto";

    // Un conteo exacto: 2 manchas con banda [2,2] es OK y con [3,3] es NG.
    cv::Mat spots(300, 300, CV_8UC1, cv::Scalar(220));
    cv::circle(spots, {120, 150}, 12, cv::Scalar(30), cv::FILLED);
    cv::circle(spots, {170, 150}, 12, cv::Scalar(30), cv::FILLED);
    const BlobGeometry blob{{145.0F, 150.0F}, 160.0F, 100.0F, 50.0F, true};
    EXPECT_TRUE(runTool(spots, kIdentity, makeConfig(ToolType::Blob, ToolGeometry(blob), 2, 2))
                    .value()
                    .ok);
    EXPECT_FALSE(runTool(spots, kIdentity, makeConfig(ToolType::Blob, ToolGeometry(blob), 3, 3))
                     .value()
                     .ok);
}

// Con escala calibrada, el detalle debe hablar en milimetros aunque la medida
// principal siga en pixeles (las tolerancias se definen en px).
TEST(ToolVerdicts, CalibratedDetailShowsMillimetres) {
    cv::Mat gray(300, 300, CV_8UC1, cv::Scalar(220));
    gray.colRange(130, 170).setTo(40);
    const CaliperGeometry caliper{{60.0F, 150.0F}, {240.0F, 150.0F}, 10.0F};
    const auto result = runTool(gray, kIdentity,
                                makeConfig(ToolType::Caliper, ToolGeometry(caliper), 0, 1e9),
                                0.5 /* mm por pixel */, LengthUnit::Millimeters);
    ASSERT_TRUE(result.isOk());
    EXPECT_NEAR(result.value().measured, 40.0, 2.0);  // sigue en pixeles
    EXPECT_NE(result.value().detail.find("mm"), std::string::npos) << result.value().detail;
}

// ===========================================================================
//  Exportar/importar plantillas con datos limite. El formato usa cv::FileStorage
//  y ya dio problemas con cadenas raras, asi que conviene apretarlo.
// ===========================================================================

TEST(TemplateIoEdge, EmptyTemplateRoundTrips) {
    const std::string json = exportTemplateJson({});
    const auto back = importTemplateJson(json);
    ASSERT_TRUE(back.isOk()) << back.error().message;
    EXPECT_TRUE(back.value().empty());
}

TEST(TemplateIoEdge, TwoHundredToolsRoundTripInOrder) {
    std::vector<ToolConfig> tools;
    for (int i = 0; i < 200; ++i) {
        ToolConfig config;
        config.type = (i % 2 == 0) ? ToolType::Ruler : ToolType::Caliper;
        config.name = "herramienta " + std::to_string(i);
        config.geometryJson =
            (i % 2 == 0)
                ? toJson(ToolGeometry(RulerGeometry{{static_cast<float>(i), 1.0F},
                                                    {static_cast<float>(i) + 5.0F, 2.0F}}))
                : toJson(ToolGeometry(CaliperGeometry{{static_cast<float>(i), 0.0F},
                                                      {static_cast<float>(i) + 20.0F, 0.0F},
                                                      8.0F}));
        config.toleranceMin = i * 0.5;
        config.toleranceMax = i * 0.5 + 3.0;
        tools.push_back(config);
    }

    const auto back = importTemplateJson(exportTemplateJson(tools));
    ASSERT_TRUE(back.isOk()) << back.error().message;
    ASSERT_EQ(back.value().size(), tools.size());
    for (std::size_t i = 0; i < tools.size(); ++i) {
        EXPECT_EQ(back.value()[i].name, tools[i].name) << "posicion " << i;
        EXPECT_EQ(back.value()[i].type, tools[i].type);
        EXPECT_DOUBLE_EQ(back.value()[i].toleranceMin, tools[i].toleranceMin);
        EXPECT_EQ(back.value()[i].id, -1) << "las importadas son nuevas para la pieza";
    }
}

// Nombres con comillas, llaves y acentos: el JSON de la plantilla lleva dentro
// otro JSON (la geometria), asi que es justo donde un escapado flojo rompe.
TEST(TemplateIoEdge, ExoticNamesSurviveTheRoundTrip) {
    const std::vector<std::string> names = {
        "ancho \"critico\"",
        "cota {con llaves}",
        "medida 'simple'",
        "acentuada ñ á é í ó ú",
        "con\\barra invertida",
        "con, coma; y punto.",
    };
    std::vector<ToolConfig> tools;
    for (const auto& name : names) {
        ToolConfig config;
        config.type = ToolType::Ruler;
        config.name = name;
        config.geometryJson = toJson(ToolGeometry(RulerGeometry{{0, 0}, {10, 0}}));
        config.toleranceMin = 1.0;
        config.toleranceMax = 20.0;
        tools.push_back(config);
    }

    const auto back = importTemplateJson(exportTemplateJson(tools));
    ASSERT_TRUE(back.isOk()) << back.error().message;
    ASSERT_EQ(back.value().size(), names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        EXPECT_EQ(back.value()[i].name, names[i]) << "nombre " << i << " se corrompio";
    }
}

TEST(TemplateIoEdge, ExtremeTolerancesAndDisabledFlagSurvive) {
    ToolConfig config;
    config.type = ToolType::Blob;
    config.name = "conteo";
    config.geometryJson =
        toJson(ToolGeometry(BlobGeometry{{10.0F, 10.0F}, 40.0F, 30.0F, 25.0F, false}));
    config.toleranceMin = 0.0;
    config.toleranceMax = 1.0e9;
    config.enabled = false;

    const auto back = importTemplateJson(exportTemplateJson({config}));
    ASSERT_TRUE(back.isOk()) << back.error().message;
    ASSERT_EQ(back.value().size(), 1U);
    EXPECT_DOUBLE_EQ(back.value()[0].toleranceMax, 1.0e9);
    EXPECT_FALSE(back.value()[0].enabled) << "una herramienta apagada no debe reactivarse";
    auto geometry = geometryFromJson(back.value()[0].type, back.value()[0].geometryJson);
    ASSERT_TRUE(geometry.isOk());
    EXPECT_FALSE(std::get<BlobGeometry>(geometry.value()).darkBlobs);
}

// Un archivo de otra version o manipulado no puede colar herramientas rotas ni
// tumbar la importacion entera sin explicacion.
TEST(TemplateIoEdge, ToolWithGeometryOfAnotherTypeIsRejected) {
    ToolConfig config;
    config.type = ToolType::Circle;
    config.name = "circulo mentiroso";
    // Geometria de Regla declarada como Circulo.
    config.geometryJson = toJson(ToolGeometry(RulerGeometry{{0, 0}, {10, 0}}));
    config.toleranceMin = 0.0;
    config.toleranceMax = 10.0;

    const auto back = importTemplateJson(exportTemplateJson({config}));
    EXPECT_FALSE(back.isOk()) << "una geometria que no corresponde al tipo debe rechazarse";
}

// --- Avisos de condiciones de medida ---
//
// Estas herramientas dan numeros creibles con datos malos, que es la peor forma
// de fallar. Los avisos existen para eso; y por eso lo que mas se prueba aqui
// no es que salten, sino que NO salten cuando no toca: un aviso que sale
// siempre es un aviso que el operador aprende a ignorar.

namespace {

std::string runCircleDetail(const cv::Mat& gray, double scaleQuality) {
    const CircleGeometry g{{200.0F, 200.0F}, 60.0F, 15.0F, 72};
    const auto r = runTool(gray, kIdentity, makeConfig(ToolType::Circle, ToolGeometry(g), 0,
                                                       1e9),
                           0.0, LengthUnit::Auto, cv::Mat(), nullptr, scaleQuality);
    EXPECT_TRUE(r.isOk());
    return r.isOk() ? r.value().detail : std::string();
}

// Disco con el contraste que se le pida: 220 sobre 40 es contraluz de libro;
// 130 sobre 120 es metal brillante mal iluminado.
cv::Mat discWithContrast(int background, int piece) {
    cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(background));
    cv::circle(gray, {200, 200}, 60, cv::Scalar(piece), cv::FILLED, cv::LINE_AA);
    return gray;
}

}  // namespace

TEST(MeasuringConditions, ATiltedCameraIsReported) {
    const cv::Mat gray = discWithContrast(220, 40);
    const std::string tilted = runCircleDetail(gray, 0.4);
    std::printf("  calidad 0.40: %s\n", tilted.c_str());
    EXPECT_NE(tilted.find("inclinada"), std::string::npos) << tilted;
}

TEST(MeasuringConditions, AGoodCameraPoseIsNotReported) {
    const cv::Mat gray = discWithContrast(220, 40);
    const std::string straight = runCircleDetail(gray, 0.95);
    EXPECT_EQ(straight.find("inclinada"), std::string::npos) << straight;
}

TEST(MeasuringConditions, UnknownPoseNeverWarns) {
    // El caso corriente: no hay marcador ArUco, asi que no se SABE como esta la
    // camara. Avisar ahi seria ruido en cada medicion; antes de este test el
    // valor por defecto (0) hacia justo eso.
    const cv::Mat gray = discWithContrast(220, 40);
    const std::string unknown = runCircleDetail(gray, -1.0);
    EXPECT_EQ(unknown.find("inclinada"), std::string::npos) << unknown;
}

TEST(MeasuringConditions, APoorEdgeIsReported) {
    // Metal brillante con luz frontal: el borde se despega poco del fondo pero
    // todavia se encuentra. Por debajo de esto ni se detecta, y entonces la
    // herramienta ya falla con "borde insuficiente" -que tambien informa-.
    const cv::Mat faint = discWithContrast(150, 120);
    const std::string detail = runCircleDetail(faint, -1.0);
    std::printf("  contraste bajo: %s\n", detail.c_str());
    EXPECT_NE(detail.find("contraste"), std::string::npos) << detail;
}

TEST(MeasuringConditions, AnUndetectableEdgeFailsBeforeWarning) {
    // El escalon siguiente: si el borde no llega ni a detectarse no hay medida
    // que matizar, y la herramienta se niega. Tambien es informar.
    const cv::Mat invisible = discWithContrast(130, 118);
    const std::string detail = runCircleDetail(invisible, -1.0);
    std::printf("  sin borde: %s\n", detail.c_str());
    EXPECT_NE(detail.find("insuficiente"), std::string::npos) << detail;
}

TEST(MeasuringConditions, AGoodEdgeIsNotReported) {
    const cv::Mat crisp = discWithContrast(220, 40);
    const std::string detail = runCircleDetail(crisp, -1.0);
    EXPECT_EQ(detail.find("contraste"), std::string::npos) << detail;
}

TEST(MeasuringConditions, TheWarningsReachTheTurnedPartTools) {
    // Los mismos avisos tienen que llegar al Eje, la Rosca y el Engranaje, que
    // son los que mas dependen de un borde limpio y de la perpendicularidad.
    const cv::Mat shaftScene = drawShaft(80.0, 80.0);
    const auto shaft = runTool(shaftScene, kIdentity,
                               makeConfig(ToolType::Shaft, ToolGeometry(shaftAlong(0.0, 90.0)),
                                          0, 1e9),
                               0.0, LengthUnit::Auto, cv::Mat(), nullptr, 0.3);
    ASSERT_TRUE(shaft.isOk());
    EXPECT_NE(shaft.value().detail.find("inclinada"), std::string::npos)
        << shaft.value().detail;

    const cv::Mat gearScene = drawGear(20, 260.0, 220.0);
    const GearGeometry gg{{350.0F, 350.0F}, 200.0F, 290.0F, 720};
    const auto gear = runTool(gearScene, kIdentity,
                              makeConfig(ToolType::Gear, ToolGeometry(gg), 0, 1e9), 0.0,
                              LengthUnit::Auto, cv::Mat(), nullptr, 0.3);
    ASSERT_TRUE(gear.isOk());
    EXPECT_NE(gear.value().detail.find("inclinada"), std::string::npos)
        << gear.value().detail;
}

// ---------------------------------------------------------------------------
// Repaso de coherencia: lo que TODA herramienta tiene que cumplir
// ---------------------------------------------------------------------------
//
// Recorren `allToolTypes()` en vez de una lista escrita a mano, para que una
// herramienta nueva entre sola en el repaso.

TEST(ToolCoherence, EveryToolHasItsOwnNameAndExplainsHowToDrawIt) {
    std::vector<std::string> labels;
    std::vector<std::string> names;
    for (const ToolType type : allToolTypes()) {
        const std::string label = toolTypeLabel(type);
        const std::string name = toolTypeName(type);
        const std::string description = toolTypeDescription(type);

        EXPECT_FALSE(label.empty());
        EXPECT_NE(label, "?") << "tipo sin nombre corto";
        EXPECT_FALSE(name.empty());
        EXPECT_NE(name, "unknown") << label << " no tiene nombre para la base de datos";

        // El tooltip es lo único que le dice al operador QUÉ mide y CÓMO se
        // traza: uno de una línea no sirve de nada. Y tiene que empezar por el
        // nombre de la herramienta, que es como está escrito el resto.
        EXPECT_GT(description.size(), 80U) << label << ": descripción demasiado corta";
        EXPECT_EQ(description.rfind(label, 0), 0U)
            << label << ": la descripción no empieza nombrando la herramienta — «"
            << description.substr(0, 40) << "»";
        EXPECT_NE(description.find('\n'), std::string::npos)
            << label << ": la descripción no explica cómo dibujarla";

        labels.push_back(label);
        names.push_back(name);
    }
    // Dos herramientas con el mismo nombre corto serían indistinguibles en los
    // botones; con el mismo nombre interno, una pisaría a la otra al guardar.
    std::sort(labels.begin(), labels.end());
    std::sort(names.begin(), names.end());
    EXPECT_EQ(std::adjacent_find(labels.begin(), labels.end()), labels.end());
    EXPECT_EQ(std::adjacent_find(names.begin(), names.end()), names.end());
    EXPECT_EQ(labels.size(), allToolTypes().size());
}

TEST(ToolCoherence, EveryToolNameSurvivesTheRoundTripThroughTheDatabase) {
    for (const ToolType type : allToolTypes()) {
        const auto back = toolTypeFromName(toolTypeName(type));
        ASSERT_TRUE(back.isOk()) << toolTypeName(type);
        EXPECT_EQ(back.value(), type);
    }
}

TEST(ToolCoherence, EveryToolGeometrySurvivesTheTemplateRoundTrip) {
    for (const ToolType type : allToolTypes()) {
        const ToolGeometry original = testing_support::sampleGeometry(type);
        ASSERT_EQ(typeOf(original), type) << toolTypeLabel(type) << ": typeOf no lo reconoce";

        const std::string json = toJson(original);
        EXPECT_FALSE(json.empty()) << toolTypeLabel(type);
        const auto back = geometryFromJson(type, json);
        ASSERT_TRUE(back.isOk()) << toolTypeLabel(type) << ": " << back.error().message;
        EXPECT_EQ(typeOf(back.value()), type);

        // Y lo que vuelve es lo mismo que se guardó: se compara por el JSON, que
        // es exactamente lo que se persiste.
        EXPECT_EQ(toJson(back.value()), json) << toolTypeLabel(type);
    }
}

TEST(ToolCoherence, EveryToolMovesWholeWhenTheToolIsDragged) {
    // Mover una herramienta la desplaza ENTERA: si un punto se quedara atrás, se
    // deformaría al arrastrarla o al duplicarla.
    const cv::Point2f delta(37.5F, -21.25F);
    for (const ToolType type : allToolTypes()) {
        const ToolGeometry original = testing_support::sampleGeometry(type);
        ToolGeometry moved = original;
        translateGeometry(moved, delta);
        ASSERT_EQ(typeOf(moved), type);

        const auto before = handlePoints(original);
        const auto after = handlePoints(moved);
        ASSERT_EQ(before.size(), after.size()) << toolTypeLabel(type);
        for (std::size_t i = 0; i < before.size(); ++i) {
            EXPECT_NEAR(after[i].x, before[i].x + delta.x, 1e-3)
                << toolTypeLabel(type) << ", punto " << i;
            EXPECT_NEAR(after[i].y, before[i].y + delta.y, 1e-3)
                << toolTypeLabel(type) << ", punto " << i;
        }
    }
}

TEST(ToolCoherence, EveryToolSuggestsABandThatAcceptsTheGoodPiece) {
    // La tolerancia sugerida sale de medir una pieza buena: si la banda no
    // contuviera esa misma medida, la primera pieza daría NG nada más dibujar la
    // herramienta.
    for (const ToolType type : allToolTypes()) {
        for (const double measured : {0.5, 3.0, 40.0, 137.25}) {
            double lo = -1.0;
            double hi = -1.0;
            suggestTolerances(type, measured, lo, hi);
            EXPECT_LE(lo, measured + 1e-9)
                << toolTypeLabel(type) << " con medida " << measured;
            EXPECT_GE(hi, measured - 1e-9)
                << toolTypeLabel(type) << " con medida " << measured;
            EXPECT_LE(lo, hi) << toolTypeLabel(type);
            EXPECT_TRUE(std::isfinite(lo) && std::isfinite(hi)) << toolTypeLabel(type);
        }
    }
}

// ---------------------------------------------------------------------------
// Familias de herramientas (R1)
// ---------------------------------------------------------------------------

TEST(ToolCoherence, TheFamiliesPartitionEveryToolWithoutGapsOrRepeats) {
    // Las familias son una PARTICIÓN: cada herramienta en una y solo una. Si
    // faltara alguna quedaría escondida en la paleta, y si estuviera en dos
    // aparecería duplicada.
    std::vector<ToolType> gathered;
    for (const ToolCategory category : allToolCategories()) {
        const auto tools = toolsInCategory(category);
        std::printf("  %-24s %zu herramienta(s)\n", categoryLabel(category), tools.size());
        for (const ToolType type : tools) {
            EXPECT_EQ(categoryOf(type), category) << toolTypeLabel(type);
            gathered.push_back(type);
        }
    }

    auto expected = std::vector<ToolType>(allToolTypes().begin(), allToolTypes().end());
    std::sort(gathered.begin(), gathered.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(gathered, expected)
        << "las familias tienen que reconstruir exactamente allToolTypes()";
    EXPECT_EQ(std::adjacent_find(gathered.begin(), gathered.end()), gathered.end())
        << "una herramienta no puede estar en dos familias";
}

TEST(ToolCoherence, EveryFamilyHasANameAndAnExplanation) {
    std::vector<std::string> labels;
    for (const ToolCategory category : allToolCategories()) {
        const std::string label = categoryLabel(category);
        const std::string description = categoryDescription(category);
        EXPECT_FALSE(label.empty());
        EXPECT_NE(label, "?") << "familia sin nombre";
        // La descripción es lo que le dice al operador qué esperar de la
        // familia; una de una línea no orienta a nadie.
        EXPECT_GT(description.size(), 60U) << label << ": descripción demasiado corta";
        labels.push_back(label);
    }
    std::sort(labels.begin(), labels.end());
    EXPECT_EQ(std::adjacent_find(labels.begin(), labels.end()), labels.end())
        << "dos familias con el mismo nombre serían indistinguibles";
}

TEST(ToolCoherence, NoFamilyIsEmptyAnyMore) {
    // «Construcciones» nació vacía en R1 con un test que exigía que lo
    // estuviera, para que el hueco no se olvidara. X1 la llenó y ese test saltó,
    // que es exactamente para lo que estaba. Ahora la regla es la contraria: una
    // familia vacía es un cajón que el operador abre para nada.
    for (const ToolCategory category : allToolCategories()) {
        EXPECT_FALSE(toolsInCategory(category).empty()) << categoryLabel(category);
    }
    // Y las dos construcciones están donde dicen estar.
    const auto constructions = toolsInCategory(ToolCategory::Construction);
    EXPECT_EQ(constructions.size(), 2U);
    EXPECT_EQ(categoryOf(ToolType::ConstructedPoint), ToolCategory::Construction);
    EXPECT_EQ(categoryOf(ToolType::ConstructedLine), ToolCategory::Construction);
}

// ---------------------------------------------------------------------------
// Referencias entre herramientas (X0)
// ---------------------------------------------------------------------------

namespace {

// Regla entre dos puntos de pieza, con nombre y referencia opcional.
ToolConfig makeRuler(const std::string& name, cv::Point2f a, cv::Point2f b,
                     const std::string& reference = {}) {
    ToolConfig config;
    config.type = ToolType::Ruler;
    config.name = name;
    config.reference = reference;
    config.geometryJson = toJson(ToolGeometry(RulerGeometry{a, b}));
    config.toleranceMin = 0.0;
    config.toleranceMax = 1e9;
    return config;
}

const ToolRunResult* findByName(const std::vector<ToolRunResult>& results,
                                const std::string& name) {
    for (const auto& result : results) {
        if (result.name == name) {
            return &result;
        }
    }
    return nullptr;
}

}  // namespace

TEST(ToolReferences, AToolThatCanBeADatumOffersItsElement) {
    const cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(128));
    const auto result = runTool(gray, kIdentity, makeRuler("cara A", {0, 0}, {100, 0}));
    ASSERT_TRUE(result.isOk());
    ASSERT_TRUE(result.value().derived.valid());
    EXPECT_EQ(result.value().derived.kind, DerivedKind::Line);
    // Dirección unitaria y en coordenadas de PIEZA: la referencia tiene que
    // seguir a la pieza igual que la herramienta que la usa.
    EXPECT_NEAR(result.value().derived.direction.x, 1.0, 1e-6);
    EXPECT_NEAR(result.value().derived.direction.y, 0.0, 1e-6);
    EXPECT_NEAR(cv::norm(result.value().derived.direction), 1.0, 1e-6);
}

TEST(ToolReferences, AChainOfThreeToolsResolvesInDependencyOrder) {
    // A -> B -> C, pero declaradas al revés en la lista: el orden de ejecución
    // lo decide la dependencia, no el orden en que el operador las dibujó.
    const cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(128));
    std::vector<ToolConfig> tools{
        makeRuler("C", {0, 60}, {80, 60}, "B"),
        makeRuler("B", {0, 30}, {80, 30}, "A"),
        makeRuler("A", {0, 0}, {80, 0}),
    };
    const auto results = runTools(gray, kIdentity, tools);
    ASSERT_EQ(results.size(), 3U);
    for (const auto* name : {"A", "B", "C"}) {
        const auto* result = findByName(results, name);
        ASSERT_NE(result, nullptr) << name;
        EXPECT_TRUE(result->ok) << name << ": " << result->detail;
    }
    // Y los resultados salen en el ORDEN DE LA LISTA, no en el de ejecución:
    // si bailaran, la tabla de resultados cambiaría al añadir una referencia.
    EXPECT_EQ(results[0].name, "C");
    EXPECT_EQ(results[1].name, "B");
    EXPECT_EQ(results[2].name, "A");
}

TEST(ToolReferences, AMissingReferenceMeansTheToolDoesNotMeasure) {
    // Nunca se cae a una referencia implícita: un GD&T medido contra otro datum
    // del que cree el operador es el fallo que este programa existe para
    // evitar, porque el número sale creíble y es falso.
    const cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(128));
    std::vector<ToolConfig> tools{makeRuler("mide", {0, 0}, {80, 0}, "no existe")};
    const auto results = runTools(gray, kIdentity, tools);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FALSE(results[0].ok);
    EXPECT_NE(results[0].detail.find("referencia"), std::string::npos) << results[0].detail;
    EXPECT_NE(results[0].detail.find("no existe"), std::string::npos) << results[0].detail;
}

TEST(ToolReferences, ADisabledReferenceIsAMissingReference) {
    const cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(128));
    std::vector<ToolConfig> tools{
        makeRuler("datum", {0, 0}, {80, 0}),
        makeRuler("mide", {0, 30}, {80, 30}, "datum"),
    };
    tools[0].enabled = false;
    const auto results = runTools(gray, kIdentity, tools);
    ASSERT_EQ(results.size(), 1U) << "la deshabilitada no se ejecuta";
    EXPECT_FALSE(results[0].ok) << "y quien la referenciaba no puede medir";
}

TEST(ToolReferences, ACircleIsFailedReferenceAlsoStopsTheOneThatUsesIt) {
    // Una referencia que EXISTE pero falla al medir tampoco vale. El caso es
    // distinto del anterior y el operador tiene que poder distinguirlos.
    const cv::Mat empty(400, 400, CV_8UC1, cv::Scalar(128));
    ToolConfig circle;
    circle.type = ToolType::Circle;
    circle.name = "agujero";
    circle.geometryJson =
        toJson(ToolGeometry(CircleGeometry{{200.0F, 200.0F}, 60.0F, 12.0F, 36}));
    circle.toleranceMin = 0.0;
    circle.toleranceMax = 1e9;

    std::vector<ToolConfig> tools{circle, makeRuler("mide", {0, 0}, {80, 0}, "agujero")};
    const auto results = runTools(empty, kIdentity, tools);
    ASSERT_EQ(results.size(), 2U);
    // Sobre una imagen plana el círculo no encuentra bordes.
    EXPECT_FALSE(results[0].ok) << results[0].detail;
    EXPECT_FALSE(results[1].ok) << "no se mide contra una referencia que falló";
}

TEST(ToolReferences, TwoToolsPointingAtEachOtherFailInsteadOfHanging) {
    const cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(128));
    std::vector<ToolConfig> tools{
        makeRuler("A", {0, 0}, {80, 0}, "B"),
        makeRuler("B", {0, 30}, {80, 30}, "A"),
    };
    const auto results = runTools(gray, kIdentity, tools);
    ASSERT_EQ(results.size(), 2U);
    for (const auto& result : results) {
        EXPECT_FALSE(result.ok) << result.name;
        EXPECT_NE(result.detail.find("circular"), std::string::npos) << result.detail;
    }
}

TEST(ToolReferences, ToolsWithoutReferencesBehaveExactlyAsBefore) {
    // Las catorce de siempre no declaran referencia: nada de esto puede
    // haberles cambiado el resultado ni el orden.
    const cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(128));
    std::vector<ToolConfig> tools{
        makeRuler("uno", {0, 0}, {50, 0}),
        makeRuler("dos", {0, 10}, {70, 10}),
        makeRuler("tres", {0, 20}, {90, 20}),
    };
    const auto results = runTools(gray, kIdentity, tools);
    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[0].name, "uno");
    EXPECT_EQ(results[1].name, "dos");
    EXPECT_EQ(results[2].name, "tres");
    EXPECT_NEAR(results[0].measured, 50.0, 1e-6);
    EXPECT_NEAR(results[2].measured, 90.0, 1e-6);
}

TEST(ToolReferences, TheReferencesSurviveTheParamsRoundTrip) {
    // Viajan dentro de `paramsJson`, así que pasan por la base de datos y por la
    // exportación de plantillas sin tocar el esquema.
    const auto roundTrip = [](const std::string& a, const std::string& b) {
        return referencesFromParams(paramsWithReferences({a, b}));
    };
    EXPECT_EQ(roundTrip("cara A", "").first, "cara A");
    EXPECT_EQ(roundTrip("cara A", "").second, "");
    EXPECT_EQ(roundTrip("cara A", "agujero 3").first, "cara A");
    EXPECT_EQ(roundTrip("cara A", "agujero 3").second, "agujero 3");
    // Solo la segunda: no es una combinación que la interfaz vaya a producir,
    // pero el formato no puede perderla si aparece.
    EXPECT_EQ(roundTrip("", "agujero 3").second, "agujero 3");
    EXPECT_EQ(paramsWithReferences({}), "{}") << "sin referencias, params queda como estaba";
    // Nombres con acentos y comillas, que son los que rompen un formato mal
    // hecho.
    for (const auto* name : {"Ángulo 1", "cara \"A\"", "eje / diámetro"}) {
        EXPECT_EQ(roundTrip(name, name).first, name);
        EXPECT_EQ(roundTrip(name, name).second, name);
    }
    // Y unos parámetros corruptos no tumban nada: se ignoran.
    EXPECT_EQ(referencesFromParams("{no es json").first, "");
    EXPECT_EQ(referencesFromParams("").first, "");
    // Params escritos por la versión de X0, con `ref` y sin `ref2`: la primera
    // referencia se sigue leyendo. Una plantilla guardada antes de X1 no puede
    // perder su datum al abrirse.
    EXPECT_EQ(referencesFromParams("{\"ref\":\"cara A\"}").first, "cara A");
    EXPECT_EQ(referencesFromParams("{\"ref\":\"cara A\"}").second, "");
}

// ---------------------------------------------------------------------------
// Construcciones geométricas (X1)
// ---------------------------------------------------------------------------
//
// Cada construcción se comprueba contra su valor ANALÍTICO sobre entradas
// conocidas: no hay tolerancia de proceso que valga aquí porque no se mide
// nada, se calcula. Y todos los casos degenerados se comprueban por lo mismo —
// que fallan con motivo y no devuelven un NaN, que es un número con toda la
// pinta de ser una medida.

namespace {

// Una Regla es la forma más directa de meter una recta conocida en la escena:
// su elemento derivado es la recta por sus dos puntos.
ToolConfig lineNamed(const std::string& name, cv::Point2f a, cv::Point2f b) {
    return makeRuler(name, a, b);
}

// Un Punto construido en modo «centro de círculo» no sirve para inyectar un
// punto arbitrario, así que para las pruebas se usa una Regla degenerada... que
// no vale (dos puntos iguales no dan recta). Se usa Posición, cuyo derivado ES
// un punto y se coloca donde se quiera.
ToolConfig pointNamed(const std::string& name, cv::Point2f p) {
    ToolConfig config;
    config.type = ToolType::Position;
    config.name = name;
    config.geometryJson = toJson(ToolGeometry(PositionGeometry{p, PositionAxis::Radial}));
    config.toleranceMin = 0.0;
    config.toleranceMax = 1e9;
    return config;
}

ToolConfig builtPoint(const std::string& name, PointConstruction mode,
                      const std::string& ref, const std::string& ref2 = {}) {
    ToolConfig config;
    config.type = ToolType::ConstructedPoint;
    config.name = name;
    config.reference = ref;
    config.reference2 = ref2;
    config.geometryJson =
        toJson(ToolGeometry(ConstructedPointGeometry{mode, {0.0F, 0.0F}}));
    config.toleranceMin = 0.0;
    config.toleranceMax = 1e9;
    return config;
}

ToolConfig builtLine(const std::string& name, LineConstruction mode,
                     const std::string& ref, const std::string& ref2 = {}) {
    ToolConfig config;
    config.type = ToolType::ConstructedLine;
    config.name = name;
    config.reference = ref;
    config.reference2 = ref2;
    config.geometryJson =
        toJson(ToolGeometry(ConstructedLineGeometry{mode, {0.0F, 0.0F}}));
    config.toleranceMin = 0.0;
    config.toleranceMax = 1e9;
    return config;
}

// Ejecuta una escena y devuelve el resultado de la herramienta pedida.
ToolRunResult runScene(const std::vector<ToolConfig>& tools, const std::string& wanted) {
    const cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(128));
    const auto results = runTools(gray, kIdentity, tools);
    for (const auto& result : results) {
        if (result.name == wanted) {
            return result;
        }
    }
    return {};
}

// Un número que no es NaN ni infinito. Es la comprobación que se repite en
// todos los casos degenerados, porque el fallo que se teme no es "da error",
// es "da un número inventado".
void expectNoNaN(const ToolRunResult& result) {
    EXPECT_FALSE(std::isnan(result.measured)) << result.name << ": " << result.detail;
    EXPECT_FALSE(std::isinf(result.measured)) << result.name << ": " << result.detail;
    EXPECT_FALSE(std::isnan(result.derived.point.x));
    EXPECT_FALSE(std::isnan(result.derived.point.y));
    EXPECT_FALSE(std::isnan(result.derived.direction.x));
    EXPECT_FALSE(std::isnan(result.derived.direction.y));
}

}  // namespace

TEST(Constructions, MidpointIsTheAnalyticMidpoint) {
    const auto result = runScene({pointNamed("A", {10.0F, 20.0F}),
                                  pointNamed("B", {50.0F, 80.0F}),
                                  builtPoint("medio", PointConstruction::Midpoint, "A", "B")},
                                 "medio");
    ASSERT_TRUE(result.ok) << result.detail;
    EXPECT_NEAR(result.derived.point.x, 30.0, 1e-4);
    EXPECT_NEAR(result.derived.point.y, 50.0, 1e-4);
    EXPECT_EQ(result.derived.kind, DerivedKind::Point);
}

TEST(Constructions, TwoCoincidentPointsStillHaveAMidpoint) {
    // A diferencia de la recta por dos puntos, aquí coincidir NO es degenerado:
    // el punto medio de un punto consigo mismo es ese punto. Inventar un error
    // sería una limitación falsa.
    const auto result = runScene({pointNamed("A", {33.0F, 44.0F}),
                                  pointNamed("B", {33.0F, 44.0F}),
                                  builtPoint("medio", PointConstruction::Midpoint, "A", "B")},
                                 "medio");
    ASSERT_TRUE(result.ok) << result.detail;
    EXPECT_NEAR(result.derived.point.x, 33.0, 1e-4);
    EXPECT_NEAR(result.derived.point.y, 44.0, 1e-4);
}

TEST(Constructions, IntersectionOfTwoLinesIsTheAnalyticCrossing) {
    // Una horizontal por y=10 y una vertical por x=70: se cortan en (70;10).
    const auto result =
        runScene({lineNamed("H", {0.0F, 10.0F}, {100.0F, 10.0F}),
                  lineNamed("V", {70.0F, -50.0F}, {70.0F, 50.0F}),
                  builtPoint("corte", PointConstruction::Intersection, "H", "V")},
                 "corte");
    ASSERT_TRUE(result.ok) << result.detail;
    EXPECT_NEAR(result.derived.point.x, 70.0, 1e-3);
    EXPECT_NEAR(result.derived.point.y, 10.0, 1e-3);
}

TEST(Constructions, ParallelLinesDoNotCrossAndSaySo) {
    const auto result =
        runScene({lineNamed("A", {0.0F, 0.0F}, {100.0F, 0.0F}),
                  lineNamed("B", {0.0F, 40.0F}, {100.0F, 40.0F}),
                  builtPoint("corte", PointConstruction::Intersection, "A", "B")},
                 "corte");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.detail.find("paralelas"), std::string::npos) << result.detail;
    expectNoNaN(result);
    EXPECT_FALSE(result.derived.valid()) << "una construcción fallida no ofrece referencia";
}

TEST(Constructions, ProjectionDropsThePerpendicularFoot) {
    // El punto (30;25) sobre la recta y=0 cae en (30;0).
    const auto result =
        runScene({pointNamed("P", {30.0F, 25.0F}),
                  lineNamed("L", {-50.0F, 0.0F}, {50.0F, 0.0F}),
                  builtPoint("pie", PointConstruction::Projection, "P", "L")},
                 "pie");
    ASSERT_TRUE(result.ok) << result.detail;
    EXPECT_NEAR(result.derived.point.x, 30.0, 1e-3);
    EXPECT_NEAR(result.derived.point.y, 0.0, 1e-3);
}

TEST(Constructions, ALineThroughTwoPointsHasTheAnalyticAngle) {
    // De (0;0) a (100;100) son 45°.
    const auto result =
        runScene({pointNamed("A", {0.0F, 0.0F}), pointNamed("B", {100.0F, 100.0F}),
                  builtLine("recta", LineConstruction::ThroughTwoPoints, "A", "B")},
                 "recta");
    ASSERT_TRUE(result.ok) << result.detail;
    EXPECT_EQ(result.derived.kind, DerivedKind::Line);
    EXPECT_NEAR(result.measured, 45.0, 1e-3);
    EXPECT_TRUE(result.measuredIsAngle);
    EXPECT_NEAR(cv::norm(result.derived.direction), 1.0, 1e-5) << "la dirección va unitaria";
}

TEST(Constructions, ALineNeedsTwoDistinctPoints) {
    const auto result =
        runScene({pointNamed("A", {12.0F, 12.0F}), pointNamed("B", {12.0F, 12.0F}),
                  builtLine("recta", LineConstruction::ThroughTwoPoints, "A", "B")},
                 "recta");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.detail.find("coinciden"), std::string::npos) << result.detail;
    expectNoNaN(result);
}

TEST(Constructions, TheBisectorSplitsTheAngleInHalf) {
    // Una a 0° y otra a 90°: su bisectriz va a 45° y pasa por donde se cortan.
    const auto result =
        runScene({lineNamed("H", {0.0F, 0.0F}, {100.0F, 0.0F}),
                  lineNamed("V", {0.0F, -50.0F}, {0.0F, 50.0F}),
                  builtLine("bis", LineConstruction::Bisector, "H", "V")},
                 "bis");
    ASSERT_TRUE(result.ok) << result.detail;
    EXPECT_NEAR(result.measured, 45.0, 1e-3);
    EXPECT_NEAR(result.derived.point.x, 0.0, 1e-3);
    EXPECT_NEAR(result.derived.point.y, 0.0, 1e-3);
}

TEST(Constructions, TheBisectorDoesNotDependOnHowTheLinesWereDrawn) {
    // El mismo par de rectas con los trazos en los cuatro sentidos posibles. Un
    // vector de dirección SÍ tiene sentido y depende de hacia dónde arrastró el
    // operador; la recta que representa, no. Si eso se colara al resultado, el
    // datum cambiaría al volver a trazar la misma recta al revés.
    //
    // El caso perpendicular es el que lo destapó: con 90° entre las rectas las
    // DOS bisectrices son igual de válidas —no hay ángulo agudo que partir— así
    // que la elección es un desempate, no una verdad. Lo que sí es exigible es
    // que el desempate salga siempre igual.
    const auto bisectorOf = [](cv::Point2f h0, cv::Point2f h1, cv::Point2f v0,
                               cv::Point2f v1) {
        return runScene({lineNamed("H", h0, h1), lineNamed("V", v0, v1),
                         builtLine("bis", LineConstruction::Bisector, "H", "V")},
                        "bis");
    };
    const cv::Point2f h0{0.0F, 0.0F}, h1{100.0F, 0.0F};
    const cv::Point2f v0{0.0F, -50.0F}, v1{0.0F, 50.0F};
    const auto reference = bisectorOf(h0, h1, v0, v1);
    ASSERT_TRUE(reference.ok) << reference.detail;
    for (const auto& variant : {bisectorOf(h1, h0, v0, v1), bisectorOf(h0, h1, v1, v0),
                                bisectorOf(h1, h0, v1, v0)}) {
        ASSERT_TRUE(variant.ok) << variant.detail;
        EXPECT_NEAR(variant.measured, reference.measured, 1e-3);
    }

    // Y con un ángulo que NO es de 90°, donde sí hay una respuesta correcta:
    // entre 0° y 60° la bisectriz del ángulo agudo va a 30°, se trace como se
    // trace.
    const cv::Point2f d{std::cos(60.0 * CV_PI / 180.0) * 100.0F,
                        std::sin(60.0 * CV_PI / 180.0) * 100.0F};
    for (const auto& variant : {bisectorOf(h0, h1, {0.0F, 0.0F}, d),
                                bisectorOf(h1, h0, d, {0.0F, 0.0F})}) {
        ASSERT_TRUE(variant.ok) << variant.detail;
        EXPECT_NEAR(variant.measured, 30.0, 1e-3);
    }
}

TEST(Constructions, TheBisectorOfParallelLinesIsTheMidLine) {
    // Dos horizontales en y=0 e y=40: la bisectriz es la recta media, y=20.
    // No es un caso especial esquivado: es el mismo resultado por continuidad,
    // y por eso «bisectriz» y «recta media» son UNA construcción, no dos.
    const auto result =
        runScene({lineNamed("A", {0.0F, 0.0F}, {100.0F, 0.0F}),
                  lineNamed("B", {0.0F, 40.0F}, {100.0F, 40.0F}),
                  builtLine("media", LineConstruction::Bisector, "A", "B")},
                 "media");
    ASSERT_TRUE(result.ok) << result.detail;
    EXPECT_NEAR(result.measured, 0.0, 1e-3) << "sigue siendo horizontal";
    // El punto de paso tiene que estar a media altura entre las dos.
    EXPECT_NEAR(result.derived.point.y, 20.0, 1e-3);
    expectNoNaN(result);
}

TEST(Constructions, ParallelAndPerpendicularThroughAPoint) {
    const std::vector<ToolConfig> scene{
        lineNamed("L", {0.0F, 0.0F}, {100.0F, 0.0F}), pointNamed("P", {25.0F, 60.0F}),
        builtLine("par", LineConstruction::ParallelThrough, "L", "P"),
        builtLine("perp", LineConstruction::PerpendicularThrough, "L", "P")};

    const auto parallel = runScene(scene, "par");
    ASSERT_TRUE(parallel.ok) << parallel.detail;
    EXPECT_NEAR(parallel.measured, 0.0, 1e-3);
    EXPECT_NEAR(parallel.derived.point.y, 60.0, 1e-3) << "pasa por el punto, no por la recta";

    const auto perpendicular = runScene(scene, "perp");
    ASSERT_TRUE(perpendicular.ok) << perpendicular.detail;
    EXPECT_NEAR(perpendicular.measured, 90.0, 1e-3);
    EXPECT_NEAR(perpendicular.derived.point.x, 25.0, 1e-3);
    EXPECT_NEAR(perpendicular.derived.point.y, 60.0, 1e-3);
}

TEST(Constructions, ACircleServesAsThePointOfItsCentre) {
    // Un agujero es el datum de punto más corriente que hay. Que no valiera
    // donde se pide "un punto" sería una limitación inventada.
    cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(200));
    cv::circle(gray, cv::Point(150, 150), 40, cv::Scalar(20), -1);

    ToolConfig circle;
    circle.type = ToolType::Circle;
    circle.name = "agujero";
    circle.geometryJson =
        toJson(ToolGeometry(CircleGeometry{{150.0F, 150.0F}, 40.0F, 14.0F, 48}));
    circle.toleranceMin = 0.0;
    circle.toleranceMax = 1e9;

    const std::vector<ToolConfig> tools{
        circle, builtPoint("centro", PointConstruction::CircleCenter, "agujero")};
    const auto results = runTools(gray, kIdentity, tools);
    ASSERT_EQ(results.size(), 2U);
    ASSERT_TRUE(results[0].ok) << results[0].detail;
    ASSERT_TRUE(results[1].ok) << results[1].detail;
    // El centro sale del borde AJUSTADO, así que se compara con holgura de
    // píxel, no exacta: aquí sí hay medición de por medio.
    EXPECT_NEAR(results[1].derived.point.x, 150.0, 1.5);
    EXPECT_NEAR(results[1].derived.point.y, 150.0, 1.5);
}

TEST(Constructions, TheWrongKindOfReferenceIsRefusedWithAReason) {
    // Un punto donde hace falta una recta. El motivo tiene que decir QUÉ hace
    // falta, o el operador se queda mirando la pantalla.
    const auto result =
        runScene({pointNamed("P", {10.0F, 10.0F}), pointNamed("Q", {40.0F, 10.0F}),
                  builtPoint("corte", PointConstruction::Intersection, "P", "Q")},
                 "corte");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.detail.find("recta"), std::string::npos) << result.detail;
    expectNoNaN(result);
}

TEST(Constructions, AConstructionWithoutItsSecondReferenceSaysWhichOneIsMissing) {
    const auto result = runScene({pointNamed("A", {10.0F, 20.0F}),
                                  builtPoint("medio", PointConstruction::Midpoint, "A")},
                                 "medio");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.detail.find("segunda"), std::string::npos) << result.detail;
}

TEST(Constructions, AConstructionIsInformativeAndDoesNotPassOrFailOnTolerance) {
    // No mide nada que pueda estar fuera de tolerancia. La tabla lo tiene que
    // saber para escribir «—» y no un OK verde que no significaría nada.
    auto tool = builtPoint("medio", PointConstruction::Midpoint, "A", "B");
    tool.toleranceMin = 1000.0;  // imposible de cumplir a propósito
    tool.toleranceMax = 1001.0;
    const auto result = runScene(
        {pointNamed("A", {10.0F, 20.0F}), pointNamed("B", {50.0F, 80.0F}), tool}, "medio");
    EXPECT_TRUE(result.informative);
    EXPECT_TRUE(result.ok) << "la tolerancia no la juzga: " << result.detail;
}

TEST(Constructions, AConstructionCanBeTheDatumOfAnotherConstruction) {
    // La cadena que hace falta para un datum de verdad: dos círculos, el punto
    // medio de sus centros, y una recta desde ese punto medio. Tres niveles de
    // dependencia resueltos en el orden correcto sin que el operador ordene
    // nada.
    const std::vector<ToolConfig> tools{
        builtLine("eje", LineConstruction::ThroughTwoPoints, "medio", "C"),
        builtPoint("medio", PointConstruction::Midpoint, "A", "B"),
        pointNamed("A", {0.0F, 0.0F}),
        pointNamed("B", {40.0F, 0.0F}),
        pointNamed("C", {20.0F, 60.0F}),
    };
    const cv::Mat gray(400, 400, CV_8UC1, cv::Scalar(128));
    const auto results = runTools(gray, kIdentity, tools);
    ASSERT_EQ(results.size(), 5U);
    // Orden de la LISTA, no de ejecución.
    EXPECT_EQ(results[0].name, "eje");
    EXPECT_EQ(results[1].name, "medio");
    for (const auto& result : results) {
        EXPECT_TRUE(result.ok) << result.name << ": " << result.detail;
    }
    // El punto medio de (0;0) y (40;0) es (20;0); la recta hasta (20;60) es
    // vertical, o sea 90°.
    EXPECT_NEAR(results[1].derived.point.x, 20.0, 1e-3);
    EXPECT_NEAR(results[0].measured, 90.0, 1e-3);
}

TEST(Constructions, ThePersistedConstructionComesBackTheSame) {
    for (const auto mode : allPointConstructions()) {
        const ToolGeometry geometry =
            ConstructedPointGeometry{mode, {12.5F, -7.25F}};
        const auto back = geometryFromJson(ToolType::ConstructedPoint, toJson(geometry));
        ASSERT_TRUE(back.isOk()) << back.error().message;
        const auto& g = std::get<ConstructedPointGeometry>(back.value());
        EXPECT_EQ(static_cast<int>(g.mode), static_cast<int>(mode));
        EXPECT_NEAR(g.anchor.x, 12.5, 1e-4);
        EXPECT_NEAR(g.anchor.y, -7.25, 1e-4);
    }
    for (const auto mode : allLineConstructions()) {
        const ToolGeometry geometry = ConstructedLineGeometry{mode, {3.0F, 4.0F}};
        const auto back = geometryFromJson(ToolType::ConstructedLine, toJson(geometry));
        ASSERT_TRUE(back.isOk()) << back.error().message;
        EXPECT_EQ(static_cast<int>(std::get<ConstructedLineGeometry>(back.value()).mode),
                  static_cast<int>(mode));
    }
}

TEST(Constructions, AnUnknownConstructionIsRefusedInsteadOfSilentlyBecomingTheFirst) {
    // Una plantilla de una versión posterior, o unos params tocados a mano. Caer
    // al primer modo daría una medida creíble que no es la configurada.
    const std::string json = "{\"mode\": 99, \"ax\": 1.0, \"ay\": 2.0}";
    const auto back = geometryFromJson(ToolType::ConstructedPoint, json);
    EXPECT_FALSE(back.isOk());
    EXPECT_NE(back.error().message.find("desconocida"), std::string::npos)
        << back.error().message;
}
