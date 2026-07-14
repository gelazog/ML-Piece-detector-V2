#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>

#include <opencv2/objdetect/aruco_detector.hpp>

#include <array>

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
