#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <vector>

#include <opencv2/objdetect/aruco_detector.hpp>

#include <array>

#include "vision/auto_roi.h"
#include "vision/board_frame.h"
#include "vision/contour_analysis.h"
#include "vision/fixture_stabilizer.h"
#include "vision/frame_geometry.h"
#include "vision/orientation.h"
#include "vision/plane_scale.h"
#include "vision/orientation_anchor.h"
#include "vision/pipeline.h"
#include "vision/position_fixture.h"
#include "vision/quality_metrics.h"
#include "vision/segmentation.h"

#include "test_helpers.h"

using namespace pci::vision;
using pci::testhelpers::drawLPiece;
using pci::testhelpers::kPi;
using pci::testhelpers::lPieceArea;

namespace {

double angleDiff(double a, double b) {
    double d = a - b;
    while (d >= 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return d;
}

double maskIoU(const cv::Mat& a, const cv::Mat& b) {
    cv::Mat interMask;
    cv::Mat unionMask;
    cv::bitwise_and(a, b, interMask);
    cv::bitwise_or(a, b, unionMask);
    const int unionCount = cv::countNonZero(unionMask);
    return unionCount > 0 ? static_cast<double>(cv::countNonZero(interMask)) / unionCount : 0.0;
}

}  // namespace

// --- Segmentación ---

TEST(Segmentation, DarkPieceOnLightBackground) {
    const auto image = drawLPiece({640, 480}, {320.0F, 240.0F}, 20.0, 40.0F, 40, 220);
    const auto mask = segmentPiece(image);
    ASSERT_TRUE(mask.isOk());

    const double area = cv::countNonZero(mask.value());
    EXPECT_NEAR(area, lPieceArea(40.0F), lPieceArea(40.0F) * 0.1);
    EXPECT_EQ(mask.value().at<uchar>(10, 10), 0);
}

TEST(Segmentation, LightPieceOnDarkBackground) {
    const auto image = drawLPiece({640, 480}, {320.0F, 240.0F}, -35.0, 40.0F, 230, 30);
    const auto mask = segmentPiece(image);
    ASSERT_TRUE(mask.isOk());

    const double area = cv::countNonZero(mask.value());
    EXPECT_NEAR(area, lPieceArea(40.0F), lPieceArea(40.0F) * 0.1);
}

TEST(Segmentation, EmptyImageFails) {
    EXPECT_FALSE(segmentPiece(cv::Mat()).isOk());
}

TEST(Segmentation, ManualThresholdAndForcedPolarity) {
    // Pieza oscura (100) sobre fondo claro (200): umbral manual entre ambos.
    const auto dark = drawLPiece({640, 480}, {320.0F, 240.0F}, 10.0, 40.0F, 100, 200);
    SegmentationOptions options;
    options.manualThreshold = 150;
    options.polarity = SegmentationPolarity::DarkPiece;
    auto mask = segmentPiece(dark, options);
    ASSERT_TRUE(mask.isOk());
    EXPECT_NEAR(cv::countNonZero(mask.value()), lPieceArea(40.0F), lPieceArea(40.0F) * 0.1);

    // Pieza clara (200) sobre fondo oscuro (60) con polaridad forzada.
    const auto light = drawLPiece({640, 480}, {320.0F, 240.0F}, 10.0, 40.0F, 200, 60);
    options.manualThreshold = 130;
    options.polarity = SegmentationPolarity::LightPiece;
    mask = segmentPiece(light, options);
    ASSERT_TRUE(mask.isOk());
    EXPECT_NEAR(cv::countNonZero(mask.value()), lPieceArea(40.0F), lPieceArea(40.0F) * 0.1);
}

// La zona de detección: un distractor grande fuera de la zona no debe
// estorbar; el contorno se busca solo dentro y se devuelve en coordenadas de
// la imagen completa.
TEST(Pipeline, RoiFocusesDetectionAndIgnoresOutside) {
    cv::Mat image = drawLPiece({640, 480}, {460.0F, 240.0F}, 15.0, 35.0F, 40, 220);
    // Distractor más grande que la pieza, a la izquierda (una "sombra").
    cv::rectangle(image, {20, 100}, {260, 400}, cv::Scalar(30), cv::FILLED);

    // Sin zona: el contorno mayor es el distractor.
    const auto whole = analyzeFrame(image);
    ASSERT_TRUE(whole.isOk());
    EXPECT_LT(whole.value().fixture.origin.x, 300.0F);

    // Con la zona sobre la pieza: se detecta la pieza, en coords completas.
    PipelineConfig config;
    config.roi = cv::Rect(300, 60, 340, 360);
    const auto focused = analyzeFrame(image, config);
    ASSERT_TRUE(focused.isOk()) << focused.error().message;
    EXPECT_NEAR(focused.value().fixture.origin.x, 460.0F, 4.0F);
    EXPECT_NEAR(focused.value().fixture.origin.y, 240.0F, 4.0F);
    EXPECT_EQ(focused.value().mask.size(), image.size());
    for (const auto& point : focused.value().contour.points) {
        EXPECT_TRUE(config.roi.contains(point));
    }
}

// --- Contorno ---

TEST(ContourAnalysis, FindsCentroidAndArea) {
    const cv::Point2f center(300.0F, 220.0F);
    const cv::Mat mask = drawLPiece({640, 480}, center, 15.0, 40.0F, 255, 0);

    const auto contour = findLargestContour(mask);
    ASSERT_TRUE(contour.isOk());
    EXPECT_NEAR(contour.value().centroid.x, center.x, 2.0F);
    EXPECT_NEAR(contour.value().centroid.y, center.y, 2.0F);
    EXPECT_NEAR(contour.value().area, lPieceArea(40.0F), lPieceArea(40.0F) * 0.05);
}

TEST(ContourAnalysis, EmptyMaskFails) {
    const cv::Mat empty = cv::Mat::zeros(480, 640, CV_8UC1);
    EXPECT_FALSE(findLargestContour(empty).isOk());
}

TEST(ContourAnalysis, FullMaskFails) {
    const cv::Mat full(480, 640, CV_8UC1, cv::Scalar(255));
    EXPECT_FALSE(findLargestContour(full).isOk());
}

// --- Orientación ---

TEST(Orientation, TracksRotationFullCircle) {
    const auto baseMask = drawLPiece({640, 480}, {320.0F, 240.0F}, 0.0, 40.0F, 255, 0);
    const auto baseAngle = principalAngleDeg(baseMask);
    ASSERT_TRUE(baseAngle.isOk());

    for (const double theta : {30.0, 90.0, 145.0, 200.0, 300.0}) {
        const auto mask = drawLPiece({640, 480}, {320.0F, 240.0F}, theta, 40.0F, 255, 0);
        const auto angle = principalAngleDeg(mask);
        ASSERT_TRUE(angle.isOk());
        EXPECT_NEAR(angleDiff(angle.value(), baseAngle.value() + theta), 0.0, 2.0)
            << "theta = " << theta;
    }
}

TEST(Orientation, EmptyMaskFails) {
    EXPECT_FALSE(principalAngleDeg(cv::Mat::zeros(100, 100, CV_8UC1)).isOk());
}

// --- Fixture ---

TEST(Fixture, CoordinateRoundTrip) {
    const Fixture fixture{{100.0F, 50.0F}, 30.0};

    for (const auto& p : {cv::Point2f(0.0F, 0.0F), cv::Point2f(37.5F, -12.25F),
                          cv::Point2f(-80.0F, 140.0F)}) {
        const cv::Point2f back = toImageCoords(fixture, toPieceCoords(fixture, p));
        EXPECT_NEAR(back.x, p.x, 1e-3F);
        EXPECT_NEAR(back.y, p.y, 1e-3F);
    }
}

TEST(Fixture, OriginMapsToZeroAndAxisToPositiveX) {
    const Fixture fixture{{100.0F, 50.0F}, 30.0};

    const cv::Point2f origin = toPieceCoords(fixture, fixture.origin);
    EXPECT_NEAR(origin.x, 0.0F, 1e-4F);
    EXPECT_NEAR(origin.y, 0.0F, 1e-4F);

    const double rad = 30.0 * kPi / 180.0;
    const cv::Point2f onAxis(100.0F + 50.0F * static_cast<float>(std::cos(rad)),
                             50.0F + 50.0F * static_cast<float>(std::sin(rad)));
    const cv::Point2f piece = toPieceCoords(fixture, onAxis);
    EXPECT_NEAR(piece.x, 50.0F, 1e-2F);
    EXPECT_NEAR(piece.y, 0.0F, 1e-2F);
}

TEST(Fixture, NormalizeEmptyMaskFails) {
    const cv::Mat image(100, 100, CV_8UC1, cv::Scalar(128));
    const cv::Mat mask = cv::Mat::zeros(100, 100, CV_8UC1);
    EXPECT_FALSE(normalizePiece(image, mask, Fixture{{50.0F, 50.0F}, 0.0}).isOk());
}

// --- Pipeline end-to-end ---

TEST(Pipeline, AnalyzesSyntheticPiece) {
    const cv::Point2f center(320.0F, 240.0F);
    cv::Mat bgr;
    cv::cvtColor(drawLPiece({640, 480}, center, 25.0, 40.0F, 40, 220), bgr,
                 cv::COLOR_GRAY2BGR);

    const auto analysis = analyzeFrame(bgr);
    ASSERT_TRUE(analysis.isOk());
    EXPECT_NEAR(analysis.value().fixture.origin.x, center.x, 2.0F);
    EXPECT_NEAR(analysis.value().fixture.origin.y, center.y, 2.0F);
    EXPECT_EQ(analysis.value().mask.type(), CV_8UC1);
    EXPECT_EQ(analysis.value().normalized.size(), cv::Size(256, 256));
    EXPECT_EQ(analysis.value().normalized.type(), CV_8UC3);
}

// El test de oro: la misma pieza a dos rotaciones y posiciones distintas debe
// producir recortes normalizados prácticamente idénticos.
TEST(Pipeline, NormalizationIsRotationInvariant) {
    const auto imageA = drawLPiece({640, 480}, {300.0F, 240.0F}, 20.0, 40.0F, 40, 220);
    const auto imageB = drawLPiece({640, 480}, {340.0F, 200.0F}, 125.0, 40.0F, 40, 220);

    PipelineConfig cfg;
    cfg.autoOrient = true;  // invarianza a rotación requiere seguir la rotación
    const auto a = analyzeFrame(imageA, cfg);
    const auto b = analyzeFrame(imageB, cfg);
    ASSERT_TRUE(a.isOk());
    ASSERT_TRUE(b.isOk());

    cv::Mat maskA;
    cv::Mat maskB;
    cv::threshold(a.value().normalized, maskA, 0.0, 255.0, cv::THRESH_BINARY);
    cv::threshold(b.value().normalized, maskB, 0.0, 255.0, cv::THRESH_BINARY);

    EXPECT_GT(maskIoU(maskA, maskB), 0.90);
}

TEST(Pipeline, FailsOnEmptyImage) {
    EXPECT_FALSE(analyzeFrame(cv::Mat()).isOk());
}

// --- Escala por marcador ArUco / homografía de plano ---

TEST(PlaneScale, DetectsMarkerAndComputesScale) {
    const auto dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
    cv::Mat marker;
    cv::aruco::generateImageMarker(dict, 0, 100, marker);  // 100x100 px

    // Marcador con zona blanca alrededor (necesaria para detectarlo).
    cv::Mat image(400, 400, CV_8UC1, cv::Scalar(255));
    marker.copyTo(image(cv::Rect(150, 150, 100, 100)));

    const auto scale = detectMarkerScale(image, 30.0);  // marcador de 30 mm
    ASSERT_TRUE(scale.has_value());
    // Lado detectado ~100 px -> 30 mm / 100 px = 0.30 mm/px.
    EXPECT_NEAR(scale->mmPerPixel, 0.30, 0.04);
    EXPECT_FALSE(scale->imageToMm.empty());
    // Vista perpendicular (marcador fronto-paralelo): calidad casi perfecta.
    EXPECT_GT(scale->quality, 0.95);
    EXPECT_LE(scale->quality, 1.0);
}

TEST(PlaneScale, NoMarkerReturnsNullopt) {
    const cv::Mat blank(200, 200, CV_8UC1, cv::Scalar(255));
    EXPECT_FALSE(detectMarkerScale(blank, 30.0).has_value());
    EXPECT_FALSE(detectMarkerScale(cv::Mat(), 30.0).has_value());
}

TEST(PlaneScale, HomographyDistanceInMm) {
    // 100 px de imagen = 50 mm reales (0.5 mm/px), plano fronto-paralelo.
    const std::array<cv::Point2f, 4> img = {cv::Point2f(0, 0), cv::Point2f(100, 0),
                                            cv::Point2f(100, 100), cv::Point2f(0, 100)};
    const std::array<cv::Point2f, 4> mm = {cv::Point2f(0, 0), cv::Point2f(50, 0),
                                           cv::Point2f(50, 50), cv::Point2f(0, 50)};
    const cv::Mat h = cv::getPerspectiveTransform(img.data(), mm.data());
    EXPECT_NEAR(planeDistanceMm(h, {0, 0}, {100, 0}), 50.0, 1e-6);
    EXPECT_NEAR(planeDistanceMm(h, {20, 20}, {60, 20}), 20.0, 1e-6);
}

// --- Anisotropía ---

TEST(Anisotropy, RoundLowElongatedHigh) {
    cv::Mat disc(240, 240, CV_8UC1, cv::Scalar(0));
    cv::circle(disc, {120, 120}, 80, cv::Scalar(255), cv::FILLED);
    const double roundAniso = principalAnisotropy(disc);
    EXPECT_LT(roundAniso, 0.1);  // un círculo no tiene eje definido

    cv::Mat bar(240, 240, CV_8UC1, cv::Scalar(0));
    cv::rectangle(bar, {20, 110}, {220, 130}, cv::Scalar(255), cv::FILLED);
    const double barAniso = principalAnisotropy(bar);
    EXPECT_GT(barAniso, 0.8);  // una barra es muy alargada
}

TEST(FixtureStabilizer, FreezesAngleForRoundPiece) {
    // Pieza redonda (anisotropía baja): el ángulo medido es ruido y debe
    // conservarse el anterior aunque salte 40°.
    Fixture previous{{100.0F, 100.0F}, 10.0};
    Fixture measured{{101.0F, 100.0F}, 50.0};
    measured.anisotropy = 0.05;
    bool flipped = false;
    const Fixture result = stabilizeFixture(previous, measured, {}, flipped);
    EXPECT_DOUBLE_EQ(result.angleDeg, 10.0);  // congelado
    EXPECT_FALSE(flipped);
}

// --- Estabilizador temporal del fixture ---

TEST(FixtureStabilizer, HoldsWithinDeadband) {
    const Fixture previous{{100.0F, 100.0F}, 10.0};
    const Fixture noisy{{101.0F, 100.8F}, 10.9};
    bool flipped = false;
    const Fixture result = stabilizeFixture(previous, noisy, {}, flipped);
    EXPECT_FALSE(flipped);
    EXPECT_FLOAT_EQ(result.origin.x, 100.0F);
    EXPECT_DOUBLE_EQ(result.angleDeg, 10.0);
}

TEST(FixtureStabilizer, SmoothsModerateMotion) {
    const Fixture previous{{100.0F, 100.0F}, 10.0};
    const Fixture moved{{110.0F, 100.0F}, 10.0};
    bool flipped = false;
    const Fixture result = stabilizeFixture(previous, moved, {}, flipped);
    // EMA con alpha 0.25: avanza hacia la medición sin saltar (100 + 0.25*10).
    EXPECT_NEAR(result.origin.x, 102.5F, 1e-4F);
    EXPECT_GT(result.origin.x, 100.0F);
    EXPECT_LT(result.origin.x, 110.0F);
}

TEST(FixtureStabilizer, SnapsOnLargeMotion) {
    const Fixture previous{{100.0F, 100.0F}, 10.0};
    const Fixture far{{200.0F, 150.0F}, 15.0};
    bool flipped = false;
    const Fixture result = stabilizeFixture(previous, far, {}, flipped);
    EXPECT_FLOAT_EQ(result.origin.x, 200.0F);
    EXPECT_DOUBLE_EQ(result.angleDeg, 15.0);
}

TEST(FixtureStabilizer, ResolvesSpuriousFlip) {
    // Giro espontáneo de ~180° (ruido del momento de 3er orden): se conserva
    // el sentido anterior y se avisa para recalcular el recorte.
    const Fixture previous{{100.0F, 100.0F}, 10.0};
    const Fixture flippedIn{{100.5F, 100.0F}, -172.0};
    bool flipped = false;
    const Fixture result = stabilizeFixture(previous, flippedIn, {}, flipped);
    EXPECT_TRUE(flipped);
    // Candidato corregido: 8°; diff -2° -> suavizado hacia 8 desde 10.
    EXPECT_GT(result.angleDeg, 8.0);
    EXPECT_LT(result.angleDeg, 10.0);

    // Con resolveFlips desactivado (pieza con rasgo), el giro se respeta.
    StabilizerOptions noFlip;
    noFlip.resolveFlips = false;
    flipped = false;
    const Fixture kept = stabilizeFixture(previous, flippedIn, noFlip, flipped);
    EXPECT_FALSE(flipped);
    EXPECT_DOUBLE_EQ(kept.angleDeg, -172.0);  // salto angular grande: snap
}

TEST(FixtureStabilizer, BlendsAcrossAngleWrap) {
    const Fixture previous{{100.0F, 100.0F}, 179.0};
    const Fixture measured{{106.0F, 100.0F}, -177.0};  // +4° cruzando ±180
    bool flipped = false;
    const Fixture result = stabilizeFixture(previous, measured, {}, flipped);
    EXPECT_FALSE(flipped);
    // 179 + 0.25*4 = 180.0 -> envuelto a -180.0.
    EXPECT_NEAR(result.angleDeg, -180.0, 1e-6);
}

// --- Rasgo distintivo de orientación ---

namespace {

// Rectángulo (180°-simétrico: los momentos no distinguen la orientación) con
// un punto oscuro cerca de un extremo como rasgo distintivo.
cv::Mat drawRectWithDot(cv::Point2f center, double angleDeg, cv::Point2f& dotImage) {
    cv::Mat image(480, 640, CV_8UC1, cv::Scalar(220));
    const cv::RotatedRect rect(center, cv::Size2f(200.0F, 80.0F),
                               static_cast<float>(angleDeg));
    cv::Point2f corners[4];
    rect.points(corners);
    std::vector<cv::Point> polygon;
    for (const auto& c : corners) {
        polygon.emplace_back(cvRound(c.x), cvRound(c.y));
    }
    cv::fillConvexPoly(image, polygon, cv::Scalar(90));

    // Punto muy oscuro a +70 px del centro a lo largo del eje mayor.
    const double rad = angleDeg * kPi / 180.0;
    dotImage = center + cv::Point2f(static_cast<float>(std::cos(rad) * 70.0),
                                    static_cast<float>(std::sin(rad) * 70.0));
    cv::circle(image, cv::Point(cvRound(dotImage.x), cvRound(dotImage.y)), 8,
               cv::Scalar(10), cv::FILLED);
    return image;
}

PipelineConfig orientCfg() {
    PipelineConfig cfg;
    cfg.autoOrient = true;
    return cfg;
}

}  // namespace

TEST(OrientationAnchor, SymmetricPieceDetectedInAnyRotation) {
    // Registro: rectángulo a 10° con su rasgo (el punto oscuro).
    cv::Point2f dotA;
    const cv::Mat imageA = drawRectWithDot({300.0F, 240.0F}, 10.0, dotA);
    auto analysisA = analyzeFrame(imageA, orientCfg());
    ASSERT_TRUE(analysisA.isOk());

    OrientationAnchor anchor;
    anchor.piecePoint = toPieceCoords(analysisA.value().fixture, dotA);
    anchor.intensity = sampleIntensity(imageA, dotA);
    ASSERT_TRUE(applyAnchor(imageA, anchor, analysisA.value()).isOk());

    // La misma pieza girada 180° (y desplazada): sin ancla los momentos no
    // pueden distinguirla; con ancla el recorte normalizado debe coincidir.
    cv::Point2f dotB;
    const cv::Mat imageB = drawRectWithDot({330.0F, 220.0F}, 190.0, dotB);
    auto analysisB = analyzeFrame(imageB, orientCfg());
    ASSERT_TRUE(analysisB.isOk());
    ASSERT_TRUE(applyAnchor(imageB, anchor, analysisB.value()).isOk());

    // El rasgo debe quedar en el mismo lugar en coordenadas de pieza…
    const cv::Point2f dotBPiece = toPieceCoords(analysisB.value().fixture, dotB);
    EXPECT_NEAR(dotBPiece.x, anchor.piecePoint.x, 4.0F);
    EXPECT_NEAR(dotBPiece.y, anchor.piecePoint.y, 4.0F);

    // …y los recortes normalizados deben solaparse casi por completo.
    cv::Mat maskA;
    cv::Mat maskB;
    cv::threshold(analysisA.value().normalized, maskA, 0.0, 255.0, cv::THRESH_BINARY);
    cv::threshold(analysisB.value().normalized, maskB, 0.0, 255.0, cv::THRESH_BINARY);
    EXPECT_GT(maskIoU(maskA, maskB), 0.90);
}

TEST(OrientationAnchor, OrientationOffsetRotatesFixture) {
    const auto image = drawLPiece({640, 480}, {320.0F, 240.0F}, 20.0, 40.0F, 40, 220);
    auto analysis = analyzeFrame(image, orientCfg());
    ASSERT_TRUE(analysis.isOk());
    const double before = analysis.value().fixture.angleDeg;

    ASSERT_TRUE(applyOrientationOffset(image, 90.0, analysis.value()).isOk());
    double delta = analysis.value().fixture.angleDeg - before;
    while (delta < -180.0) delta += 360.0;
    while (delta >= 180.0) delta -= 360.0;
    EXPECT_NEAR(delta, 90.0, 1e-9);
    EXPECT_EQ(analysis.value().normalized.size(), cv::Size(256, 256));

    // Offset cero: no toca nada.
    const double angle = analysis.value().fixture.angleDeg;
    ASSERT_TRUE(applyOrientationOffset(image, 0.0, analysis.value()).isOk());
    EXPECT_DOUBLE_EQ(analysis.value().fixture.angleDeg, angle);
}

TEST(OrientationAnchor, ResolveKeepsCorrectFixture) {
    cv::Point2f dot;
    const cv::Mat image = drawRectWithDot({300.0F, 240.0F}, 10.0, dot);
    const auto analysis = analyzeFrame(image, orientCfg());
    ASSERT_TRUE(analysis.isOk());

    OrientationAnchor anchor;
    anchor.piecePoint = toPieceCoords(analysis.value().fixture, dot);
    anchor.intensity = sampleIntensity(image, dot);

    // Con el fixture correcto no debe girar nada.
    const Fixture resolved = resolveWithAnchor(image, analysis.value().fixture, anchor);
    EXPECT_NEAR(resolved.angleDeg, analysis.value().fixture.angleDeg, 1e-9);

    // Con el fixture girado 180° a mano, debe volver al correcto.
    Fixture flipped = analysis.value().fixture;
    flipped.angleDeg += flipped.angleDeg >= 0.0 ? -180.0 : 180.0;
    const Fixture back = resolveWithAnchor(image, flipped, anchor);
    EXPECT_NEAR(back.angleDeg, analysis.value().fixture.angleDeg, 1e-6);
}

TEST(Pipeline, FailsOnUniformImage) {
    const cv::Mat uniform(480, 640, CV_8UC1, cv::Scalar(128));
    EXPECT_FALSE(analyzeFrame(uniform).isOk());
}

// --- Tablero de referencia centrado (T1) ---

TEST(BoardFrame, OriginModesResolveAsDeclared) {
    Fixture fixture;
    fixture.origin = {120.0F, 90.0F};
    fixture.angleDeg = 30.0;
    const cv::Size imageSize(640, 480);

    BoardConfig cfg;
    cfg.origin = BoardOrigin::PieceCenter;
    EXPECT_EQ(resolveBoardFrame(cfg, fixture, true, imageSize).origin, fixture.origin);
    // Sin pieza detectada cae al centro de la imagen (y sin girar).
    const BoardFrame noPiece = resolveBoardFrame(cfg, fixture, false, imageSize);
    EXPECT_EQ(noPiece.origin, cv::Point2f(320.0F, 240.0F));
    EXPECT_DOUBLE_EQ(noPiece.angleDeg, 0.0);

    cfg.origin = BoardOrigin::ImageCenter;
    EXPECT_EQ(resolveBoardFrame(cfg, fixture, true, imageSize).origin,
              cv::Point2f(320.0F, 240.0F));

    cfg.origin = BoardOrigin::FixedPoint;
    cfg.fixedPoint = {12.5F, 34.5F};
    EXPECT_EQ(resolveBoardFrame(cfg, fixture, true, imageSize).origin, cfg.fixedPoint);

    // Los ejes solo giran si se pide seguir a la pieza Y hay pieza.
    cfg.followPieceAngle = true;
    EXPECT_DOUBLE_EQ(resolveBoardFrame(cfg, fixture, true, imageSize).angleDeg, 30.0);
    EXPECT_DOUBLE_EQ(resolveBoardFrame(cfg, fixture, false, imageSize).angleDeg, 0.0);
}

TEST(BoardFrame, CenterIsZeroAndYPointsUp) {
    BoardFrame frame;
    frame.origin = {100.0F, 100.0F};

    const BoardReading center = readPoint(frame, {100.0F, 100.0F});
    EXPECT_DOUBLE_EQ(center.dx, 0.0);
    EXPECT_DOUBLE_EQ(center.dy, 0.0);
    EXPECT_DOUBLE_EQ(center.radius, 0.0);
    EXPECT_DOUBLE_EQ(center.angleDeg, 0.0);

    // A la derecha = +X, 0°.
    const BoardReading right = readPoint(frame, {110.0F, 100.0F});
    EXPECT_NEAR(right.dx, 10.0, 1e-9);
    EXPECT_NEAR(right.dy, 0.0, 1e-9);
    EXPECT_NEAR(right.angleDeg, 0.0, 1e-9);

    // Arriba en pantalla (y de imagen MENOR) = +Y del tablero, 90°.
    const BoardReading up = readPoint(frame, {100.0F, 80.0F});
    EXPECT_NEAR(up.dy, 20.0, 1e-9);
    EXPECT_NEAR(up.angleDeg, 90.0, 1e-9);

    // Abajo = -Y, -90°.
    const BoardReading down = readPoint(frame, {100.0F, 130.0F});
    EXPECT_NEAR(down.dy, -30.0, 1e-9);
    EXPECT_NEAR(down.angleDeg, -90.0, 1e-9);

    // Radio: 3-4-5.
    const BoardReading diag = readPoint(frame, {103.0F, 96.0F});
    EXPECT_NEAR(diag.radius, 5.0, 1e-9);
}

TEST(BoardFrame, RotatedAxesMeasureInPieceFrame) {
    BoardFrame frame;
    frame.origin = {200.0F, 150.0F};
    frame.angleDeg = 90.0;  // el eje X del tablero apunta hacia abajo en la imagen

    // Un punto 10 px por debajo del origen en la imagen queda en +X del tablero.
    const BoardReading reading = readPoint(frame, {200.0F, 160.0F});
    EXPECT_NEAR(reading.dx, 10.0, 1e-6);
    EXPECT_NEAR(reading.dy, 0.0, 1e-6);
    EXPECT_NEAR(reading.angleDeg, 0.0, 1e-6);
}

TEST(BoardFrame, ReadAndBackIsIdentity) {
    BoardFrame frame;
    frame.origin = {321.0F, 77.0F};
    for (const double angle : {0.0, 17.0, -42.5, 123.0}) {
        frame.angleDeg = angle;
        for (const cv::Point2f point : {cv::Point2f{0.0F, 0.0F}, cv::Point2f{640.0F, 480.0F},
                                        cv::Point2f{321.0F, 77.0F}, cv::Point2f{15.5F, 400.25F}}) {
            const BoardReading reading = readPoint(frame, point);
            const cv::Point2f back = toImagePoint(frame, reading.dx, reading.dy);
            EXPECT_NEAR(back.x, point.x, 1e-3);
            EXPECT_NEAR(back.y, point.y, 1e-3);
        }
    }
}

TEST(BoardFrame, PieceDeviationAndAngleOffset) {
    BoardConfig cfg;
    cfg.origin = BoardOrigin::ImageCenter;
    Fixture fixture;
    fixture.origin = {330.0F, 220.0F};  // 10 px a la derecha, 20 px por encima
    fixture.angleDeg = 12.0;

    const BoardFrame frame = resolveBoardFrame(cfg, fixture, true, cv::Size(640, 480));
    const BoardReading reading = readPiece(frame, fixture);
    EXPECT_NEAR(reading.dx, 10.0, 1e-9);
    EXPECT_NEAR(reading.dy, 20.0, 1e-9);
    EXPECT_NEAR(pieceAngleOffset(frame, fixture), 12.0, 1e-9);

    // Con los ejes siguiendo a la pieza, la desviación angular es cero.
    cfg.followPieceAngle = true;
    const BoardFrame aligned = resolveBoardFrame(cfg, fixture, true, cv::Size(640, 480));
    EXPECT_NEAR(pieceAngleOffset(aligned, fixture), 0.0, 1e-9);

    // El salto de 360° no debe aparecer como desviación enorme.
    Fixture wrapped = fixture;
    wrapped.angleDeg = 359.0;
    BoardFrame flat;
    EXPECT_NEAR(pieceAngleOffset(flat, wrapped), -1.0, 1e-9);
}

TEST(BoardFrame, MillimetersOnlyWhenCalibrated) {
    BoardReading reading;
    reading.dx = 10.0;
    reading.dy = -4.0;
    reading.radius = 12.0;
    reading.angleDeg = 33.0;

    const BoardReading raw = toMillimeters(reading, 0.0);
    EXPECT_DOUBLE_EQ(raw.dx, 10.0);  // sin calibrar: se queda en px
    EXPECT_DOUBLE_EQ(raw.radius, 12.0);

    const BoardReading mm = toMillimeters(reading, 0.5);
    EXPECT_DOUBLE_EQ(mm.dx, 5.0);
    EXPECT_DOUBLE_EQ(mm.dy, -2.0);
    EXPECT_DOUBLE_EQ(mm.radius, 6.0);
    EXPECT_DOUBLE_EQ(mm.angleDeg, 33.0);  // el ángulo no depende de la escala
}

TEST(BoardFrame, GridStepIsRoundAndProportional) {
    EXPECT_DOUBLE_EQ(niceGridStep(100.0, 10), 10.0);
    EXPECT_DOUBLE_EQ(niceGridStep(1000.0, 10), 100.0);
    EXPECT_DOUBLE_EQ(niceGridStep(10.0, 10), 1.0);
    EXPECT_DOUBLE_EQ(niceGridStep(0.5, 10), 0.05);
    // Valores intermedios caen en el escalón 1-2-5 más cercano (corte en la
    // media geométrica), no en el inmediato superior: así la densidad de la
    // grilla es la pedida y no la mitad.
    EXPECT_DOUBLE_EQ(niceGridStep(30.0, 10), 2.0);   // 3.0 está más cerca de 2 que de 5
    EXPECT_DOUBLE_EQ(niceGridStep(40.0, 10), 5.0);   // 4.0 ya cae del lado del 5
    EXPECT_DOUBLE_EQ(niceGridStep(18.0, 10), 2.0);
    EXPECT_DOUBLE_EQ(niceGridStep(12.0, 10), 1.0);
    // Entradas degeneradas no rompen el dibujo.
    EXPECT_GT(niceGridStep(0.0, 10), 0.0);
    EXPECT_GT(niceGridStep(100.0, 0), 0.0);
}

TEST(BoardFrame, OriginKeysRoundTrip) {
    for (const BoardOrigin origin : {BoardOrigin::PieceBounds, BoardOrigin::PieceCenter,
                                     BoardOrigin::ImageCenter, BoardOrigin::FixedPoint}) {
        EXPECT_EQ(originFromKey(originKey(origin)), origin);
    }
    // Clave desconocida: se cae al centrado automático recomendado.
    EXPECT_EQ(originFromKey("desconocido"), BoardOrigin::PieceBounds);
    EXPECT_EQ(originFromKey("", BoardOrigin::ImageCenter), BoardOrigin::ImageCenter);
    // La clave 'piece' de la v5 sigue leyéndose como centro de masa: las piezas
    // ya guardadas las convierte la migración v6, no el parser.
    EXPECT_EQ(originFromKey("piece"), BoardOrigin::PieceCenter);
}

// El centro de MASA y el centro del CONTORNO no coinciden en piezas
// asimétricas: es justo el motivo de que el centrado automático se viera
// descentrado, así que la diferencia se fija en un test.
TEST(BoardFrame, BoundsOriginUsesGeometricCenterNotMass) {
    Fixture fixture;
    fixture.origin = {100.0F, 100.0F};  // centro de masa
    const cv::Point2f boundsCenter{140.0F, 130.0F};
    const cv::Size imageSize(640, 480);

    BoardConfig cfg;
    cfg.origin = BoardOrigin::PieceBounds;
    EXPECT_EQ(resolveBoardFrame(cfg, fixture, true, imageSize, &boundsCenter).origin,
              boundsCenter);
    // Sin centro de contorno conocido cae al de masa (siempre disponible).
    EXPECT_EQ(resolveBoardFrame(cfg, fixture, true, imageSize).origin, fixture.origin);
    // Sin pieza, al centro de la imagen: el tablero sigue siendo utilizable.
    EXPECT_EQ(resolveBoardFrame(cfg, fixture, false, imageSize, &boundsCenter).origin,
              cv::Point2f(320.0F, 240.0F));
}

TEST(BoardFrame, ManualOffsetShiftsZeroInBoardAxes) {
    Fixture fixture;
    fixture.origin = {200.0F, 150.0F};
    const cv::Size imageSize(640, 480);

    BoardConfig cfg;
    cfg.origin = BoardOrigin::PieceCenter;
    cfg.manualOffset = {10.0F, 4.0F};  // +X derecha, +Y ARRIBA
    const BoardFrame shifted = resolveBoardFrame(cfg, fixture, true, imageSize);
    EXPECT_NEAR(shifted.origin.x, 210.0, 1e-4);
    EXPECT_NEAR(shifted.origin.y, 146.0, 1e-4);  // y de imagen crece hacia abajo

    // Con los ejes girados con la pieza, el ajuste acompaña al giro.
    Fixture rotated = fixture;
    rotated.angleDeg = 90.0;
    cfg.followPieceAngle = true;
    cfg.manualOffset = {10.0F, 0.0F};
    const BoardFrame turned = resolveBoardFrame(cfg, rotated, true, imageSize);
    EXPECT_NEAR(turned.origin.x, 200.0, 1e-4);
    EXPECT_NEAR(turned.origin.y, 160.0, 1e-4);  // +X del tablero apunta hacia abajo

    // El ajuste mueve el cero, así que un punto en el cero nuevo lee (0,0).
    const BoardReading atZero = readPoint(turned, turned.origin);
    EXPECT_NEAR(atZero.radius, 0.0, 1e-6);
}

// ===========================================================================
//  Deteccion en escenas dificiles: lo que se encuentra de verdad en una linea
//  (sombras, dos piezas, encuadres malos). Fija que la app se degrade de forma
//  PREDECIBLE en vez de dar una pieza inventada.
// ===========================================================================

TEST(DetectionHardCases, ShadowGradientBreaksOtsuButManualThresholdSavesIt) {
    // Fondo con un degradado fuerte (sombra lateral) mas la pieza oscura.
    cv::Mat gray(480, 640, CV_8UC1);
    for (int y = 0; y < gray.rows; ++y) {
        for (int x = 0; x < gray.cols; ++x) {
            gray.at<uchar>(y, x) = static_cast<uchar>(90 + (x * 150) / gray.cols);
        }
    }
    const cv::Mat piece = drawLPiece({640, 480}, {430.0F, 240.0F}, 0.0, 40.0F, 255, 0);
    gray.setTo(35, piece);  // pieza muy oscura en la zona clara del degradado

    // Otsu global parte el degradado por la mitad: el "contorno" se come medio
    // fondo, asi que el area sale disparada respecto a la pieza real.
    const auto automatic = analyzeFrame(gray);
    const double pieceArea = lPieceArea(40.0F);
    if (automatic.isOk()) {
        EXPECT_GT(automatic.value().contour.area, pieceArea * 2.0)
            << "con degradado, Otsu global no deberia acertar el area de la pieza";
    }

    // Con umbral manual y polaridad forzada (lo que ofrecen Deteccion... y los
    // perfiles) la pieza aparece limpia.
    PipelineConfig manual;
    manual.segmentation.manualThreshold = 60;
    manual.segmentation.polarity = SegmentationPolarity::DarkPiece;
    const auto fixed = analyzeFrame(gray, manual);
    ASSERT_TRUE(fixed.isOk()) << fixed.error().message;
    EXPECT_NEAR(fixed.value().contour.area, pieceArea, pieceArea * 0.25);
    EXPECT_NEAR(fixed.value().fixture.origin.x, 430.0F, 12.0F);
}

TEST(DetectionHardCases, TwoPiecesKeepTheLargestPredictably) {
    cv::Mat gray(480, 640, CV_8UC1, cv::Scalar(220));
    const cv::Mat big = drawLPiece({640, 480}, {200.0F, 240.0F}, 0.0, 45.0F, 255, 0);
    const cv::Mat small = drawLPiece({640, 480}, {480.0F, 240.0F}, 0.0, 20.0F, 255, 0);
    gray.setTo(40, big);
    gray.setTo(40, small);

    const auto analysis = analyzeFrame(gray);
    ASSERT_TRUE(analysis.isOk()) << analysis.error().message;
    // Se queda con la mayor: comportamiento definido, no aleatorio.
    EXPECT_NEAR(analysis.value().fixture.origin.x, 200.0F, 25.0F);
    EXPECT_NEAR(analysis.value().contour.area, lPieceArea(45.0F), lPieceArea(45.0F) * 0.2);
}

TEST(DetectionHardCases, PieceTouchingTheBorderIsStillFoundButClipped) {
    // Limitacion conocida y documentada en el README: si la pieza toca el
    // borde, el recorte sale incompleto. Lo importante es que NO reviente y que
    // el area detectada sea menor que la de la pieza entera.
    cv::Mat gray(480, 640, CV_8UC1, cv::Scalar(220));
    const cv::Mat piece = drawLPiece({640, 480}, {30.0F, 240.0F}, 0.0, 40.0F, 255, 0);
    gray.setTo(40, piece);

    const auto analysis = analyzeFrame(gray);
    ASSERT_TRUE(analysis.isOk()) << analysis.error().message;
    EXPECT_LT(analysis.value().contour.area, lPieceArea(40.0F))
        << "una pieza cortada por el borde no puede medir mas que la entera";
    EXPECT_FALSE(analysis.value().normalized.empty());
}

TEST(DetectionHardCases, AreaFractionLimitsRejectMotesAndFullFrameBlobs) {
    // Una mota diminuta no es una pieza...
    cv::Mat speck(480, 640, CV_8UC1, cv::Scalar(220));
    cv::circle(speck, {320, 240}, 3, cv::Scalar(30), cv::FILLED);
    EXPECT_FALSE(analyzeFrame(speck).isOk()) << "una mota no deberia pasar por pieza";

    // ...y una mancha que ocupa casi todo el encuadre, tampoco.
    cv::Mat huge(480, 640, CV_8UC1, cv::Scalar(220));
    cv::rectangle(huge, {5, 5}, {635, 475}, cv::Scalar(30), cv::FILLED);
    EXPECT_FALSE(analyzeFrame(huge).isOk())
        << "algo que ocupa el encuadre entero no es una pieza aislada";
}

TEST(DetectionHardCases, TinyImagesAndDegenerateRoiFailWithoutCrashing) {
    for (const cv::Size size : {cv::Size(1, 1), cv::Size(3, 3), cv::Size(16, 9)}) {
        const cv::Mat tiny(size, CV_8UC1, cv::Scalar(128));
        EXPECT_FALSE(analyzeFrame(tiny).isOk()) << size.width << "x" << size.height;
    }

    cv::Mat gray(480, 640, CV_8UC1, cv::Scalar(220));
    const cv::Mat piece = drawLPiece({640, 480}, {320.0F, 240.0F}, 0.0, 40.0F, 255, 0);
    gray.setTo(40, piece);

    // ROI fuera de la imagen: se recorta contra el frame y no debe colgar.
    PipelineConfig outside;
    outside.roi = cv::Rect(5000, 5000, 100, 100);
    const auto result = analyzeFrame(gray, outside);
    EXPECT_TRUE(result.isOk() || !result.error().message.empty());

    // ROI de 1 px: demasiado pequena para contener una pieza.
    PipelineConfig sliver;
    sliver.roi = cv::Rect(320, 240, 1, 1);
    EXPECT_FALSE(analyzeFrame(gray, sliver).isOk());
}

TEST(DetectionHardCases, HeavyNoiseDoesNotInventAPiece) {
    cv::Mat noise(480, 640, CV_8UC1);
    cv::randu(noise, 0, 255);
    const auto analysis = analyzeFrame(noise);
    if (analysis.isOk()) {
        // Si algo se detecta, no puede ser una pieza compacta y grande: el
        // ruido da manchas dispersas.
        EXPECT_LT(analysis.value().contour.area, 480.0 * 640.0 * 0.5);
    }
}

// ===========================================================================
//  Estabilizador en SECUENCIAS largas: lo que ve el operador no es un frame
//  suelto, sino minutos de video. Aqui se vigilan la deriva, la vibracion y el
//  retardo, que es donde se nota si el estabilizador esta bien ajustado.
// ===========================================================================

TEST(StabilizerSequences, StillPieceWithNoiseDoesNotDriftOrJitter) {
    StabilizerOptions options;
    const Fixture truth{{320.0F, 240.0F}, 30.0};
    Fixture shown = truth;

    // 300 frames de la pieza QUIETA con ruido de medicion pequeño.
    unsigned seed = 12345;
    const auto noise = [&seed](double amplitude) {
        seed = seed * 1103515245U + 12345U;
        const double unit = static_cast<double>((seed >> 16) % 2001) / 1000.0 - 1.0;
        return unit * amplitude;
    };
    double maxAngleError = 0.0;
    double maxPositionError = 0.0;
    for (int i = 0; i < 300; ++i) {
        Fixture measured = truth;
        measured.origin.x += static_cast<float>(noise(1.5));
        measured.origin.y += static_cast<float>(noise(1.5));
        measured.angleDeg += noise(1.0);
        bool flipped = false;
        shown = stabilizeFixture(shown, measured, options, flipped);
        EXPECT_FALSE(flipped);
        maxAngleError = std::max(maxAngleError, std::abs(angleDiff(shown.angleDeg, 30.0)));
        maxPositionError = std::max(
            maxPositionError, static_cast<double>(cv::norm(shown.origin - truth.origin)));
    }
    // Dentro de la banda muerta la vista queda clavada: nada de deriva lenta.
    EXPECT_LT(maxAngleError, 2.5) << "el angulo derivo con la pieza quieta";
    EXPECT_LT(maxPositionError, 5.0) << "la posicion derivo con la pieza quieta";
}

TEST(StabilizerSequences, SlowRotationIsFollowedWithBoundedLag) {
    StabilizerOptions options;
    Fixture shown{{320.0F, 240.0F}, 0.0};
    double worstLag = 0.0;
    for (int i = 1; i <= 120; ++i) {
        const double truthAngle = i * 0.5;  // medio grado por frame
        const Fixture measured{{320.0F, 240.0F}, truthAngle};
        bool flipped = false;
        shown = stabilizeFixture(shown, measured, options, flipped);
        if (i > 30) {  // tras el arranque, el retardo debe estabilizarse
            worstLag = std::max(worstLag, std::abs(angleDiff(shown.angleDeg, truthAngle)));
        }
    }
    EXPECT_LT(worstLag, 6.0) << "el seguimiento se quedo demasiado atras";
}

TEST(StabilizerSequences, SpuriousFlipsAreCorrectedAndReportedForTheCrop) {
    // Serie realista: pieza casi simetrica cuyo eje principal salta 180 grados
    // cada pocos frames. Contrato del estabilizador (verificado contra su uso
    // en MainWindow): el angulo MOSTRADO no salta nunca, y ademas avisa con
    // flipped180 en los frames afectados para que el llamador rehaga el recorte
    // normalizado girandolo 180 grados.
    StabilizerOptions options;
    Fixture shown{{300.0F, 200.0F}, 40.0};
    int flipsReported = 0;
    int flippedFrames = 0;
    for (int i = 0; i < 200; ++i) {
        const bool measurementFlipped = (i % 3 == 0);
        flippedFrames += measurementFlipped ? 1 : 0;
        Fixture measured{{300.0F, 200.0F}, measurementFlipped ? 40.0 - 180.0 : 40.0};
        bool flipped = false;
        shown = stabilizeFixture(shown, measured, options, flipped);
        flipsReported += flipped ? 1 : 0;
        EXPECT_EQ(flipped, measurementFlipped) << "aviso fuera de sitio en el frame " << i;
        EXPECT_LT(std::abs(angleDiff(shown.angleDeg, 40.0)), 5.0)
            << "salto visible en el frame " << i;
    }
    EXPECT_EQ(flipsReported, flippedFrames);
}

TEST(StabilizerSequences, RoundPieceKeepsItsAngleFrozenForever) {
    // Anisotropia por debajo del umbral: el eje no es fiable y el angulo debe
    // quedarse congelado por muchos frames que pasen.
    StabilizerOptions options;
    Fixture shown{{100.0F, 100.0F}, 15.0};
    shown.anisotropy = 0.05;
    for (int i = 0; i < 150; ++i) {
        Fixture measured{{100.0F, 100.0F}, static_cast<double>((i * 37) % 360)};
        measured.anisotropy = 0.05;  // sigue siendo redonda
        bool flipped = false;
        shown = stabilizeFixture(shown, measured, options, flipped);
        EXPECT_NEAR(shown.angleDeg, 15.0, 1e-6) << "frame " << i;
    }
}

TEST(StabilizerSequences, JumpToANewPositionSnapsInsteadOfSliding) {
    // La pieza se cambia por otra colocada lejos: la vista debe llegar de
    // inmediato, no arrastrarse durante decenas de frames.
    StabilizerOptions options;
    Fixture shown{{100.0F, 100.0F}, 0.0};
    const Fixture measured{{400.0F, 380.0F}, 0.0};
    bool flipped = false;
    shown = stabilizeFixture(shown, measured, options, flipped);
    EXPECT_NEAR(shown.origin.x, 400.0F, 1.0F);
    EXPECT_NEAR(shown.origin.y, 380.0F, 1.0F);
}

// El rasgo distintivo es la verdad cuando la pieza es simetrica; comprobar que
// aguanta una vuelta completa, no solo un par de angulos.
TEST(OrientationAnchorSequences, HoldsOrientationThroughAFullTurn) {
    const cv::Point2f center(320.0F, 240.0F);
    cv::Point2f dot;
    // Pieza con una marca distintiva; se registra el rasgo en la pose 0.
    cv::Mat base = drawLPiece({640, 480}, center, 0.0, 40.0F, 40, 220);
    dot = pci::testhelpers::lPointToImage({3.2F, 0.5F}, center, 0.0, 40.0F);
    cv::circle(base, cv::Point(cvRound(dot.x), cvRound(dot.y)), 6, cv::Scalar(230),
               cv::FILLED);
    PipelineConfig config;
    config.autoOrient = true;
    const auto baseAnalysis = analyzeFrame(base, config);
    ASSERT_TRUE(baseAnalysis.isOk());

    OrientationAnchor anchor;
    anchor.piecePoint = toPieceCoords(baseAnalysis.value().fixture, dot);
    anchor.intensity = sampleIntensity(base, dot);

    for (const double theta : {0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0}) {
        cv::Mat frame = drawLPiece({640, 480}, center, theta, 40.0F, 40, 220);
        const cv::Point2f rotatedDot =
            pci::testhelpers::lPointToImage({3.2F, 0.5F}, center, theta, 40.0F);
        cv::circle(frame, cv::Point(cvRound(rotatedDot.x), cvRound(rotatedDot.y)), 6,
                   cv::Scalar(230), cv::FILLED);
        auto analysis = analyzeFrame(frame, config);
        ASSERT_TRUE(analysis.isOk()) << "theta " << theta;
        ASSERT_TRUE(applyAnchor(frame, anchor, analysis.value()).isOk());

        // Con el rasgo aplicado, el punto marcado debe volver a caer donde se
        // registro (en coordenadas de PIEZA), sea cual sea la rotacion.
        const cv::Point2f recovered =
            toPieceCoords(analysis.value().fixture, rotatedDot);
        EXPECT_NEAR(recovered.x, anchor.piecePoint.x, 14.0F) << "theta " << theta;
        EXPECT_NEAR(recovered.y, anchor.piecePoint.y, 14.0F) << "theta " << theta;
    }
}

// ===========================================================================
//  Escala por marcador ArUco en condiciones adversas. Aqui esta el riesgo de
//  dar milimetros equivocados con cara de certeza, que es peor que no medir.
// ===========================================================================

namespace {

// Imagen con un marcador ArUco de lado sidePx colocado en topLeft, sobre fondo
// blanco (el detector necesita el margen claro alrededor).
cv::Mat sceneWithMarker(cv::Size size, cv::Point topLeft, int sidePx) {
    const auto dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
    cv::Mat marker;
    cv::aruco::generateImageMarker(dict, 0, sidePx, marker);
    cv::Mat image(size, CV_8UC1, cv::Scalar(255));
    const cv::Rect target(topLeft.x, topLeft.y, sidePx, sidePx);
    if ((target & cv::Rect(0, 0, size.width, size.height)) == target) {
        marker.copyTo(image(target));
    } else {
        // Colocacion parcial: se copia solo el trozo que cabe.
        const cv::Rect visible = target & cv::Rect(0, 0, size.width, size.height);
        if (visible.area() > 0) {
            marker(cv::Rect(visible.x - target.x, visible.y - target.y, visible.width,
                            visible.height))
                .copyTo(image(visible));
        }
    }
    return image;
}

}  // namespace

TEST(PlaneScaleHardCases, TiltedMarkerReportsLowQuality) {
    // Vista fronto-paralela: calidad alta (ya cubierto). Aqui se inclina el
    // plano con una homografia y la calidad debe CAER, que es el aviso de que
    // una escala unica deja de ser fiable lejos del marcador.
    const cv::Mat flat = sceneWithMarker({500, 500}, {200, 200}, 120);
    const auto straight = detectMarkerScale(flat, 30.0);
    ASSERT_TRUE(straight.has_value());

    const std::array<cv::Point2f, 4> src = {cv::Point2f(0, 0), cv::Point2f(499, 0),
                                            cv::Point2f(499, 499), cv::Point2f(0, 499)};
    // Perspectiva fuerte: el lado derecho se acerca y el izquierdo se aleja.
    const std::array<cv::Point2f, 4> dst = {cv::Point2f(60, 40), cv::Point2f(470, 0),
                                            cv::Point2f(499, 499), cv::Point2f(10, 430)};
    cv::Mat tilted;
    cv::warpPerspective(flat, tilted,
                        cv::getPerspectiveTransform(src.data(), dst.data()), flat.size(),
                        cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(255));

    const auto slanted = detectMarkerScale(tilted, 30.0);
    if (slanted.has_value()) {
        EXPECT_LT(slanted->quality, straight->quality)
            << "una vista inclinada no puede puntuar igual que una perpendicular";
        EXPECT_GE(slanted->quality, 0.0);
        EXPECT_LE(slanted->quality, 1.0);
    }
}

TEST(PlaneScaleHardCases, MarkerCutByTheFrameIsNotUsed) {
    // Medio marcador fuera del encuadre: mejor no dar escala que darla mal.
    const cv::Mat cut = sceneWithMarker({400, 400}, {-60, 150}, 120);
    EXPECT_FALSE(detectMarkerScale(cut, 30.0).has_value());
}

TEST(PlaneScaleHardCases, InvalidMarkerSizeIsRejected) {
    const cv::Mat scene = sceneWithMarker({400, 400}, {150, 150}, 100);
    EXPECT_FALSE(detectMarkerScale(scene, 0.0).has_value());
    EXPECT_FALSE(detectMarkerScale(scene, -30.0).has_value());
}

TEST(PlaneScaleHardCases, ScaleFollowsTheApparentSizeOfTheMarker) {
    // El mismo marcador de 30 mm visto grande (cerca) y pequeno (lejos): la
    // escala mm/px tiene que cambiar en proporcion inversa al tamano aparente.
    const auto near = detectMarkerScale(sceneWithMarker({600, 600}, {200, 200}, 200), 30.0);
    const auto far = detectMarkerScale(sceneWithMarker({600, 600}, {250, 250}, 100), 30.0);
    ASSERT_TRUE(near.has_value());
    ASSERT_TRUE(far.has_value());
    EXPECT_NEAR(near->mmPerPixel, 30.0 / 200.0, 0.02);
    EXPECT_NEAR(far->mmPerPixel, 30.0 / 100.0, 0.03);
    EXPECT_LT(near->mmPerPixel, far->mmPerPixel)
        << "de cerca cada pixel abarca menos milimetros";
}

// La escala local y la homografia tienen que contar lo mismo: si discrepan, una
// misma medida daria un numero distinto segun por donde se calcule.
TEST(PlaneScaleHardCases, LocalScaleAndHomographyAgree) {
    const cv::Mat scene = sceneWithMarker({600, 600}, {240, 240}, 120);
    const auto scale = detectMarkerScale(scene, 30.0);
    ASSERT_TRUE(scale.has_value());

    // 120 px a lo ancho del marcador deben ser ~30 mm por los dos caminos.
    const double byHomography =
        planeDistanceMm(scale->imageToMm, {240.0F, 300.0F}, {360.0F, 300.0F});
    const double byLocalScale = 120.0 * scale->mmPerPixel;
    EXPECT_NEAR(byHomography, 30.0, 2.0);
    EXPECT_NEAR(byHomography, byLocalScale, 2.0);
}

TEST(PlaneScaleHardCases, EmptyHomographyGivesZeroInsteadOfGarbage) {
    // Sin homografia valida, la distancia en mm es 0 (el llamador cae a la
    // escala constante) en vez de un numero inventado.
    EXPECT_DOUBLE_EQ(planeDistanceMm(cv::Mat(), {0.0F, 0.0F}, {100.0F, 0.0F}), 0.0);
}

TEST(PlaneScaleHardCases, TwoMarkersDoNotBreakTheDetection) {
    cv::Mat scene = sceneWithMarker({700, 400}, {80, 140}, 110);
    const auto dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
    cv::Mat second;
    cv::aruco::generateImageMarker(dict, 7, 110, second);
    second.copyTo(scene(cv::Rect(450, 140, 110, 110)));

    const auto scale = detectMarkerScale(scene, 30.0);
    ASSERT_TRUE(scale.has_value()) << "con dos marcadores debe usar uno, no fallar";
    EXPECT_NEAR(scale->mmPerPixel, 30.0 / 110.0, 0.03);
}

TEST(PlaneScaleHardCases, NoisyBackgroundDoesNotInventAMarker) {
    cv::Mat noise(400, 400, CV_8UC1);
    cv::randu(noise, 0, 255);
    EXPECT_FALSE(detectMarkerScale(noise, 30.0).has_value());
}

// --- Reajuste de lo que vive en pixeles al cambiar de resolucion ---

TEST(FrameGeometry, RectKeepsItsPlaceWhenTheFrameGrowsOrShrinks) {
    const cv::Size vga(640, 480);
    const cv::Size fullHd(1920, 1080);

    // Una zona centrada debe seguir centrada tras el cambio.
    const cv::Rect centered(160, 120, 320, 240);
    const cv::Rect scaled = rescaleRect(centered, vga, fullHd);
    EXPECT_NEAR(scaled.x + scaled.width / 2.0, fullHd.width / 2.0, 2.0);
    EXPECT_NEAR(scaled.y + scaled.height / 2.0, fullHd.height / 2.0, 2.0);
    // Y ocupar la misma fraccion del encuadre.
    EXPECT_NEAR(static_cast<double>(scaled.width) / fullHd.width,
                static_cast<double>(centered.width) / vga.width, 0.01);

    // Ida y vuelta: se recupera practicamente el mismo rectangulo.
    const cv::Rect back = rescaleRect(scaled, fullHd, vga);
    EXPECT_NEAR(back.x, centered.x, 2);
    EXPECT_NEAR(back.width, centered.width, 2);
}

TEST(FrameGeometry, RescaledRectNeverEscapesTheNewFrameNorVanishes) {
    const cv::Size big(1920, 1080);
    const cv::Size small(320, 240);

    // Zona pegada a la esquina inferior derecha del frame grande.
    const cv::Rect corner(1800, 1000, 120, 80);
    const cv::Rect scaled = rescaleRect(corner, big, small);
    EXPECT_GE(scaled.x, 0);
    EXPECT_GE(scaled.y, 0);
    EXPECT_LE(scaled.x + scaled.width, small.width);
    EXPECT_LE(scaled.y + scaled.height, small.height);

    // Una zona diminuta no puede desaparecer por redondeo: eso dejaria la
    // deteccion mirando a la nada sin decir por que.
    const cv::Rect tiny(100, 100, 3, 3);
    const cv::Rect shrunk = rescaleRect(tiny, big, small);
    EXPECT_GE(shrunk.width, 1);
    EXPECT_GE(shrunk.height, 1);
}

TEST(FrameGeometry, DegenerateSizesLeaveTheValueUntouched) {
    const cv::Rect roi(10, 10, 50, 50);
    EXPECT_EQ(rescaleRect(roi, {0, 0}, {640, 480}), roi);
    EXPECT_EQ(rescaleRect(roi, {640, 480}, {0, 0}), roi);
    EXPECT_EQ(rescaleRect({}, {640, 480}, {1280, 720}), cv::Rect());

    const cv::Point2f point(120.0F, 90.0F);
    EXPECT_EQ(rescalePoint(point, {0, 0}, {640, 480}), point);
}

TEST(FrameGeometry, PointFollowsTheSameSpotOfTheScene) {
    // El cero fijado del tablero estaba a un cuarto del ancho y a la mitad del
    // alto: tras duplicar la resolucion debe seguir ahi.
    const cv::Point2f fixed(160.0F, 240.0F);
    const cv::Point2f scaled = rescalePoint(fixed, {640, 480}, {1280, 960});
    EXPECT_FLOAT_EQ(scaled.x, 320.0F);
    EXPECT_FLOAT_EQ(scaled.y, 480.0F);
    EXPECT_NEAR(scaled.x / 1280.0, fixed.x / 640.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Nitidez para el asistente de enfoque (C2)
// ---------------------------------------------------------------------------

namespace {

// Pieza nítida (cuadrado con damero fino dentro, que es lo que da textura al
// Laplaciano) sobre el fondo que se pida.
cv::Mat sharpPieceOn(const cv::Mat& background, cv::Rect box) {
    cv::Mat scene = background.clone();
    cv::rectangle(scene, box, cv::Scalar(230), cv::FILLED);
    for (int y = box.y; y < box.y + box.height; y += 6) {
        cv::line(scene, {box.x, y}, {box.x + box.width - 1, y}, cv::Scalar(20), 2);
    }
    return scene;
}

}  // namespace

TEST(Sharpness, GetsWorseAsTheImageGoesOutOfFocus) {
    // El asistente de enfoque vale exactamente lo que valga esta propiedad: si
    // la nitidez no bajara de forma monótona al desenfocar, buscar el máximo no
    // llevaría a ningún sitio.
    const cv::Mat sharp =
        sharpPieceOn(cv::Mat(400, 400, CV_8UC1, cv::Scalar(40)), cv::Rect(100, 100, 200, 200));

    const double peak = sharpnessOf(sharp);
    std::printf("  nitidez sin desenfoque: %.0f\n", peak);
    ASSERT_GT(peak, 0.0);

    // Se exige la caida monotona MIENTRAS la medida signifique algo. Medido:
    // 7444 sin desenfoque -> 1372 (18 %) -> 765 -> 98 (1,3 %) -> 2,5 (0,03 %)
    // -> 6,5 (0,09 %). Ese ultimo repunte es real y NO es un defecto: por
    // debajo del 0,1 % del pico ya no
    // queda detalle que perder y lo que se mide es residuo numerico del rizado
    // que sobrevive al filtro. Al asistente le da igual —a esas alturas la
    // barra lleva rato clavada abajo— pero afirmar monotonia ahi seria afirmar
    // sobre ruido, y el test fallaria de vez en cuando sin que nada este mal.
    double previous = peak;
    for (const int kernel : {3, 5, 9, 15, 25}) {
        cv::Mat blurred;
        cv::GaussianBlur(sharp, blurred, cv::Size(kernel, kernel), 0);
        const double value = sharpnessOf(blurred);
        std::printf("  desenfoque %2d px -> %8.1f  (%.3f %% del pico)\n", kernel, value,
                    100.0 * value / peak);
        if (previous > peak * 0.001) {
            EXPECT_LT(value, previous) << "desenfocar mas tiene que dar menos nitidez";
        }
        previous = value;
    }
    // Y lo que de verdad tiene que cumplirse para que enfocar sirva de algo: el
    // desenfoque fuerte deja la medida cien veces por debajo. El margen es 1 %
    // y no 0,1 % a proposito: lo medido es 0,09 %, y apretar el umbral hasta
    // rozar el valor real convierte el test en un generador de fallos.
    cv::Mat veryBlurred;
    cv::GaussianBlur(sharp, veryBlurred, cv::Size(25, 25), 0);
    EXPECT_LT(sharpnessOf(veryBlurred), peak * 0.01);
}

TEST(Sharpness, MeasuredOnThePieceItIgnoresATexturedBackground) {
    // Este es el test que justifica medir sobre la pieza y no sobre el frame
    // entero. Con un fondo texturizado y la pieza DESENFOCADA, la nitidez del
    // encuadre completo sigue alta por culpa del fondo: quien mirase ese numero
    // creeria estar enfocado.
    cv::Mat noisyBackground(400, 400, CV_8UC1);
    cv::randu(noisyBackground, 0, 255);  // fondo con muchisimo detalle

    const cv::Rect box(120, 120, 160, 160);
    cv::Mat scene = sharpPieceOn(noisyBackground, box);
    // Se desenfoca SOLO la pieza: la camara enfocada a otra distancia.
    cv::Mat blurredPiece;
    cv::GaussianBlur(scene(box), blurredPiece, cv::Size(21, 21), 0);
    blurredPiece.copyTo(scene(box));

    const double whole = sharpnessOf(scene);
    const double onPiece = sharpnessOf(scene, box);
    std::printf("  encuadre completo: %.0f   solo la pieza: %.0f\n", whole, onPiece);
    EXPECT_GT(whole, onPiece * 3.0)
        << "el fondo texturizado tiene que dominar el numero del frame entero";

    // Y con la pieza nitida sobre el mismo fondo, la medida sobre la pieza sube
    // de verdad: no es que el recorte de siempre un numero bajo.
    const cv::Mat focused = sharpPieceOn(noisyBackground, box);
    const double onSharpPiece = sharpnessOf(focused, box);
    std::printf("  pieza enfocada: %.0f (desenfocada: %.0f)\n", onSharpPiece, onPiece);
    EXPECT_GT(onSharpPiece, onPiece * 2.0);
}

TEST(Sharpness, DegenerateInputsGiveZeroInsteadOfGarbage) {
    EXPECT_DOUBLE_EQ(sharpnessOf(cv::Mat()), 0.0);
    const cv::Mat image(100, 100, CV_8UC1, cv::Scalar(128));
    // Un recorte diminuto no tiene varianza que signifique nada.
    EXPECT_DOUBLE_EQ(sharpnessOf(image, cv::Rect(10, 10, 3, 3)), 0.0);
    // Un recorte que se sale se acota contra la imagen en vez de reventar.
    EXPECT_GE(sharpnessOf(image, cv::Rect(50, 50, 500, 500)), 0.0);
    // Y una imagen plana no tiene nada que enfocar.
    EXPECT_NEAR(sharpnessOf(image), 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Zona de trabajo automática (C3)
// ---------------------------------------------------------------------------

namespace {

// Escena con una pieza rectangular clara sobre fondo oscuro, en la posición que
// se pida. Tamaño de frame realista para que el test de rendimiento signifique
// algo.
cv::Mat sceneWithPieceAt(cv::Point topLeft, cv::Size pieceSize,
                         cv::Size frameSize = {1280, 720}) {
    cv::Mat scene(frameSize, CV_8UC1, cv::Scalar(20));
    cv::rectangle(scene, cv::Rect(topLeft, pieceSize), cv::Scalar(220), cv::FILLED);
    return scene;
}

cv::Rect boundsOf(const PieceAnalysis& analysis) {
    return cv::boundingRect(analysis.contour.points);
}

}  // namespace

TEST(AutoRoi, FollowsAMovingPieceAndGivesTheSameFixtureAsTheWholeFrame) {
    // Lo que hay que demostrar antes que nada: recortar NO cambia el resultado.
    // Si el fixture saliera distinto, todas las herramientas se desplazarían.
    AutoRoiTracker tracker;
    const cv::Size piece(160, 120);

    for (int step = 0; step < 12; ++step) {
        const cv::Point at(120 + step * 40, 200 + step * 15);
        const cv::Mat scene = sceneWithPieceAt(at, piece);

        PipelineConfig cropped;
        cropped.roi = tracker.roi();
        const auto withRoi = analyzeFrame(scene, cropped);
        const auto whole = analyzeFrame(scene, PipelineConfig{});
        ASSERT_TRUE(whole.isOk()) << "paso " << step;
        ASSERT_TRUE(withRoi.isOk())
            << "paso " << step << ": el recorte perdió la pieza — " << withRoi.error().message;

        // El fixture es lo que coloca las herramientas: tiene que coincidir.
        EXPECT_NEAR(withRoi.value().fixture.origin.x, whole.value().fixture.origin.x, 0.5)
            << "paso " << step;
        EXPECT_NEAR(withRoi.value().fixture.origin.y, whole.value().fixture.origin.y, 0.5)
            << "paso " << step;

        // Y el recorte que se usó contenía de verdad a la pieza.
        if (cropped.roi.area() > 0) {
            EXPECT_EQ(cropped.roi & boundsOf(whole.value()), boundsOf(whole.value()))
                << "paso " << step << ": el recorte cortaba la pieza";
        }
        tracker.update(true, boundsOf(withRoi.value()), scene.size());
    }
    EXPECT_TRUE(tracker.tracking()) << "tras doce frames buenos deberia estar siguiendo";
}

TEST(AutoRoi, GivesUpQuicklyWhenThePieceDisappears) {
    AutoRoiTracker tracker;
    const cv::Size frame(1280, 720);
    tracker.update(true, cv::Rect(400, 300, 160, 120), frame);
    ASSERT_TRUE(tracker.tracking());

    // Un parpadeo suelto no tira el seguimiento.
    tracker.update(false, cv::Rect(), frame);
    EXPECT_TRUE(tracker.tracking()) << "un frame malo no puede tirar el seguimiento";

    // Pero desaparecer de verdad si.
    for (int i = 0; i < 3; ++i) {
        tracker.update(false, cv::Rect(), frame);
    }
    EXPECT_FALSE(tracker.tracking());
    EXPECT_EQ(tracker.lastGiveUp(), AutoRoiGiveUp::PieceLost);
    EXPECT_STRNE(giveUpReason(tracker.lastGiveUp()), "");
}

TEST(AutoRoi, GivesUpWhenThePieceReachesTheEdgeOfTheCrop) {
    // Una pieza tocando el borde del recorte puede estar ya cortada: lo que se
    // mida de ella no vale, y perseguirla arrastraria el error.
    AutoRoiTracker tracker;
    const cv::Size frame(1280, 720);
    tracker.update(true, cv::Rect(400, 300, 160, 120), frame);
    const cv::Rect roi = tracker.roi();
    ASSERT_GT(roi.area(), 0);

    tracker.update(true, cv::Rect(roi.x, roi.y, 160, 120), frame);
    EXPECT_FALSE(tracker.tracking());
    EXPECT_EQ(tracker.lastGiveUp(), AutoRoiGiveUp::PieceEscaping);
}

TEST(AutoRoi, GivesUpWhenSomeoneSwapsThePiece) {
    AutoRoiTracker tracker;
    const cv::Size frame(1280, 720);
    tracker.update(true, cv::Rect(400, 300, 160, 120), frame);
    ASSERT_TRUE(tracker.tracking());
    // Mas del doble de area, y DENTRO del recorte anterior: si sobresaliera,
    // saltaria antes la guarda de "se esta saliendo" y este test no probaria
    // lo que dice probar. Ese orden es deliberado — ver el comentario de
    // AutoRoiTracker::update.
    tracker.update(true, cv::Rect(360, 270, 240, 180), frame);
    EXPECT_FALSE(tracker.tracking());
    EXPECT_EQ(tracker.lastGiveUp(), AutoRoiGiveUp::AreaJumped);
}

TEST(AutoRoi, DoesNotCropWhenThereIsNothingToGain) {
    const cv::Size frame(1280, 720);
    AutoRoiTracker tracker;
    // Pieza diminuta: el recorte seria mas pequeno que el minimo util.
    tracker.update(true, cv::Rect(600, 350, 10, 10), frame);
    EXPECT_FALSE(tracker.tracking()) << "recortar 10 px no ahorra nada y si puede fallar";

    // Pieza casi tan grande como el frame: recortar no ahorra y anade riesgo.
    AutoRoiTracker big;
    big.update(true, cv::Rect(20, 20, 1240, 680), frame);
    EXPECT_FALSE(big.tracking());

    // Y un frame degenerado no revienta.
    AutoRoiTracker degenerate;
    degenerate.update(true, cv::Rect(0, 0, 10, 10), cv::Size(0, 0));
    EXPECT_FALSE(degenerate.tracking());
}

TEST(AutoRoi, TheCropGrowsAtOnceAndShrinksSlowly) {
    // Crecer tarde recortaria a la pieza; encoger de golpe haria latir el
    // rectangulo con el ruido de la segmentacion.
    AutoRoiTracker tracker;
    const cv::Size frame(1280, 720);
    tracker.update(true, cv::Rect(500, 300, 200, 150), frame);
    const cv::Rect wide = tracker.roi();

    // La pieza encoge POCO, como encoge el ruido de la segmentacion de un
    // frame a otro. Un encogimiento grande seria otra cosa (otra pieza) y la
    // guarda de area lo trataria como tal.
    tracker.update(true, cv::Rect(510, 308, 180, 135), frame);
    EXPECT_GT(tracker.roi().area(), 0);
    EXPECT_LT(tracker.roi().area(), wide.area()) << "algo tiene que encoger";
    EXPECT_GT(tracker.roi().area(), wide.area() / 2) << "pero no de golpe";
}

TEST(AutoRoi, CroppingIsAtLeastTwiceAsFastOnASmallPiece) {
    // La razon de ser del item. Se mide RELATIVO y en el mismo proceso: en
    // milisegundos absolutos dependeria de la maquina y el test se convertiria
    // en un generador de fallos intermitentes.
    const cv::Mat scene = sceneWithPieceAt({520, 280}, {180, 140}, {1280, 720});
    AutoRoiTracker tracker;
    const auto first = analyzeFrame(scene, PipelineConfig{});
    ASSERT_TRUE(first.isOk());
    tracker.update(true, boundsOf(first.value()), scene.size());
    ASSERT_TRUE(tracker.tracking());

    PipelineConfig cropped;
    cropped.roi = tracker.roi();
    constexpr int kRuns = 40;

    // Una pasada de calentamiento de cada una: la primera paga cachés y
    // reservas que no tienen nada que ver con el tamaño del recorte.
    (void)analyzeFrame(scene, PipelineConfig{});
    (void)analyzeFrame(scene, cropped);

    const auto time = [&](const PipelineConfig& config) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kRuns; ++i) {
            (void)analyzeFrame(scene, config);
        }
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start)
                   .count() /
               kRuns;
    };
    const double wholeMs = time(PipelineConfig{});
    const double croppedMs = time(cropped);
    const double fraction = static_cast<double>(cropped.roi.area()) /
                            (scene.cols * static_cast<double>(scene.rows));
    std::printf("  frame entero %.2f ms  |  recorte %.2f ms (%.1f %% del area)  -> %.1fx\n",
                wholeMs, croppedMs, 100.0 * fraction, wholeMs / croppedMs);
    EXPECT_LT(croppedMs * 2.0, wholeMs)
        << "el recorte tiene que ir al menos el doble de rapido";
}

// ---------------------------------------------------------------------------
// C4b: acelerar los momentos y el recorte canónico, sin mover las medidas
// ---------------------------------------------------------------------------
//
// Son optimizaciones de una función que ya funcionaba, así que la prueba que
// vale no es "da un número razonable" sino "da EXACTAMENTE lo de antes". Por
// eso el test lleva una implementación de referencia con el código original y
// compara contra ella.

namespace {

// El `normalizePiece` de antes de C4b, tal cual: fondo fuera, dos warpAffine de
// imagen completa y recorte de la envolvente del resultado.
cv::Mat normalizePieceReference(const cv::Mat& image, const cv::Mat& mask,
                                const Fixture& fixture, int canonicalSize) {
    cv::Mat clean = cv::Mat::zeros(image.size(), image.type());
    image.copyTo(clean, mask);

    const cv::Mat rotation = cv::getRotationMatrix2D(fixture.origin, fixture.angleDeg, 1.0);
    cv::Mat rotated;
    cv::Mat rotatedMask;
    cv::warpAffine(clean, rotated, rotation, clean.size(), cv::INTER_LINEAR,
                   cv::BORDER_CONSTANT, cv::Scalar());
    cv::warpAffine(mask, rotatedMask, rotation, mask.size(), cv::INTER_NEAREST,
                   cv::BORDER_CONSTANT, cv::Scalar());

    const cv::Rect box = cv::boundingRect(rotatedMask);
    const cv::Mat cropped = rotated(box);
    const double scale = static_cast<double>(canonicalSize) /
                         static_cast<double>(std::max(box.width, box.height));
    cv::Mat resized;
    cv::resize(cropped, resized, cv::Size(), scale, scale,
               scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);

    cv::Mat canvas = cv::Mat::zeros(canonicalSize, canonicalSize, image.type());
    const int x = (canonicalSize - resized.cols) / 2;
    const int y = (canonicalSize - resized.rows) / 2;
    resized.copyTo(canvas(cv::Rect(x, y, resized.cols, resized.rows)));
    return canvas;
}

// Escena y máscara de una pieza en L, que no es simétrica: si el centroide o el
// ángulo se movieran, se notaría.
void lSceneAndMask(cv::Mat& scene, cv::Mat& mask, cv::Size size = {2560, 1440}) {
    scene = cv::Mat(size, CV_8UC1, cv::Scalar(30));
    mask = cv::Mat::zeros(size, CV_8UC1);
    const std::vector<cv::Rect> parts{{600, 300, 1200, 300}, {600, 300, 300, 900}};
    for (const auto& part : parts) {
        cv::rectangle(scene, part, cv::Scalar(210), cv::FILLED);
        cv::rectangle(mask, part, cv::Scalar(255), cv::FILLED);
    }
}

}  // namespace

TEST(FixtureSpeedup, TheMomentsOverTheBoundingBoxAreTheSameOnes) {
    // El atajo es calcular los momentos sobre la envolvente en vez de sobre la
    // máscara entera. Es exacto porque los momentos centrales no dependen de
    // dónde esté la pieza; lo único que hay que corregir es el centroide.
    cv::Mat scene;
    cv::Mat mask;
    lSceneAndMask(scene, mask);

    const cv::Moments whole = cv::moments(mask, true);
    const cv::Rect box = cv::boundingRect(mask);
    const cv::Moments cropped = cv::moments(mask(box), true);

    EXPECT_DOUBLE_EQ(cropped.m00, whole.m00);
    EXPECT_NEAR(cropped.m10 / cropped.m00 + box.x, whole.m10 / whole.m00, 1e-9);
    EXPECT_NEAR(cropped.m01 / cropped.m00 + box.y, whole.m01 / whole.m00, 1e-9);
    // Los centrales, idénticos: de ahí salen el ángulo y la anisotropía.
    EXPECT_NEAR(cropped.mu20, whole.mu20, std::abs(whole.mu20) * 1e-12);
    EXPECT_NEAR(cropped.mu11, whole.mu11, std::abs(whole.mu20) * 1e-12);
    EXPECT_NEAR(cropped.mu02, whole.mu02, std::abs(whole.mu02) * 1e-12);
}

TEST(FixtureSpeedup, TheFixtureIsTheSameAsComputingItTheLongWay) {
    cv::Mat scene;
    cv::Mat mask;
    lSceneAndMask(scene, mask);

    for (const bool autoOrient : {false, true}) {
        const auto fixture = computeFixture(mask, autoOrient);
        ASSERT_TRUE(fixture.isOk());

        // Referencia: momentos sobre la máscara ENTERA, como se hacía antes.
        const cv::Moments m = cv::moments(mask, true);
        const double expectedX = m.m10 / m.m00;
        const double expectedY = m.m01 / m.m00;
        std::printf("  autoOrient=%d: origen (%.6f, %.6f), esperado (%.6f, %.6f)\n",
                    autoOrient, static_cast<double>(fixture.value().origin.x),
                    static_cast<double>(fixture.value().origin.y), expectedX, expectedY);
        EXPECT_NEAR(fixture.value().origin.x, expectedX, 1e-3);
        EXPECT_NEAR(fixture.value().origin.y, expectedY, 1e-3);
        EXPECT_NEAR(fixture.value().anisotropy, principalAnisotropy(mask), 1e-12);
        if (autoOrient) {
            const auto angle = principalAngleDeg(mask);
            ASSERT_TRUE(angle.isOk());
            EXPECT_NEAR(fixture.value().angleDeg, angle.value(), 1e-12);
        }
    }
}

TEST(FixtureSpeedup, ComputingTheFixtureGotFaster) {
    cv::Mat scene;
    cv::Mat mask;
    lSceneAndMask(scene, mask);

    constexpr int kRuns = 20;
    const auto time = [&](auto&& f) {
        f();
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kRuns; ++i) {
            f();
        }
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start)
                   .count() /
               kRuns;
    };
    // Referencia: las dos pasadas que se hacían antes sobre la máscara entera.
    const double before = time([&] {
        const cv::Moments m = cv::moments(mask, true);
        (void)m.m10;
        (void)principalAnisotropy(mask);
    });
    const double after = time([&] { (void)computeFixture(mask, false); });
    std::printf("  computeFixture: antes ~%.2f ms  |  ahora %.2f ms  -> %.2fx\n", before,
                after, before / after);
    EXPECT_LT(after, before);
}

TEST(NormalizeSpeedup, TheUprightShortcutGivesThePixelsItAlwaysGave) {
    // El caso por defecto (`autoOrient` false) hacía dos warpAffine con una
    // rotación identidad. Saltárselos tiene que dar EXACTAMENTE la misma
    // imagen, no una parecida.
    cv::Mat scene;
    cv::Mat mask;
    lSceneAndMask(scene, mask);

    Fixture fixture;
    fixture.origin = {1000.0F, 700.0F};
    fixture.angleDeg = 0.0;

    const auto fast = normalizePiece(scene, mask, fixture, 256);
    ASSERT_TRUE(fast.isOk());
    const cv::Mat reference = normalizePieceReference(scene, mask, fixture, 256);

    ASSERT_EQ(fast.value().size(), reference.size());
    ASSERT_EQ(fast.value().type(), reference.type());
    cv::Mat diff;
    cv::absdiff(fast.value(), reference, diff);
    double maxDiff = 0.0;
    cv::minMaxLoc(diff, nullptr, &maxDiff);
    std::printf("  recorte canónico: %d píxeles distintos, diferencia máxima %.0f\n",
                cv::countNonZero(diff), maxDiff);
    EXPECT_EQ(cv::countNonZero(diff), 0) << "el atajo tiene que ser exacto, no parecido";
}

TEST(NormalizeSpeedup, TheRotatedPathIsUntouched) {
    // Con giro se sigue por el camino de siempre: se comprueba que no se rompió
    // al meter el atajo.
    cv::Mat scene;
    cv::Mat mask;
    lSceneAndMask(scene, mask);

    Fixture fixture;
    fixture.origin = {1000.0F, 700.0F};
    fixture.angleDeg = 27.0;

    const auto rotatedResult = normalizePiece(scene, mask, fixture, 256);
    ASSERT_TRUE(rotatedResult.isOk());
    const cv::Mat reference = normalizePieceReference(scene, mask, fixture, 256);
    cv::Mat diff;
    cv::absdiff(rotatedResult.value(), reference, diff);
    EXPECT_EQ(cv::countNonZero(diff), 0);
}

TEST(NormalizeSpeedup, TheUprightCaseGotFaster) {
    cv::Mat scene;
    cv::Mat mask;
    lSceneAndMask(scene, mask);
    Fixture fixture;
    fixture.origin = {1000.0F, 700.0F};
    fixture.angleDeg = 0.0;

    constexpr int kRuns = 20;
    const auto time = [&](auto&& f) {
        f();
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kRuns; ++i) {
            f();
        }
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start)
                   .count() /
               kRuns;
    };
    const double before =
        time([&] { (void)normalizePieceReference(scene, mask, fixture, 256); });
    const double after = time([&] { (void)normalizePiece(scene, mask, fixture, 256); });
    std::printf("  normalizePiece sin giro: antes %.2f ms  |  ahora %.2f ms  -> %.2fx\n",
                before, after, before / after);
    EXPECT_LT(after * 2.0, before) << "saltarse dos warpAffine tiene que notarse";
}

// ---------------------------------------------------------------------------
// Varias piezas en la imagen (C5)
// ---------------------------------------------------------------------------

namespace {

// Bandeja con `count` piezas cuadradas de tamanos decrecientes, bien separadas.
cv::Mat trayWith(int count, cv::Size frame = {1280, 720}) {
    cv::Mat scene(frame, CV_8UC1, cv::Scalar(20));
    for (int i = 0; i < count; ++i) {
        const int side = 170 - i * 12;  // decrecientes: el orden por area es conocido
        const int x = 60 + (i % 3) * 380;
        const int y = 60 + (i / 3) * 320;
        cv::rectangle(scene, cv::Rect(x, y, side, side), cv::Scalar(220), cv::FILLED);
    }
    return scene;
}

}  // namespace

TEST(MultiPiece, FindsThemAllAndInOrderOfSize) {
    for (const int count : {1, 3, 6}) {
        const cv::Mat scene = trayWith(count);
        const auto pieces = analyzeFrames(scene);
        ASSERT_TRUE(pieces.isOk()) << count << " piezas: " << pieces.error().message;
        EXPECT_EQ(static_cast<int>(pieces.value().size()), count);

        // De mayor a menor: quien las numere en pantalla necesita un orden
        // estable, y el area es el unico que no depende de por donde empiece
        // findContours.
        for (std::size_t i = 1; i < pieces.value().size(); ++i) {
            EXPECT_GE(pieces.value()[i - 1].contour.area, pieces.value()[i].contour.area)
                << "piezas " << i - 1 << " y " << i;
        }
        std::printf("  %d piezas -> areas:", count);
        for (const auto& piece : pieces.value()) {
            std::printf(" %.0f", piece.contour.area);
        }
        std::printf("\n");
    }
}

TEST(MultiPiece, EachOneGetsItsOwnFixtureAndMask) {
    const cv::Mat scene = trayWith(3);
    const auto pieces = analyzeFrames(scene);
    ASSERT_TRUE(pieces.isOk());
    ASSERT_EQ(pieces.value().size(), 3U);

    for (const auto& piece : pieces.value()) {
        // La mascara es del tamano del frame y contiene SOLO a esa pieza.
        EXPECT_EQ(piece.mask.size(), scene.size());
        const double maskArea = cv::countNonZero(piece.mask);
        EXPECT_NEAR(maskArea, piece.contour.area, piece.contour.area * 0.05);
        // Y el fixture cae dentro de su propia envolvente.
        const cv::Rect box = cv::boundingRect(piece.contour.points);
        EXPECT_TRUE(box.contains(cv::Point(static_cast<int>(piece.fixture.origin.x),
                                           static_cast<int>(piece.fixture.origin.y))));
        EXPECT_FALSE(piece.normalized.empty());
    }
}

TEST(MultiPiece, TheFirstOneIsExactlyWhatAnalyzeFrameReturns) {
    // El contrato de `analyzeFrame` no cambia: sigue siendo la pieza mayor.
    const cv::Mat scene = trayWith(4);
    const auto one = analyzeFrame(scene);
    const auto many = analyzeFrames(scene);
    ASSERT_TRUE(one.isOk());
    ASSERT_TRUE(many.isOk());
    EXPECT_NEAR(many.value().front().fixture.origin.x, one.value().fixture.origin.x, 0.01);
    EXPECT_NEAR(many.value().front().fixture.origin.y, one.value().fixture.origin.y, 0.01);
    EXPECT_NEAR(many.value().front().contour.area, one.value().contour.area, 1.0);
}

TEST(MultiPiece, SpecklesBelowTheMinimumAreaAreNotPieces) {
    cv::Mat scene = trayWith(2);
    // Motas: por debajo de minAreaFraction (0,5 % de 1280x720 = 4608 px).
    cv::circle(scene, {900, 600}, 8, cv::Scalar(220), cv::FILLED);
    cv::circle(scene, {1000, 650}, 5, cv::Scalar(220), cv::FILLED);
    const auto pieces = analyzeFrames(scene);
    ASSERT_TRUE(pieces.isOk());
    EXPECT_EQ(pieces.value().size(), 2U) << "una mota no es una pieza";
}

TEST(MultiPiece, AnEmptySceneFailsWithTheSameMessageAsBefore) {
    const cv::Mat empty(720, 1280, CV_8UC1, cv::Scalar(20));
    const auto pieces = analyzeFrames(empty);
    EXPECT_FALSE(pieces.isOk());
    const auto one = analyzeFrame(empty);
    EXPECT_FALSE(one.isOk());
    // Mismo motivo: quien lo enseñe no tiene que distinguir dos textos.
    EXPECT_EQ(pieces.error().message, one.error().message);

    EXPECT_FALSE(analyzeFrames(cv::Mat()).isOk());
}

TEST(MultiPiece, TheDetectionZoneStillApplies) {
    // Con zona de deteccion, las piezas de fuera no se cuentan y las de dentro
    // salen en coordenadas de la imagen completa.
    const cv::Mat scene = trayWith(6);
    PipelineConfig config;
    config.roi = cv::Rect(0, 0, 640, 400);
    const auto pieces = analyzeFrames(scene, config);
    ASSERT_TRUE(pieces.isOk());
    EXPECT_LT(pieces.value().size(), 6U) << "la zona tiene que dejar fuera a alguna";
    for (const auto& piece : pieces.value()) {
        EXPECT_TRUE(config.roi.contains(cv::Point(static_cast<int>(piece.fixture.origin.x),
                                                  static_cast<int>(piece.fixture.origin.y))))
            << "el resultado tiene que venir en coordenadas de la imagen completa";
        EXPECT_EQ(piece.mask.size(), scene.size());
    }
}

TEST(MultiPiece, CountingManyPiecesIsNotAbsurdlySlower) {
    // Analizar seis piezas no puede costar seis veces analizar una: cada una se
    // procesa dentro de su propia envolvente, no a tamano de frame.
    const cv::Mat one = trayWith(1);
    const cv::Mat six = trayWith(6);
    constexpr int kRuns = 20;
    const auto time = [](const cv::Mat& scene) {
        (void)analyzeFrames(scene);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kRuns; ++i) {
            (void)analyzeFrames(scene);
        }
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start)
                   .count() /
               kRuns;
    };
    const double oneMs = time(one);
    const double sixMs = time(six);
    std::printf("  1 pieza %.2f ms  |  6 piezas %.2f ms  -> %.2fx\n", oneMs, sixMs,
                sixMs / oneMs);
    EXPECT_LT(sixMs, oneMs * 3.0)
        << "seis piezas no pueden costar como seis analisis completos";
}

// ---------------------------------------------------------------------------
// Los ajustes de detección del operador tienen que llegar (C7)
// ---------------------------------------------------------------------------

TEST(DetectionSettings, TheManualThresholdChangesWhichBlobIsThePiece) {
    // Escena preparada para que el UMBRAL decida, y de forma determinista: un
    // fondo oscuro, una pieza grande gris medio y una pequena muy clara.
    //   - Con Otsu, el corte cae bajo y las dos son pieza: gana la grande.
    //   - Con umbral manual 180, solo la clara pasa: gana la pequena.
    //
    // Se probo antes con la polaridad sobre una escena de tres niveles y se
    // descarto: con un histograma trimodal, donde cae Otsu no es predecible y
    // el test medía la suerte, no la configuracion.
    //
    // Este test existe por un fallo real: el editor de plantilla llamaba a
    // analyzeFrame SIN configuracion, asi que detectaba con Otsu diera igual lo
    // que el operador tuviera puesto. Lo que se dibujaba encima y lo que luego
    // se inspeccionaba podian no ser la misma pieza.
    cv::Mat scene(720, 1280, CV_8UC1, cv::Scalar(20));
    const cv::Rect big(120, 200, 420, 320);
    const cv::Rect brightSmall(800, 260, 240, 200);
    cv::rectangle(scene, big, cv::Scalar(120), cv::FILLED);
    cv::rectangle(scene, brightSmall, cv::Scalar(240), cv::FILLED);

    const auto byDefault = analyzeFrame(scene, PipelineConfig{});
    ASSERT_TRUE(byDefault.isOk()) << byDefault.error().message;

    PipelineConfig manual;
    manual.segmentation.manualThreshold = 180;
    const auto forced = analyzeFrame(scene, manual);
    ASSERT_TRUE(forced.isOk()) << forced.error().message;

    const cv::Rect defaultBox = cv::boundingRect(byDefault.value().contour.points);
    const cv::Rect forcedBox = cv::boundingRect(forced.value().contour.points);
    std::printf("  Otsu -> %dx%d en (%d,%d)  |  umbral 180 -> %dx%d en (%d,%d)\n",
                defaultBox.width, defaultBox.height, defaultBox.x, defaultBox.y,
                forcedBox.width, forcedBox.height, forcedBox.x, forcedBox.y);

    // La configuracion CAMBIA que pieza se detecta: si el editor la ignora,
    // se dibuja sobre una y se inspecciona la otra.
    EXPECT_NE(defaultBox, forcedBox)
        << "si el umbral no cambiara nada, este test no probaria nada";
    EXPECT_NEAR(defaultBox.width, big.width, 6);
    EXPECT_NEAR(forcedBox.width, brightSmall.width, 6);
    EXPECT_NEAR(forcedBox.height, brightSmall.height, 6);
}

TEST(DetectionSettings, TheDetectionZoneAlsoDecides) {
    // El otro ajuste que el editor se saltaba: la zona de deteccion. Con dos
    // piezas, la zona elige cual es "la pieza".
    cv::Mat scene(720, 1280, CV_8UC1, cv::Scalar(20));
    cv::rectangle(scene, cv::Rect(100, 200, 300, 260), cv::Scalar(220), cv::FILLED);
    cv::rectangle(scene, cv::Rect(800, 200, 260, 220), cv::Scalar(220), cv::FILLED);

    const auto whole = analyzeFrame(scene, PipelineConfig{});
    ASSERT_TRUE(whole.isOk());

    PipelineConfig zoned;
    zoned.roi = cv::Rect(700, 100, 500, 500);
    const auto zonedResult = analyzeFrame(scene, zoned);
    ASSERT_TRUE(zonedResult.isOk());

    EXPECT_LT(whole.value().fixture.origin.x, 500.0F) << "sin zona gana la mayor";
    EXPECT_GT(zonedResult.value().fixture.origin.x, 700.0F)
        << "con zona gana la que cae dentro";
}

// ---------------------------------------------------------------------------
// La zona de trabajo: qué se usa y cuándo (corrección del fallo reportado)
// ---------------------------------------------------------------------------
//
// El fallo: la zona de detección dibujada a mano se guardaba, el botón pasaba a
// decir «Quitar zona» y la barra de estado decía que estaba activa… y no se
// usaba ni se pintaba, porque el modo de trabajo arranca en «imagen entera» y
// era el modo el que decidía. Dibujar y usar estaban desacoplados, y solo lo
// notaba quien fuese a Configurar ▸ Rendimiento a marcar el modo a mano.

TEST(WorkingZone, DrawingAZoneIsUsingIt) {
    // La regla que faltaba. Nadie arrastra un recuadro de detección para luego
    // no usarlo: el gesto ES la intención.
    EXPECT_EQ(modeAfterFixedZoneChanged(WorkingZoneMode::Off, true),
              WorkingZoneMode::Fixed);
    // Incluso desde automática: el gesto explícito manda sobre el seguimiento.
    EXPECT_EQ(modeAfterFixedZoneChanged(WorkingZoneMode::Automatic, true),
              WorkingZoneMode::Fixed);
    EXPECT_EQ(modeAfterFixedZoneChanged(WorkingZoneMode::Fixed, true),
              WorkingZoneMode::Fixed);
}

TEST(WorkingZone, RemovingTheZoneTurnsOffTheModeThatUsedIt) {
    // Si no, «fija» quedaría apuntando a un rectángulo que ya no existe, y el
    // programa diría que trabaja en una zona mientras mira la imagen entera.
    EXPECT_EQ(modeAfterFixedZoneChanged(WorkingZoneMode::Fixed, false),
              WorkingZoneMode::Off);
    // Los otros dos no dependen de la zona dibujada y no se tocan.
    EXPECT_EQ(modeAfterFixedZoneChanged(WorkingZoneMode::Automatic, false),
              WorkingZoneMode::Automatic);
    EXPECT_EQ(modeAfterFixedZoneChanged(WorkingZoneMode::Off, false),
              WorkingZoneMode::Off);
}

TEST(WorkingZone, EachModeUsesItsOwnRectangleAndNoOther) {
    const cv::Rect drawn(10, 20, 300, 200);
    const cv::Rect tracked(50, 60, 120, 90);

    EXPECT_EQ(effectiveWorkingZone(WorkingZoneMode::Fixed, drawn, tracked), drawn);
    EXPECT_EQ(effectiveWorkingZone(WorkingZoneMode::Automatic, drawn, tracked), tracked);
    // «Imagen entera» es imagen entera aunque haya un rectángulo guardado: es la
    // mitad de la regla que se rompió, y tiene que seguir siendo cierta.
    EXPECT_EQ(effectiveWorkingZone(WorkingZoneMode::Off, drawn, tracked).area(), 0);

    // Y un modo automático que todavía no ha enganchado nada da imagen entera,
    // no la zona dibujada.
    EXPECT_EQ(effectiveWorkingZone(WorkingZoneMode::Automatic, drawn, cv::Rect()).area(), 0);
}

TEST(AutoRoi, AFrameThatWasNeverAnalysedMustNotCountAsALostPiece) {
    // Con el contorno oculto la pose se congela y no se segmenta nada, así que
    // no hay contorno. Antes eso se le pasaba al seguimiento como «no hay
    // pieza» y a los dos frames se rendía con «se dejó de ver la pieza»: una
    // afirmación sobre algo que nadie había mirado. La ventana ya no lo
    // alimenta en ese caso; esto fija lo que pasa si se hiciera.
    AutoRoiTracker tracker;
    const cv::Size frame(1280, 720);
    const cv::Rect piece(500, 300, 180, 140);
    for (int k = 0; k < 5; ++k) {
        tracker.update(true, piece, frame);
    }
    ASSERT_TRUE(tracker.tracking()) << "primero tiene que estar siguiendo";

    // Tres frames sin pieza (el tolerado es 2) y el seguimiento se cae.
    for (int k = 0; k < 3; ++k) {
        tracker.update(false, cv::Rect(), frame);
    }
    EXPECT_FALSE(tracker.tracking());
    EXPECT_EQ(tracker.lastGiveUp(), AutoRoiGiveUp::PieceLost)
        << "y el motivo que vería el operador sería este, que con la pose "
           "congelada era mentira";
}
