#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

#include "ml/embedding_extractor.h"
#include "ml/reference.h"

using namespace pci::ml;

// --- Preprocesado (sin modelo) ---

TEST(Preprocess, EmptyImageGivesEmptyTensor) {
    EXPECT_TRUE(preprocessToTensor(cv::Mat(), PreprocessSpec{}).empty());
}

TEST(Preprocess, NhwcLayoutAndNormalization) {
    cv::Mat bgr(2, 2, CV_8UC3, cv::Scalar(0, 0, 0));
    bgr.at<cv::Vec3b>(0, 0) = {255, 127, 0};  // B=255 G=127 R=0

    PreprocessSpec spec;
    spec.width = 2;
    spec.height = 2;
    spec.nchw = false;

    const auto tensor = preprocessToTensor(bgr, spec);
    ASSERT_EQ(tensor.size(), 12U);
    // Píxel (0,0) en RGB: R=0, G=127, B=255 -> (x-127)/128
    EXPECT_FLOAT_EQ(tensor[0], (0.0F - 127.0F) / 128.0F);
    EXPECT_FLOAT_EQ(tensor[1], 0.0F);
    EXPECT_FLOAT_EQ(tensor[2], (255.0F - 127.0F) / 128.0F);
}

TEST(Preprocess, NchwLayoutSeparatesChannels) {
    cv::Mat bgr(1, 2, CV_8UC3);
    bgr.at<cv::Vec3b>(0, 0) = {0, 0, 255};  // rojo puro
    bgr.at<cv::Vec3b>(0, 1) = {255, 0, 0};  // azul puro

    PreprocessSpec spec;
    spec.width = 2;
    spec.height = 1;
    spec.nchw = true;

    const auto tensor = preprocessToTensor(bgr, spec);
    ASSERT_EQ(tensor.size(), 6U);
    const float hi = (255.0F - 127.0F) / 128.0F;
    const float lo = (0.0F - 127.0F) / 128.0F;
    // Canal R completo, luego G, luego B.
    EXPECT_FLOAT_EQ(tensor[0], hi);  // R del píxel 0
    EXPECT_FLOAT_EQ(tensor[1], lo);  // R del píxel 1
    EXPECT_FLOAT_EQ(tensor[4], lo);  // B del píxel 0
    EXPECT_FLOAT_EQ(tensor[5], hi);  // B del píxel 1
}

TEST(Preprocess, ResizesToSpec) {
    const cv::Mat big(64, 64, CV_8UC3, cv::Scalar(100, 100, 100));
    PreprocessSpec spec;
    spec.width = 8;
    spec.height = 8;
    EXPECT_EQ(preprocessToTensor(big, spec).size(), 8U * 8U * 3U);
}

TEST(Preprocess, L2Normalize) {
    std::vector<float> v{3.0F, 4.0F};
    l2Normalize(v);
    EXPECT_FLOAT_EQ(v[0], 0.6F);
    EXPECT_FLOAT_EQ(v[1], 0.8F);

    std::vector<float> zero{0.0F, 0.0F};
    l2Normalize(zero);
    EXPECT_FLOAT_EQ(zero[0], 0.0F);
}

// --- Similitud coseno ---

TEST(Cosine, IdenticalOrthogonalOpposite) {
    const std::vector<float> a{1.0F, 0.0F, 0.0F};
    const std::vector<float> b{0.0F, 1.0F, 0.0F};
    const std::vector<float> c{-1.0F, 0.0F, 0.0F};

    EXPECT_NEAR(cosineSimilarity(a, a), 1.0, 1e-9);
    EXPECT_NEAR(cosineSimilarity(a, b), 0.0, 1e-9);
    EXPECT_NEAR(cosineSimilarity(a, c), -1.0, 1e-9);
}

TEST(Cosine, ScaleInvariant) {
    const std::vector<float> a{1.0F, 2.0F, 3.0F};
    const std::vector<float> b{2.0F, 4.0F, 6.0F};
    EXPECT_NEAR(cosineSimilarity(a, b), 1.0, 1e-9);
}

TEST(Cosine, MismatchedSizesGiveZero) {
    EXPECT_EQ(cosineSimilarity({1.0F}, {1.0F, 2.0F}), 0.0);
    EXPECT_EQ(cosineSimilarity({}, {}), 0.0);
}

// --- ReferenceBuilder (Welford) ---

TEST(ReferenceBuilder, MatchesDirectComputation) {
    ReferenceBuilder builder;
    ASSERT_TRUE(builder.add({1.0F, 2.0F}).isOk());
    ASSERT_TRUE(builder.add({2.0F, 4.0F}).isOk());
    ASSERT_TRUE(builder.add({3.0F, 6.0F}).isOk());

    const auto reference = builder.build();
    ASSERT_TRUE(reference.isOk());
    EXPECT_NEAR(reference.value().mean[0], 2.0F, 1e-6F);
    EXPECT_NEAR(reference.value().mean[1], 4.0F, 1e-6F);
    // Desviación muestral de {1,2,3} = 1; de {2,4,6} = 2.
    EXPECT_NEAR(reference.value().stddev[0], 1.0F, 1e-6F);
    EXPECT_NEAR(reference.value().stddev[1], 2.0F, 1e-6F);
    EXPECT_EQ(reference.value().sampleCount, 3);
}

TEST(ReferenceBuilder, RejectsDimensionMismatch) {
    ReferenceBuilder builder;
    ASSERT_TRUE(builder.add({1.0F, 2.0F}).isOk());
    EXPECT_FALSE(builder.add({1.0F, 2.0F, 3.0F}).isOk());
    EXPECT_FALSE(builder.add({}).isOk());
}

TEST(ReferenceBuilder, NeedsTwoSamples) {
    ReferenceBuilder builder;
    EXPECT_FALSE(builder.build().isOk());
    ASSERT_TRUE(builder.add({1.0F}).isOk());
    EXPECT_FALSE(builder.build().isOk());
}

TEST(ReferenceBuilder, IncrementalContinuationKeepsMean) {
    ReferenceBuilder first;
    ASSERT_TRUE(first.add({1.0F, 0.0F}).isOk());
    ASSERT_TRUE(first.add({3.0F, 0.0F}).isOk());
    const auto reference = first.build();
    ASSERT_TRUE(reference.isOk());

    // Continuar con una muestra más: la media pasa de 2 a (1+3+5)/3 = 3.
    ReferenceBuilder second(reference.value());
    EXPECT_EQ(second.count(), 2);
    ASSERT_TRUE(second.add({5.0F, 0.0F}).isOk());
    const auto updated = second.build();
    ASSERT_TRUE(updated.isOk());
    EXPECT_NEAR(updated.value().mean[0], 3.0F, 1e-6F);
    EXPECT_EQ(updated.value().sampleCount, 3);
}

// --- Detección de anomalías ---

TEST(Anomaly, ConsistentSamplesAcceptSimilarRejectOrthogonal) {
    // Muestras casi idénticas alrededor de un eje dominante.
    ReferenceBuilder builder;
    for (int i = 0; i < 10; ++i) {
        std::vector<float> sample{1.0F, 0.01F * static_cast<float>(i % 3), 0.0F, 0.0F};
        l2Normalize(sample);
        ASSERT_TRUE(builder.add(sample).isOk());
    }
    const auto reference = builder.build();
    ASSERT_TRUE(reference.isOk());
    EXPECT_GT(reference.value().simMean, 0.99);

    std::vector<float> good{1.0F, 0.01F, 0.0F, 0.0F};
    l2Normalize(good);
    EXPECT_FALSE(isAnomalous(good, reference.value()));

    const std::vector<float> anomaly{0.0F, 0.0F, 1.0F, 0.0F};
    EXPECT_TRUE(isAnomalous(anomaly, reference.value()));
}

// --- Extractor con modelo real (se salta si el modelo no está descargado) ---

TEST(EmbeddingExtractorIntegration, ExtractsStableNormalizedEmbedding) {
    const std::filesystem::path modelPath =
        std::filesystem::path(PCI_MODELS_DIR) / "embedding_model.onnx";
    if (!std::filesystem::exists(modelPath)) {
        GTEST_SKIP() << "Modelo no descargado (ejecuta run.ps1); se omite la integración";
    }

    auto extractor = EmbeddingExtractor::create(modelPath.string());
    ASSERT_TRUE(extractor.isOk()) << extractor.error().message;

    cv::Mat pieza(256, 256, CV_8UC3, cv::Scalar(30, 30, 30));
    cv::rectangle(pieza, {60, 60}, {200, 160}, cv::Scalar(200, 180, 40), cv::FILLED);

    const auto first = extractor.value()->extract(pieza);
    ASSERT_TRUE(first.isOk()) << first.error().message;
    ASSERT_GT(first.value().size(), 0U);

    // L2-normalizado y determinista.
    double norm = 0.0;
    for (const float x : first.value()) {
        norm += static_cast<double>(x) * x;
    }
    EXPECT_NEAR(norm, 1.0, 1e-4);

    const auto second = extractor.value()->extract(pieza);
    ASSERT_TRUE(second.isOk());
    EXPECT_NEAR(cosineSimilarity(first.value(), second.value()), 1.0, 1e-6);

    // Una imagen distinta debe dar un embedding distinto.
    cv::Mat otra(256, 256, CV_8UC3, cv::Scalar(220, 220, 220));
    cv::circle(otra, {128, 128}, 70, cv::Scalar(10, 10, 10), cv::FILLED);
    const auto different = extractor.value()->extract(otra);
    ASSERT_TRUE(different.isOk());
    EXPECT_LT(cosineSimilarity(first.value(), different.value()), 0.999);
}
