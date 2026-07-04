#include <gtest/gtest.h>

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
