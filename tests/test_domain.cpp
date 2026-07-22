#include <gtest/gtest.h>

#include "domain/calibration.h"
#include "domain/capture_quality.h"
#include "domain/verdict.h"

using namespace pci::domain;

// --- Veredicto combinado ---

TEST(Verdict, AllOkGivesOk) {
    EmbeddingCheck embedding;
    embedding.evaluated = true;
    embedding.similarity = 0.99;
    embedding.anomalous = false;

    const auto verdict = combineVerdict(embedding, {{"caliper", true, 40.0, "d=40px"}});
    EXPECT_TRUE(verdict.ok);
    EXPECT_EQ(verdict.summary, "OK");
}

TEST(Verdict, AnomalousAppearanceGivesNg) {
    EmbeddingCheck embedding;
    embedding.evaluated = true;
    embedding.anomalous = true;

    const auto verdict = combineVerdict(embedding, {{"caliper", true, 40.0, ""}});
    EXPECT_FALSE(verdict.ok);
    EXPECT_NE(verdict.summary.find("anomalía"), std::string::npos);
}

TEST(Verdict, FailedToolGivesNgWithCount) {
    EmbeddingCheck embedding;
    embedding.evaluated = true;

    const auto verdict = combineVerdict(
        embedding, {{"a", false, 1.0, ""}, {"b", true, 2.0, ""}, {"c", false, 3.0, ""}});
    EXPECT_FALSE(verdict.ok);
    EXPECT_NE(verdict.summary.find("2 herramienta(s)"), std::string::npos);
}

TEST(Verdict, NoModelStillOkWithNote) {
    EmbeddingCheck embedding;  // evaluated = false
    embedding.note = "modelo no disponible";

    const auto verdict = combineVerdict(embedding, {{"caliper", true, 40.0, ""}});
    EXPECT_TRUE(verdict.ok);
    EXPECT_NE(verdict.summary.find("sin comparación"), std::string::npos);
}

TEST(Verdict, BothFailuresListed) {
    EmbeddingCheck embedding;
    embedding.evaluated = true;
    embedding.anomalous = true;

    const auto verdict = combineVerdict(embedding, {{"a", false, 0.0, ""}});
    EXPECT_FALSE(verdict.ok);
    EXPECT_NE(verdict.summary.find("anomalía"), std::string::npos);
    EXPECT_NE(verdict.summary.find("herramienta"), std::string::npos);
}

// --- Calibración de escala ---

TEST(Calibration, FromKnownLength) {
    // 200 px corresponden a 50 mm -> 0.25 mm/px.
    const auto calibration = calibrationFromKnownLength(200.0, 50.0, 640, 60.0);
    ASSERT_TRUE(calibration.valid());
    EXPECT_DOUBLE_EQ(calibration.mmPerPixel, 0.25);
    EXPECT_DOUBLE_EQ(calibration.toMm(100.0), 25.0);
    EXPECT_GT(calibration.cameraDistanceMm, 0.0);
}

TEST(Calibration, MethodsAreConsistent) {
    // La distancia estimada por el método A debe reproducir la misma escala
    // al usarla como entrada del método B (mismo FOV y ancho de imagen).
    const auto fromLength = calibrationFromKnownLength(200.0, 50.0, 640, 60.0);
    const auto fromDistance = calibrationFromCameraDistance(
        fromLength.cameraDistanceMm, 60.0, 640);
    ASSERT_TRUE(fromDistance.valid());
    EXPECT_NEAR(fromDistance.mmPerPixel, fromLength.mmPerPixel, 1e-9);
}

TEST(Calibration, InvalidInputsGiveUncalibrated) {
    EXPECT_FALSE(calibrationFromKnownLength(0.0, 50.0, 640, 60.0).valid());
    EXPECT_FALSE(calibrationFromKnownLength(200.0, 0.0, 640, 60.0).valid());
    EXPECT_FALSE(calibrationFromCameraDistance(0.0, 60.0, 640).valid());
    EXPECT_DOUBLE_EQ(estimateCameraDistanceMm(0.0, 60.0, 640), 0.0);
}

TEST(Calibration, FormatLengthWithAndWithoutScale) {
    ScaleCalibration none;
    EXPECT_NE(none.formatLength(42.3).find("px"), std::string::npos);
    EXPECT_EQ(none.formatLength(42.3).find("mm"), std::string::npos);

    const auto calibrated = calibrationFromKnownLength(100.0, 25.0, 640, 60.0);
    const std::string text = calibrated.formatLength(100.0);
    EXPECT_NE(text.find("25.00 mm"), std::string::npos);
    EXPECT_NE(text.find("px"), std::string::npos);

    // A partir de 100 mm se muestra en cm.
    const std::string big = calibrated.formatLength(600.0);  // 150 mm
    EXPECT_NE(big.find("15.00 cm"), std::string::npos);
}

TEST(Calibration, ResolutionMatchGuardsStaleScale) {
    ScaleCalibration cal;
    cal.mmPerPixel = 0.1;

    // Sin resolución conocida (calibración heredada): no se cuestiona.
    EXPECT_FALSE(cal.resolutionKnown());
    EXPECT_TRUE(cal.matchesResolution(640, 480));
    EXPECT_TRUE(cal.matchesResolution(1920, 1080));

    // Con resolución sellada: solo coincide con esa exacta.
    cal.calibratedWidth = 1280;
    cal.calibratedHeight = 720;
    EXPECT_TRUE(cal.resolutionKnown());
    EXPECT_TRUE(cal.matchesResolution(1280, 720));
    EXPECT_FALSE(cal.matchesResolution(640, 480));
    EXPECT_FALSE(cal.matchesResolution(1280, 721));
}

// --- Criterios de calidad de captura ---

namespace {

QualityMetrics goodMetrics() {
    QualityMetrics metrics;
    metrics.sharpness = 200.0;
    metrics.meanBrightness = 120.0;
    metrics.clippedFraction = 0.01;
    metrics.pieceFound = true;
    metrics.pieceTouchesBorder = false;
    return metrics;
}

}  // namespace

TEST(CaptureQuality, GoodCaptureAccepted) {
    EXPECT_TRUE(validateQuality(goodMetrics()).isOk());
}

TEST(CaptureQuality, RejectionsWithReasons) {
    auto noPiece = goodMetrics();
    noPiece.pieceFound = false;
    auto result = validateQuality(noPiece);
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.error().message.find("pieza"), std::string::npos);

    auto cut = goodMetrics();
    cut.pieceTouchesBorder = true;
    result = validateQuality(cut);
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.error().message.find("borde"), std::string::npos);

    auto blurry = goodMetrics();
    blurry.sharpness = 5.0;
    result = validateQuality(blurry);
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.error().message.find("borrosa"), std::string::npos);

    auto dark = goodMetrics();
    dark.meanBrightness = 15.0;
    result = validateQuality(dark);
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.error().message.find("oscura"), std::string::npos);

    auto clipped = goodMetrics();
    clipped.clippedFraction = 0.5;
    result = validateQuality(clipped);
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.error().message.find("saturada"), std::string::npos);
}
