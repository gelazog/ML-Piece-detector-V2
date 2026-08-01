#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>

#include <opencv2/objdetect/aruco_detector.hpp>

#include <array>

#include "vision/board_frame.h"
#include "vision/contour_analysis.h"
#include "vision/fixture_stabilizer.h"
#include "vision/orientation.h"
#include "vision/plane_scale.h"
#include "vision/orientation_anchor.h"
#include "vision/pipeline.h"
#include "vision/position_fixture.h"
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
