#include <gtest/gtest.h>

#include <cstdio>

#include "domain/shift_report.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

#include "database/db.h"
#include "database/schema.h"
#include "database/statement.h"
#include "domain/verdict.h"
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

TEST_F(EngineTest, DailyStatsAggregatesByDay) {
    auto pieceId = pieces_->createPiece("stats-test");
    ASSERT_TRUE(pieceId.isOk());

    domain::InspectionVerdict okV;
    okV.ok = true;
    okV.summary = "OK";
    domain::InspectionVerdict ngV;
    ngV.ok = false;
    ngV.summary = "NG";
    ASSERT_TRUE(history_->saveInspection(pieceId.value(), 1, okV, {}, {}).isOk());
    ASSERT_TRUE(history_->saveInspection(pieceId.value(), 1, okV, {}, {}).isOk());
    ASSERT_TRUE(history_->saveInspection(pieceId.value(), 1, ngV, {}, {}).isOk());

    auto daily = history_->dailyStats(pieceId.value(), 30);
    ASSERT_TRUE(daily.isOk());
    ASSERT_EQ(daily.value().size(), 1U);  // todas las inspecciones son de hoy
    EXPECT_EQ(daily.value()[0].total, 3);
    EXPECT_EQ(daily.value()[0].okCount, 2);
    EXPECT_EQ(daily.value()[0].ngCount, 1);
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

// --- Registro sin modelo ONNX (G1) ---

// Sin modelo, la sesion acepta capturas por CALIDAD: la pieza se puede
// registrar como medidor puro en vez de quedarse fuera del sistema.
TEST_F(EngineTest, RegistrationWithoutModelAcceptsFramesAndHasNoReference) {
    vision::PipelineConfig cfg;
    cfg.autoOrient = true;
    engine::RegistrationSession session(nullptr, 30, 5, std::nullopt, cfg);
    EXPECT_TRUE(session.toolsOnly());

    for (int i = 0; i < 6; ++i) {
        cv::Mat bgr;
        cv::cvtColor(drawLPiece({640, 480}, {300.0F + static_cast<float>(i * 3), 240.0F},
                                10.0 + i * 5.0, 40.0F, 40, 220),
                     bgr, cv::COLOR_GRAY2BGR);
        const auto feedback = session.addFrame(bgr);
        ASSERT_TRUE(feedback.isOk()) << feedback.error().message;
        EXPECT_TRUE(feedback.value().accepted) << feedback.value().reason;
        EXPECT_EQ(feedback.value().count, i + 1);
    }
    EXPECT_EQ(session.count(), 6);
    EXPECT_TRUE(session.readyToFinish());
    EXPECT_FALSE(session.firstNormalized().empty());  // sirve para la miniatura

    auto reference = session.finish();
    ASSERT_TRUE(reference.isOk());
    // Referencia VACIA a proposito: la UI no debe guardarla.
    EXPECT_TRUE(reference.value().mean.empty());
}

// Un frame malo se sigue rechazando sin modelo: la calidad no depende de ONNX.
TEST_F(EngineTest, RegistrationWithoutModelStillRejectsBadFrames) {
    engine::RegistrationSession session(nullptr, 30, 5);
    const cv::Mat uniform(480, 640, CV_8UC3, cv::Scalar(128, 128, 128));
    const auto feedback = session.addFrame(uniform);
    ASSERT_TRUE(feedback.isOk()) << feedback.error().message;
    EXPECT_FALSE(feedback.value().accepted);
    EXPECT_EQ(session.count(), 0);
    EXPECT_FALSE(session.finish().isOk());  // no llega al minimo
}

// Guardar una referencia vacia haria que la similitud fuese 0 y todo saliese
// NG. La primera barrera es el repositorio, que ni siquiera la acepta; el
// motor tiene ademas una guarda por si una fila llegara corrupta.
TEST_F(EngineTest, EmptyReferenceIsRejectedBeforeItCanBeStored) {
    const auto pieceId = registerLPiece();
    EXPECT_FALSE(pieces_->saveReference(pieceId, ml::Reference{}).isOk());

    // La pieza sigue con su referencia buena y se inspecciona normalmente.
    cv::Mat frame;
    cv::cvtColor(drawLPiece({640, 480}, {300.0F, 240.0F}, 15.0, 40.0F, 40, 220), frame,
                 cv::COLOR_GRAY2BGR);
    const auto outcome = engine_->inspect(frame, pieceId);
    ASSERT_TRUE(outcome.isOk()) << outcome.error().message;
    EXPECT_TRUE(outcome.value().verdict.embedding.evaluated);
}

// Una pieza registrada en modo "solo herramientas" no tiene NINGUNA referencia
// guardada: la inspeccion debe seguir funcionando con las herramientas.
TEST_F(EngineTest, PieceWithoutReferenceInspectsWithToolsOnly) {
    const auto pieceId = pieces_->createPiece("solo-herramientas");
    ASSERT_TRUE(pieceId.isOk());

    cv::Mat frame;
    cv::cvtColor(drawLPiece({640, 480}, {300.0F, 240.0F}, 15.0, 40.0F, 40, 220), frame,
                 cv::COLOR_GRAY2BGR);
    const auto outcome = engine_->inspect(frame, pieceId.value());
    ASSERT_TRUE(outcome.isOk()) << outcome.error().message;
    EXPECT_FALSE(outcome.value().verdict.embedding.evaluated);
    EXPECT_FALSE(outcome.value().verdict.embedding.note.empty());
    EXPECT_TRUE(outcome.value().verdict.ok) << outcome.value().verdict.summary;
}

// ===========================================================================
//  Motor bajo condiciones adversas y en tandas largas.
// ===========================================================================

TEST_F(EngineTest, InspectingWithoutAPieceInFrameFailsControlled) {
    const auto pieceId = registerLPiece();
    const cv::Mat empty(480, 640, CV_8UC3, cv::Scalar(128, 128, 128));
    const auto outcome = engine_->inspect(empty, pieceId);
    ASSERT_FALSE(outcome.isOk()) << "sin pieza no puede haber veredicto";
    EXPECT_FALSE(outcome.error().message.empty());
}

TEST_F(EngineTest, InspectingAnUnknownPieceStillMeasuresWithoutAppearance) {
    // Un id que no existe: no hay referencia ni herramientas, pero la
    // inspeccion no puede reventar.
    cv::Mat frame;
    cv::cvtColor(drawLPiece({640, 480}, {300.0F, 240.0F}, 15.0, 40.0F, 40, 220), frame,
                 cv::COLOR_GRAY2BGR);
    const auto outcome = engine_->inspect(frame, 999999);
    ASSERT_TRUE(outcome.isOk()) << outcome.error().message;
    EXPECT_FALSE(outcome.value().verdict.embedding.evaluated);
    EXPECT_TRUE(outcome.value().toolResults.empty());
    // El historial SI falla (clave foranea: esa pieza no existe), pero eso se
    // reporta aparte y no tumba la inspeccion ni oculta el veredicto.
    EXPECT_EQ(outcome.value().historyId, -1);
    EXPECT_FALSE(outcome.value().persistError.empty());
}

// Tanda larga: la linea inspecciona sin parar durante un turno. El historial y
// las estadisticas tienen que cuadrar exactamente con lo inspeccionado.
TEST_F(EngineTest, LongRunKeepsHistoryAndStatsConsistent) {
    const auto pieceId = registerLPiece();
    constexpr int kGood = 18;
    constexpr int kBad = 7;

    for (int i = 0; i < kGood; ++i) {
        cv::Mat frame;
        cv::cvtColor(drawLPiece({640, 480}, {300.0F + static_cast<float>(i % 5), 240.0F},
                                15.0 + (i % 3), 40.0F, 40, 220),
                     frame, cv::COLOR_GRAY2BGR);
        const auto outcome = engine_->inspect(frame, pieceId);
        ASSERT_TRUE(outcome.isOk()) << outcome.error().message;
        EXPECT_TRUE(outcome.value().verdict.ok) << outcome.value().verdict.summary;
    }
    for (int i = 0; i < kBad; ++i) {
        // Pieza claramente mas pequena: el caliper se sale de tolerancia.
        cv::Mat frame;
        cv::cvtColor(drawLPiece({640, 480}, {300.0F, 240.0F}, 15.0, 26.0F, 40, 220), frame,
                     cv::COLOR_GRAY2BGR);
        const auto outcome = engine_->inspect(frame, pieceId);
        ASSERT_TRUE(outcome.isOk()) << outcome.error().message;
        EXPECT_FALSE(outcome.value().verdict.ok);
    }

    const auto stats = history_->todayStats(pieceId);
    ASSERT_TRUE(stats.isOk());
    EXPECT_EQ(stats.value().okCount, kGood);
    EXPECT_EQ(stats.value().ngCount, kBad);
    EXPECT_EQ(stats.value().total, kGood + kBad);

    const auto recent = history_->recentForPiece(pieceId, 100);
    ASSERT_TRUE(recent.isOk());
    EXPECT_EQ(static_cast<int>(recent.value().size()), kGood + kBad);
}

// El aprendizaje incremental se usa una y otra vez sobre la misma pieza: las
// versiones deben subir de una en una y la referencia seguir siendo utilizable.
TEST_F(EngineTest, RepeatedIncrementalLearningKeepsTheReferenceUsable) {
    const auto pieceId = registerLPiece();
    int previousVersion = pieces_->loadLatestReference(pieceId).value().version;

    for (int i = 0; i < 5; ++i) {
        cv::Mat frame;
        cv::cvtColor(drawLPiece({640, 480}, {300.0F, 240.0F}, 15.0 + i * 2.0, 40.0F, 40, 220),
                     frame, cv::COLOR_GRAY2BGR);
        const auto outcome = engine_->inspect(frame, pieceId);
        ASSERT_TRUE(outcome.isOk()) << outcome.error().message;
        const auto version = engine_->updateReference(pieceId, outcome.value().embedding);
        ASSERT_TRUE(version.isOk()) << version.error().message;
        EXPECT_EQ(version.value(), previousVersion + 1);
        previousVersion = version.value();
    }

    // Tras cinco actualizaciones, una pieza buena sigue saliendo OK.
    cv::Mat frame;
    cv::cvtColor(drawLPiece({640, 480}, {300.0F, 240.0F}, 15.0, 40.0F, 40, 220), frame,
                 cv::COLOR_GRAY2BGR);
    const auto outcome = engine_->inspect(frame, pieceId);
    ASSERT_TRUE(outcome.isOk());
    EXPECT_TRUE(outcome.value().verdict.ok) << outcome.value().verdict.summary;
    const auto stored = pieces_->loadLatestReference(pieceId);
    ASSERT_TRUE(stored.isOk());
    EXPECT_GT(stored.value().reference.sampleCount, 0);
}

// ---------------------------------------------------------------------------
// Medir varias piezas con la misma plantilla (C6)
// ---------------------------------------------------------------------------

namespace {

// Bandeja de tres barras iguales salvo la del medio, mas estrecha: al medirlas
// con la misma plantilla, esa tiene que ser la unica que salga fuera.
cv::Mat trayOfBars(int narrowIndex, int narrowWidth) {
    cv::Mat scene(720, 1280, CV_8UC1, cv::Scalar(20));
    for (int i = 0; i < 3; ++i) {
        const int width = (i == narrowIndex) ? narrowWidth : 200;
        // Separadas de sobra; alturas distintas para que el orden por area sea
        // el mismo en todas las ejecuciones y no dependa de findContours.
        cv::rectangle(scene, cv::Rect(80 + i * 400, 250, width, 200 - i * 10),
                      cv::Scalar(220), cv::FILLED);
    }
    return scene;
}

}  // namespace

TEST_F(EngineTest, MeasuresEveryPieceWithTheSameTemplate) {
    auto& pieces = *pieces_;
    auto& tools = *tools_;
    auto& history = *history_;
    auto pieceId = pieces.createPiece("bandeja");
    ASSERT_TRUE(pieceId.isOk());
    auto measurement = pieces.loadMeasurement(pieceId.value());
    ASSERT_TRUE(measurement.isOk());
    measurement.value().expectedPieces = 3;
    ASSERT_TRUE(pieces.saveMeasurement(pieceId.value(), measurement.value()).isOk());

    // Una regla que cruza la barra a lo ancho, en coordenadas de pieza: el
    // centro de la barra es el origen, asi que va de -120 a +120 en x.
    inspection::ToolConfig caliper;
    caliper.type = inspection::ToolType::Caliper;
    caliper.name = "ancho";
    caliper.geometryJson = inspection::toJson(inspection::ToolGeometry(
        inspection::CaliperGeometry{{-160.0F, 0.0F}, {160.0F, 0.0F}, 20.0F}));
    caliper.toleranceMin = 180.0;
    caliper.toleranceMax = 220.0;
    ASSERT_TRUE(tools.save(pieceId.value(), caliper, "principal").isOk());

    // Motor propio sin autoOrient: las barras son simetricas y su eje
    // principal podria voltear entre ejecuciones, que no es lo que se prueba.
    engine::InspectionEngine engine(nullptr, pieces, tools, history);
    cv::Mat scene;
    cv::cvtColor(trayOfBars(1, 120), scene, cv::COLOR_GRAY2BGR);
    auto outcome = engine.inspect(scene, pieceId.value());
    ASSERT_TRUE(outcome.isOk()) << outcome.error().message;

    EXPECT_EQ(outcome.value().piecesFound, 3);
    EXPECT_EQ(outcome.value().pieceFixtures.size(), 3U);
    // Tres piezas x una herramienta = tres medidas, cada una con su indice.
    ASSERT_EQ(outcome.value().toolResults.size(), 3U);
    std::vector<int> indices;
    for (const auto& result : outcome.value().toolResults) {
        indices.push_back(result.pieceIndex);
        std::printf("  pieza %d: %s -> %.1f (%s)\n", result.pieceIndex, result.name.c_str(),
                    result.measured, result.ok ? "OK" : "NG");
    }
    std::sort(indices.begin(), indices.end());
    EXPECT_EQ(indices, (std::vector<int>{0, 1, 2}));

    // La bandeja entera es NG porque una barra lo es: el veredicto es el peor.
    EXPECT_FALSE(outcome.value().verdict.ok);
    int failed = 0;
    for (const auto& tool : outcome.value().verdict.tools) {
        if (!tool.ok) {
            ++failed;
            // El nombre dice EN QUE PIEZA mirar: sin eso habria que ir barra
            // por barra a mano, que es justo el trabajo que esto ahorra.
            EXPECT_NE(tool.name.find("pieza"), std::string::npos) << tool.name;
        }
    }
    EXPECT_EQ(failed, 1) << "solo una de las tres barras esta fuera";
}

TEST_F(EngineTest, WithOnePieceExpectedNothingChanges) {
    // El caso de siempre no puede haberse movido: una sola pieza, una sola
    // medida, sin indices ni nombres decorados.
    auto& pieces = *pieces_;
    auto& tools = *tools_;
    auto& history = *history_;
    auto pieceId = pieces.createPiece("suelta");
    ASSERT_TRUE(pieceId.isOk());

    inspection::ToolConfig caliper;
    caliper.type = inspection::ToolType::Caliper;
    caliper.name = "ancho";
    caliper.geometryJson = inspection::toJson(inspection::ToolGeometry(
        inspection::CaliperGeometry{{-160.0F, 0.0F}, {160.0F, 0.0F}, 20.0F}));
    caliper.toleranceMin = 180.0;
    caliper.toleranceMax = 220.0;
    ASSERT_TRUE(tools.save(pieceId.value(), caliper, "principal").isOk());

    // Motor propio sin autoOrient: las barras son simetricas y su eje
    // principal podria voltear entre ejecuciones, que no es lo que se prueba.
    engine::InspectionEngine engine(nullptr, pieces, tools, history);
    cv::Mat scene;
    cv::cvtColor(trayOfBars(-1, 200), scene, cv::COLOR_GRAY2BGR);
    auto outcome = engine.inspect(scene, pieceId.value());
    ASSERT_TRUE(outcome.isOk());

    ASSERT_EQ(outcome.value().toolResults.size(), 1U) << "sin declararlo, una sola pieza";
    EXPECT_EQ(outcome.value().toolResults.front().pieceIndex, 0);
    EXPECT_EQ(outcome.value().verdict.tools.front().name, "ancho")
        << "sin varias piezas, el nombre no se decora";
    EXPECT_FALSE(outcome.value().verdict.count.evaluated);
}

TEST_F(EngineTest, TheHistoryRemembersWhichPieceEachMeasurementCameFrom) {
    auto& pieces = *pieces_;
    auto& tools = *tools_;
    auto& history = *history_;
    auto pieceId = pieces.createPiece("bandeja");
    ASSERT_TRUE(pieceId.isOk());
    auto measurement = pieces.loadMeasurement(pieceId.value());
    ASSERT_TRUE(measurement.isOk());
    measurement.value().expectedPieces = 3;
    ASSERT_TRUE(pieces.saveMeasurement(pieceId.value(), measurement.value()).isOk());

    inspection::ToolConfig caliper;
    caliper.type = inspection::ToolType::Caliper;
    caliper.name = "ancho";
    caliper.geometryJson = inspection::toJson(inspection::ToolGeometry(
        inspection::CaliperGeometry{{-160.0F, 0.0F}, {160.0F, 0.0F}, 20.0F}));
    caliper.toleranceMin = 0.0;
    caliper.toleranceMax = 1e9;
    ASSERT_TRUE(tools.save(pieceId.value(), caliper, "principal").isOk());

    // Motor propio sin autoOrient: las barras son simetricas y su eje
    // principal podria voltear entre ejecuciones, que no es lo que se prueba.
    engine::InspectionEngine engine(nullptr, pieces, tools, history);
    cv::Mat scene;
    cv::cvtColor(trayOfBars(-1, 200), scene, cv::COLOR_GRAY2BGR);
    auto outcome = engine.inspect(scene, pieceId.value());
    ASSERT_TRUE(outcome.isOk());
    ASSERT_GE(outcome.value().historyId, 0) << outcome.value().persistError;

    // Las tres medidas quedan guardadas, cada una con su indice de pieza.
    auto stmt = db_->prepare(
        "SELECT piece_index FROM ToolResults WHERE inspection_id = ? ORDER BY piece_index;");
    ASSERT_TRUE(stmt.isOk());
    ASSERT_TRUE(stmt.value().bindInt(1, outcome.value().historyId).isOk());
    std::vector<int> stored;
    while (true) {
        auto row = stmt.value().step();
        ASSERT_TRUE(row.isOk());
        if (!row.value()) {
            break;
        }
        stored.push_back(stmt.value().columnInt(0));
    }
    EXPECT_EQ(stored, (std::vector<int>{0, 1, 2}));
}

// EL MOTIVO DE CADA RECHAZO, que es lo que convierte el historial en
// información.
//
// `recentForPiece` devuelve fecha y veredicto, así que un turno con 47 rechazos
// es un número. El informe de turno necesita saber POR QUÉ, y eso hay que
// traerlo de dos sitios distintos —la herramienta que falló y el tipo de
// comprobación que falló— sin hacer una consulta por inspección.
TEST_F(EngineTest, TheReportBringsTheReasonOfEachReject) {
    auto pieceId = pieces_->createPiece("brida");
    ASSERT_TRUE(pieceId.isOk());

    // Una herramienta de verdad, para que su NOMBRE pueda salir como motivo.
    inspection::ToolConfig caliper;
    caliper.type = inspection::ToolType::Caliper;
    caliper.name = "diámetro exterior";
    caliper.geometryJson = inspection::toJson(
        inspection::ToolGeometry(inspection::CaliperGeometry{{0, 0}, {40, 0}, 6.0F}));
    const auto toolId = tools_->save(pieceId.value(), caliper);
    ASSERT_TRUE(toolId.isOk()) << toolId.error().message;

    const auto runResult = [&](bool ok) {
        inspection::ToolRunResult result;
        result.toolId = toolId.value();
        result.name = caliper.name;
        result.ok = ok;
        result.measured = ok ? 40.0 : 44.0;
        result.detail = ok ? "dentro" : "44,0 mm (max 42,0)";
        return result;
    };

    domain::InspectionVerdict good;
    good.ok = true;
    good.summary = "OK";

    // Un rechazo POR HERRAMIENTA.
    domain::InspectionVerdict byTool;
    byTool.ok = false;
    byTool.summary = "NG";

    // Y otro por APARIENCIA, sin herramienta fallida.
    domain::InspectionVerdict byLook;
    byLook.ok = false;
    byLook.summary = "NG";
    byLook.embedding.evaluated = true;
    byLook.embedding.anomalous = true;

    ASSERT_TRUE(history_->saveInspection(pieceId.value(), 1, good, {runResult(true)}, {}).isOk());
    ASSERT_TRUE(
        history_->saveInspection(pieceId.value(), 1, byTool, {runResult(false)}, {}).isOk());
    ASSERT_TRUE(history_->saveInspection(pieceId.value(), 1, byLook, {}, {}).isOk());

    auto report = history_->reportForPiece(pieceId.value());
    ASSERT_TRUE(report.isOk()) << report.error().message;
    ASSERT_EQ(report.value().size(), 3U);
    for (const auto& row : report.value()) {
        std::printf("  [informe] %s  %-3s  motivo: %s\n", row.startedAt.c_str(),
                    row.verdict.c_str(), row.reason.empty() ? "(pasó)" : row.reason.c_str());
    }

    // La que pasó no lleva motivo: un informe que le pone un motivo a cada fila
    // no distingue las buenas de las malas de un vistazo.
    EXPECT_TRUE(report.value()[0].reason.empty());

    // El rechazo por herramienta se nombra CON LA HERRAMIENTA, no con su
    // detalle. El detalle lleva la medida —«44,0 mm»— y con eso cada rechazo
    // sería un motivo distinto y no se agruparían nunca.
    EXPECT_EQ(report.value()[1].reason, "diámetro exterior");
    EXPECT_EQ(report.value()[1].reason.find("44"), std::string::npos)
        << "el motivo lleva la medida dentro, así que dos rechazos de la misma "
           "herramienta contarían como motivos distintos";

    // Y el de apariencia se dice en castellano, no «embedding»: un informe que
    // pone el nombre interno obliga a saber qué es eso para leerlo.
    EXPECT_EQ(report.value()[2].reason, "apariencia");
}

// Y el informe completo, armado sobre esos datos: el resumen contesta las tres
// preguntas sin que nadie tenga que leer las filas.
TEST_F(EngineTest, TheShiftReportAnswersTheThreeQuestions) {
    auto pieceId = pieces_->createPiece("brida");
    ASSERT_TRUE(pieceId.isOk());

    inspection::ToolConfig caliper;
    caliper.type = inspection::ToolType::Caliper;
    caliper.name = "diámetro exterior";
    caliper.geometryJson = inspection::toJson(
        inspection::ToolGeometry(inspection::CaliperGeometry{{0, 0}, {40, 0}, 6.0F}));
    const auto toolId = tools_->save(pieceId.value(), caliper);
    ASSERT_TRUE(toolId.isOk());

    inspection::ToolRunResult bad;
    bad.toolId = toolId.value();
    bad.name = caliper.name;
    bad.ok = false;
    domain::InspectionVerdict good;
    good.ok = true;
    domain::InspectionVerdict ng;
    ng.ok = false;

    for (int i = 0; i < 7; ++i) {
        ASSERT_TRUE(history_->saveInspection(pieceId.value(), 1, good, {}, {}).isOk());
    }
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(history_->saveInspection(pieceId.value(), 1, ng, {bad}, {}).isOk());
    }

    auto report = history_->reportForPiece(pieceId.value());
    ASSERT_TRUE(report.isOk());
    std::vector<domain::InspectionRow> rows;
    for (const auto& entry : report.value()) {
        domain::InspectionRow row;
        row.startedAt = entry.startedAt;
        row.piece = "brida";
        row.ok = entry.verdict == "OK";
        row.similarity = entry.similarity;
        row.reason = entry.reason;
        rows.push_back(std::move(row));
    }
    const auto summary = domain::summarise(rows);
    const std::string text = domain::shiftReportText(rows, summary);
    std::printf("  [informe] parte del turno:\n%s", text.c_str());

    EXPECT_EQ(summary.total, 10);
    EXPECT_EQ(summary.ngCount, 3);
    ASSERT_FALSE(summary.reasons.empty());
    EXPECT_EQ(summary.reasons.front().reason, "diámetro exterior");
    EXPECT_EQ(summary.reasons.front().count, 3)
        << "los tres rechazos de la misma herramienta no se agruparon: el informe diría "
           "«1 vez cada uno» y no señalaría nada";
    EXPECT_NE(text.find("diámetro exterior"), std::string::npos);
}

// EL MOTOR CONSULTA TODAS LAS VARIANTES, de punta a punta.
//
// `tests/test_variants.cpp` ya mide la DECISIÓN —dos acabados en una sola media
// dejan ciega la referencia, separados la recuperan—. Lo que falta comprobar es
// el CABLEADO: que el motor carga todas las variantes de la pieza y juzga contra
// todas, y no solo contra la principal.
//
// La segunda referencia de esta prueba es una forma claramente distinta, y es a
// propósito. El embedding de este banco son cuatro medias por cuadrante y va
// L2-normalizado, así que no distingue acabados: se probó con otros niveles de
// gris (similitud 0,9999) y con un rasgo añadido (0,9994), y en los dos casos no
// había dos grupos que separar. Para comprobar el cableado hace falta algo que
// este embedding SÍ distinga, y la naturaleza de la segunda referencia no cambia
// lo que se está comprobando: que registrarla hace que lo que antes era anómalo
// se acepte, sin tocar lo que ya funcionaba.
TEST_F(EngineTest, TheEngineJudgesAgainstEveryRegisteredVariant) {
    const auto pieceId = registerLPiece();

    const auto circleFrame = [] {
        cv::Mat frame(480, 640, CV_8UC1, cv::Scalar(40));
        cv::circle(frame, {320, 240}, 70, cv::Scalar(220), cv::FILLED, cv::LINE_8);
        cv::Mat bgr;
        cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    };
    const auto lFrame = [] {
        const auto frame = drawLPiece({640, 480}, {306.0F, 236.0F}, 23.0, 40.0F, 40, 220);
        cv::Mat bgr;
        cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    };

    // Antes de registrar la segunda referencia, la otra forma sale anómala.
    auto before = engine_->inspect(circleFrame(), pieceId);
    ASSERT_TRUE(before.isOk()) << before.error().message;
    ASSERT_TRUE(before.value().verdict.embedding.evaluated)
        << "no se llegó a evaluar la apariencia: la prueba no mide lo que cree";
    std::printf("  [variantes] sin registrar: similitud %.4f, umbral %.4f -> %s\n",
                before.value().verdict.embedding.similarity,
                before.value().verdict.embedding.threshold,
                before.value().verdict.embedding.anomalous ? "ANÓMALA" : "acepta");
    ASSERT_TRUE(before.value().verdict.embedding.anomalous)
        << "ya se aceptaba antes de registrar nada: entonces esta prueba no demuestra "
           "que registrar la variante haya servido de algo";

    // El operador la registra como una variante admisible de la misma pieza.
    vision::PipelineConfig cfg;
    cfg.autoOrient = true;
    engine::RegistrationSession session(fakeEmbed, 30, 5, std::nullopt, cfg);
    for (int i = 0; i < 8; ++i) {
        const auto feedback = session.addFrame(circleFrame());
        ASSERT_TRUE(feedback.isOk()) << feedback.error().message;
        ASSERT_TRUE(feedback.value().accepted) << feedback.value().reason;
    }
    auto variantReference = session.finish();
    ASSERT_TRUE(variantReference.isOk()) << variantReference.error().message;
    ASSERT_TRUE(pieces_->saveReference(pieceId, variantReference.value(), "pulido").isOk());

    // Ahora se acepta.
    auto after = engine_->inspect(circleFrame(), pieceId);
    ASSERT_TRUE(after.isOk());
    std::printf("  [variantes] ya registrada: similitud %.4f, umbral %.4f -> %s\n",
                after.value().verdict.embedding.similarity,
                after.value().verdict.embedding.threshold,
                after.value().verdict.embedding.anomalous ? "ANÓMALA" : "acepta");
    EXPECT_FALSE(after.value().verdict.embedding.anomalous)
        << "registrarla como variante no cambió nada: el motor sigue juzgando solo "
           "contra la principal";

    // Y el umbral que se enseña es el de la variante que decidió, no el de la
    // principal: si no, el informe pondría una cifra que no es la que se usó.
    EXPECT_NE(after.value().verdict.embedding.threshold,
              before.value().verdict.embedding.threshold)
        << "el umbral del informe sigue siendo el de la principal aunque haya decidido "
           "otra variante";

    // Lo que ya funcionaba sigue funcionando: añadir una variante no puede
    // romper el acabado que estaba registrado.
    auto original = engine_->inspect(lFrame(), pieceId);
    ASSERT_TRUE(original.isOk());
    EXPECT_FALSE(original.value().verdict.embedding.anomalous)
        << "añadir una variante rompió la pieza que ya se reconocía";
}

// EL INFORME DE TURNO TIENE QUE SER DE UN TURNO.
//
// Se escribió con un tope de 2 000 filas y sin filtro de fechas, y las dos cosas
// juntas lo rompían de una forma que no se ve: un turno de ocho horas a cinco
// segundos por pieza son 5 760 inspecciones, así que el tope se quedaba corto
// para el caso normal — y como la consulta ordenaba de más antigua a más nueva
// ANTES de recortar, lo que salía eran las 2 000 más VIEJAS de la pieza.
//
// O sea que a una pieza con historial le daba un informe de hace meses y lo
// titulaba «turno». No fallaba, no avisaba: daba un rendimiento creíble de un
// periodo que no era el que se pedía.
TEST_F(EngineTest, TheShiftReportCoversTheShiftAndNotTheOldestRows) {
    auto pieceId = pieces_->createPiece("brida");
    ASSERT_TRUE(pieceId.isOk());

    domain::InspectionVerdict good;
    good.ok = true;
    domain::InspectionVerdict bad;
    bad.ok = false;

    // Se escriben las fechas a mano para poder distinguir «lo viejo» de «hoy».
    const auto saveAt = [&](const std::string& when, bool ok) {
        ASSERT_TRUE(history_->saveInspection(pieceId.value(), 1, ok ? good : bad, {}, {})
                        .isOk());
        auto stmt = db_->prepare(
            "UPDATE InspectionHistory SET started_at = ? WHERE id = "
            "(SELECT MAX(id) FROM InspectionHistory);");
        ASSERT_TRUE(stmt.isOk());
        ASSERT_TRUE(stmt.value().bindText(1, when).isOk());
        ASSERT_TRUE(stmt.value().step().isOk());
    };

    // Cinco de hace un mes, todas buenas. Y tres de hoy, todas malas.
    for (int i = 0; i < 5; ++i) {
        saveAt("2026-07-15 08:0" + std::to_string(i) + ":00", true);
    }
    for (int i = 0; i < 3; ++i) {
        saveAt("2026-08-23 14:0" + std::to_string(i) + ":00", false);
    }

    // CON EL TOPE EN 3, lo que tiene que salir son las TRES DE HOY. Con el orden
    // ascendente de antes salían las tres de julio, y el informe habría dicho
    // «rendimiento 100 %» de un turno en el que no pasó ninguna.
    int discarded = -1;
    auto recent = history_->reportForPiece(pieceId.value(), {}, 3, &discarded);
    ASSERT_TRUE(recent.isOk()) << recent.error().message;
    ASSERT_EQ(recent.value().size(), 3U);
    std::printf("  [informe] con tope 3, la primera es %s y la ultima %s (descartadas %d)\n",
                recent.value().front().startedAt.c_str(),
                recent.value().back().startedAt.c_str(), discarded);
    EXPECT_EQ(recent.value().front().startedAt.substr(0, 10), "2026-08-23")
        << "el informe trae las inspecciones más VIEJAS de la pieza en vez de las "
           "últimas: el rendimiento que daría es el de otro día";
    EXPECT_EQ(discarded, 5) << "se recortaron cinco y no se dijo";

    // Y en orden cronológico, que es como se lee.
    EXPECT_LT(recent.value().front().startedAt, recent.value().back().startedAt);

    // CON UN PERIODO, solo ese periodo — que es lo que hace que «informe del
    // turno» signifique algo.
    auto today = history_->reportForPiece(
        pieceId.value(), {"2026-08-23 00:00:00", "2026-08-23 23:59:59"});
    ASSERT_TRUE(today.isOk());
    EXPECT_EQ(today.value().size(), 3U)
        << "el filtro de fechas no acota: el informe sigue trayendo el historial entero";
    for (const auto& row : today.value()) {
        EXPECT_EQ(row.startedAt.substr(0, 10), "2026-08-23");
    }

    auto july = history_->reportForPiece(
        pieceId.value(), {"2026-07-01 00:00:00", "2026-07-31 23:59:59"});
    ASSERT_TRUE(july.isOk());
    EXPECT_EQ(july.value().size(), 5U);

    // Sin periodo y sin tope apretado, salen las ocho.
    auto everything = history_->reportForPiece(pieceId.value());
    ASSERT_TRUE(everything.isOk());
    EXPECT_EQ(everything.value().size(), 8U);
}

// Y el tope nuevo cubre un turno de verdad, con margen.
TEST_F(EngineTest, TheReportLimitCoversARealShift) {
    // Ocho horas a cinco segundos por pieza son 5 760 inspecciones. El tope
    // anterior eran 2 000, o sea que se quedaba corto para el caso NORMAL.
    constexpr int kEightHourShift = 8 * 3600 / 5;
    std::printf("  [informe] un turno de 8 h a 5 s/pieza son %d inspecciones; el tope es "
                "%d\n",
                kEightHourShift,
                repositories::InspectionRepository::kMaxReportRows);
    EXPECT_GT(repositories::InspectionRepository::kMaxReportRows, kEightHourShift)
        << "el tope del informe no cubre ni un turno de ocho horas";
    // Y un día de tres turnos.
    EXPECT_GT(repositories::InspectionRepository::kMaxReportRows, 3 * kEightHourShift)
        << "no cubre un día de tres turnos, que es lo que se pide al cerrar la jornada";
}
