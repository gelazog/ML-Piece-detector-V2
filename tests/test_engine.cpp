#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <filesystem>
#include <memory>
#include <string>

#include "database/db.h"
#include "database/schema.h"
#include "engine/inspection_engine.h"
#include "engine/registration_session.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "ml/embedding_extractor.h"
#include "repositories/inspection_repository.h"
#include "repositories/piece_repository.h"
#include "repositories/tool_repository.h"
#include "test_helpers.h"
#include "vision/pipeline.h"
#include "vision/position_fixture.h"
#include "vision/quality_metrics.h"

using namespace pci;
using pci::testhelpers::drawLPiece;
using pci::testhelpers::lPointToImage;

namespace {

// Embedding falso determinista: medias de los 4 cuadrantes del recorte
// normalizado. Piezas iguales -> vectores casi idénticos; formas distintas ->
// vectores distintos. Suficiente para probar el flujo sin el modelo ONNX.
core::Result<std::vector<float>> fakeEmbed(const cv::Mat& normalizedBgr) {
    cv::Mat gray;
    if (normalizedBgr.channels() == 3) {
        cv::cvtColor(normalizedBgr, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = normalizedBgr;
    }
    const int hw = gray.cols / 2;
    const int hh = gray.rows / 2;
    std::vector<float> embedding = {
        static_cast<float>(cv::mean(gray(cv::Rect(0, 0, hw, hh)))[0] / 255.0),
        static_cast<float>(cv::mean(gray(cv::Rect(hw, 0, hw, hh)))[0] / 255.0),
        static_cast<float>(cv::mean(gray(cv::Rect(0, hh, hw, hh)))[0] / 255.0),
        static_cast<float>(cv::mean(gray(cv::Rect(hw, hh, hw, hh)))[0] / 255.0),
    };
    ml::l2Normalize(embedding);
    return core::Result<std::vector<float>>::ok(std::move(embedding));
}

class EngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = (std::filesystem::temp_directory_path() /
                 (std::string("pci_engine_") + info->name() + ".db"))
                    .string();
        std::filesystem::remove(path_);

        auto opened = database::Db::open(path_);
        ASSERT_TRUE(opened.isOk()) << opened.error().message;
        db_ = std::move(opened.value());
        ASSERT_TRUE(database::migrate(*db_).isOk());

        pieces_ = std::make_unique<repositories::PieceRepository>(*db_);
        tools_ = std::make_unique<repositories::ToolRepository>(*db_);
        history_ = std::make_unique<repositories::InspectionRepository>(*db_);
        // Los tests usan piezas en distintas rotaciones: seguir la rotación.
        engine::EngineOptions options;
        options.pipeline.autoOrient = true;
        engine_ = std::make_unique<engine::InspectionEngine>(fakeEmbed, *pieces_, *tools_,
                                                             *history_, options);
    }

    void TearDown() override {
        engine_.reset();
        history_.reset();
        tools_.reset();
        pieces_.reset();
        db_.reset();
        for (const char* suffix : {"", "-wal", "-shm"}) {
            std::filesystem::remove(path_ + suffix);
        }
    }

    // Registra la pieza L con varias capturas sintéticas y un caliper cruzando
    // el brazo vertical. Devuelve el id de la pieza.
    std::int64_t registerLPiece() {
        vision::PipelineConfig cfg;
        cfg.autoOrient = true;  // igual que el motor: piezas en varias rotaciones
        engine::RegistrationSession session(fakeEmbed, 30, 5, std::nullopt, cfg);
        for (int i = 0; i < 8; ++i) {
            const auto frame = drawLPiece({640, 480},
                                          {300.0F + static_cast<float>(i * 3),
                                           240.0F - static_cast<float>(i * 2)},
                                          15.0 + i * 4.0, 40.0F, 40, 220);
            cv::Mat bgr;
            cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
            const auto feedback = session.addFrame(bgr);
            EXPECT_TRUE(feedback.isOk()) << feedback.error().message;
            EXPECT_TRUE(feedback.value().accepted) << feedback.value().reason;
        }
        EXPECT_TRUE(session.readyToFinish());
        auto reference = session.finish();
        EXPECT_TRUE(reference.isOk());

        auto pieceId = pieces_->createPiece("L-test");
        EXPECT_TRUE(pieceId.isOk());
        EXPECT_TRUE(pieces_->saveReference(pieceId.value(), reference.value()).isOk());

        // Caliper anclado al fixture del primer frame de referencia.
        const auto refFrame = drawLPiece({640, 480}, {300.0F, 240.0F}, 15.0, 40.0F, 40, 220);
        const auto analysis = vision::analyzeFrame(refFrame, cfg);
        EXPECT_TRUE(analysis.isOk());
        inspection::CaliperGeometry g;
        g.p0 = vision::toPieceCoords(analysis.value().fixture,
                                     lPointToImage({-0.7F, 2.0F}, {300.0F, 240.0F}, 15.0, 40.0F));
        g.p1 = vision::toPieceCoords(analysis.value().fixture,
                                     lPointToImage({1.7F, 2.0F}, {300.0F, 240.0F}, 15.0, 40.0F));
        g.bandWidth = 5.0F;

        inspection::ToolConfig config;
        config.type = inspection::ToolType::Caliper;
        config.name = "Ancho brazo";
        config.geometryJson = inspection::toJson(inspection::ToolGeometry(g));
        config.toleranceMin = 35.0;
        config.toleranceMax = 45.0;
        EXPECT_TRUE(tools_->save(pieceId.value(), config).isOk());

        return pieceId.value();
    }

    std::string path_;
    std::unique_ptr<database::Db> db_;
    std::unique_ptr<repositories::PieceRepository> pieces_;
    std::unique_ptr<repositories::ToolRepository> tools_;
    std::unique_ptr<repositories::InspectionRepository> history_;
    std::unique_ptr<engine::InspectionEngine> engine_;
};

}  // namespace

// --- Registro ---

TEST_F(EngineTest, RegistrationRejectsBadCaptures) {
    engine::RegistrationSession session(fakeEmbed, 30, 5);

    // Frame vacío/uniforme: sin pieza.
    cv::Mat flat(480, 640, CV_8UC3, cv::Scalar(128, 128, 128));
    auto feedback = session.addFrame(flat);
    ASSERT_TRUE(feedback.isOk());
    EXPECT_FALSE(feedback.value().accepted);
    EXPECT_NE(feedback.value().reason.find("pieza"), std::string::npos);

    // Pieza cortada por el borde.
    cv::Mat cut;
    cv::cvtColor(drawLPiece({640, 480}, {40.0F, 240.0F}, 0.0, 40.0F, 40, 220), cut,
                 cv::COLOR_GRAY2BGR);
    feedback = session.addFrame(cut);
    ASSERT_TRUE(feedback.isOk());
    EXPECT_FALSE(feedback.value().accepted);
    EXPECT_NE(feedback.value().reason.find("borde"), std::string::npos);

    EXPECT_EQ(session.count(), 0);
    EXPECT_FALSE(session.finish().isOk());
}

TEST_F(EngineTest, QualityMetricsDetectBlur) {
    const auto sharp = drawLPiece({640, 480}, {320.0F, 240.0F}, 20.0, 40.0F, 40, 220);
    cv::Mat blurred;
    cv::GaussianBlur(sharp, blurred, cv::Size(31, 31), 8.0);

    const auto sharpAnalysis = vision::analyzeFrame(sharp);
    ASSERT_TRUE(sharpAnalysis.isOk());
    const auto sharpMetrics = vision::computeQualityMetrics(sharp, &sharpAnalysis.value());

    const auto blurAnalysis = vision::analyzeFrame(blurred);
    const auto blurMetrics = vision::computeQualityMetrics(
        blurred, blurAnalysis.isOk() ? &blurAnalysis.value() : nullptr);

    EXPECT_GT(sharpMetrics.sharpness, blurMetrics.sharpness * 5.0);
}

// --- Inspección end-to-end ---

TEST_F(EngineTest, GoodPieceIsOkAndPersisted) {
    const auto pieceId = registerLPiece();

    // Pieza buena, rotada y desplazada respecto al registro.
    cv::Mat frame;
    cv::cvtColor(drawLPiece({640, 480}, {330.0F, 215.0F}, 100.0, 40.0F, 40, 220), frame,
                 cv::COLOR_GRAY2BGR);

    const auto outcome = engine_->inspect(frame, pieceId);
    ASSERT_TRUE(outcome.isOk()) << outcome.error().message;
    EXPECT_TRUE(outcome.value().verdict.ok) << outcome.value().verdict.summary;
    EXPECT_TRUE(outcome.value().verdict.embedding.evaluated);
    EXPECT_GT(outcome.value().verdict.embedding.similarity, 0.99);
    ASSERT_EQ(outcome.value().toolResults.size(), 1U);
    EXPECT_NEAR(outcome.value().toolResults[0].measured, 40.0, 2.0);
    EXPECT_GE(outcome.value().historyId, 0) << outcome.value().persistError;

    const auto recent = history_->recentForPiece(pieceId);
    ASSERT_TRUE(recent.isOk());
    ASSERT_EQ(recent.value().size(), 1U);
    EXPECT_EQ(recent.value()[0].verdict, "OK");

    const auto stats = history_->todayStats(pieceId);
    ASSERT_TRUE(stats.isOk());
    EXPECT_EQ(stats.value().total, 1);
    EXPECT_EQ(stats.value().okCount, 1);
}

TEST_F(EngineTest, DefectivePieceIsNg) {
    const auto pieceId = registerLPiece();

    // Brazo vertical adelgazado (defecto dimensional): el caliper debe fallar.
    // Pieza a 0° para que el recorte axis-aligned corte el brazo con exactitud;
    // el anclaje con rotación ya lo cubre FixtureAnchoring.
    cv::Mat defective = drawLPiece({640, 480}, {300.0F, 240.0F}, 0.0, 40.0F, 40, 220);
    const cv::Point2f notchA = lPointToImage({0.0F, 1.3F}, {300.0F, 240.0F}, 0.0, 40.0F);
    const cv::Point2f notchB = lPointToImage({0.35F, 2.9F}, {300.0F, 240.0F}, 0.0, 40.0F);
    cv::rectangle(defective, cv::Point(cvRound(notchA.x), cvRound(notchA.y)),
                  cv::Point(cvRound(notchB.x), cvRound(notchB.y)), cv::Scalar(220),
                  cv::FILLED);
    cv::Mat frame;
    cv::cvtColor(defective, frame, cv::COLOR_GRAY2BGR);

    const auto outcome = engine_->inspect(frame, pieceId);
    ASSERT_TRUE(outcome.isOk()) << outcome.error().message;
    EXPECT_FALSE(outcome.value().verdict.ok);

    const auto stats = history_->todayStats(pieceId);
    ASSERT_TRUE(stats.isOk());
    EXPECT_EQ(stats.value().ngCount, 1);
}

TEST_F(EngineTest, DifferentShapeIsAnomalous) {
    const auto pieceId = registerLPiece();

    // Un disco en lugar de la L: apariencia totalmente distinta.
    cv::Mat disc(480, 640, CV_8UC3, cv::Scalar(220, 220, 220));
    cv::circle(disc, {320, 240}, 80, cv::Scalar(40, 40, 40), cv::FILLED);

    const auto outcome = engine_->inspect(disc, pieceId);
    ASSERT_TRUE(outcome.isOk()) << outcome.error().message;
    EXPECT_FALSE(outcome.value().verdict.ok);
    EXPECT_TRUE(outcome.value().verdict.embedding.evaluated);
    EXPECT_TRUE(outcome.value().verdict.embedding.anomalous);
}

TEST_F(EngineTest, IncrementalLearningCreatesNewVersion) {
    const auto pieceId = registerLPiece();

    cv::Mat frame;
    cv::cvtColor(drawLPiece({640, 480}, {310.0F, 235.0F}, 60.0, 40.0F, 40, 220), frame,
                 cv::COLOR_GRAY2BGR);
    const auto outcome = engine_->inspect(frame, pieceId);
    ASSERT_TRUE(outcome.isOk());
    ASSERT_TRUE(outcome.value().verdict.ok) << outcome.value().verdict.summary;

    const auto newVersion = engine_->updateReference(pieceId, outcome.value().embedding);
    ASSERT_TRUE(newVersion.isOk()) << newVersion.error().message;
    EXPECT_EQ(newVersion.value(), 2);

    // La versión anterior sigue existiendo (nunca se borra el historial).
    const auto versions = pieces_->listReferenceVersions(pieceId);
    ASSERT_TRUE(versions.isOk());
    EXPECT_EQ(versions.value(), (std::vector<int>{1, 2}));

    const auto latest = pieces_->loadLatestReference(pieceId);
    ASSERT_TRUE(latest.isOk());
    EXPECT_EQ(latest.value().reference.sampleCount, 9);  // 8 del registro + 1
}

TEST_F(EngineTest, NoModelDegradesToToolsOnly) {
    const auto pieceId = registerLPiece();
    engine::EngineOptions options;
    options.pipeline.autoOrient = true;
    engine::InspectionEngine noModel(nullptr, *pieces_, *tools_, *history_, options);

    cv::Mat frame;
    cv::cvtColor(drawLPiece({640, 480}, {300.0F, 240.0F}, 15.0, 40.0F, 40, 220), frame,
                 cv::COLOR_GRAY2BGR);

    const auto outcome = noModel.inspect(frame, pieceId);
    ASSERT_TRUE(outcome.isOk()) << outcome.error().message;
    EXPECT_FALSE(outcome.value().verdict.embedding.evaluated);
    EXPECT_TRUE(outcome.value().verdict.ok) << outcome.value().verdict.summary;
    ASSERT_EQ(outcome.value().toolResults.size(), 1U);
}
