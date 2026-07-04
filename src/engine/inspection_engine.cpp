#include "engine/inspection_engine.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <utility>

#include "core/logging.h"
#include "ml/reference.h"
#include "vision/pipeline.h"

namespace pci::engine {

InspectionEngine::InspectionEngine(EmbedFn embedFn, repositories::PieceRepository& pieces,
                                   repositories::ToolRepository& tools,
                                   repositories::InspectionRepository& history,
                                   EngineOptions options)
    : embedFn_(std::move(embedFn)), pieces_(pieces), tools_(tools), history_(history),
      options_(options) {}

core::Result<InspectionEngine::Outcome> InspectionEngine::inspect(const cv::Mat& frameBgr,
                                                                  std::int64_t pieceId) {
    using ResultT = core::Result<Outcome>;

    auto analysis = vision::analyzeFrame(frameBgr);
    if (!analysis.isOk()) {
        return ResultT::err("No se pudo analizar el frame: " + analysis.error().message);
    }

    Outcome outcome;
    outcome.analysis = std::move(analysis.value());

    // 1. Apariencia por embeddings (si hay modelo y referencia guardada).
    domain::EmbeddingCheck check;
    if (!embedFn_) {
        check.note = "modelo de embeddings no disponible";
    } else {
        auto stored = pieces_.loadLatestReference(pieceId);
        if (!stored.isOk()) {
            check.note = stored.error().message;
        } else {
            auto embedding = embedFn_(outcome.analysis.normalized);
            if (!embedding.isOk()) {
                check.note = embedding.error().message;
            } else {
                const auto& reference = stored.value().reference;
                outcome.embedding = std::move(embedding.value());
                outcome.referenceVersion = stored.value().version;
                check.evaluated = true;
                check.similarity = ml::cosineSimilarity(outcome.embedding, reference.mean);
                check.threshold =
                    reference.simMean -
                    std::max(options_.kSigma * reference.simStd, 0.02);
                check.anomalous =
                    ml::isAnomalous(outcome.embedding, reference, options_.kSigma);
            }
        }
    }

    // 2. Herramientas geométricas sobre la imagen original (sin warp).
    std::vector<inspection::ToolConfig> toolConfigs;
    if (auto listed = tools_.listForPiece(pieceId); listed.isOk()) {
        toolConfigs = std::move(listed.value());
    } else {
        core::logWarning("No se pudieron cargar las herramientas: " +
                         listed.error().message);
    }
    outcome.toolResults = inspection::runTools(frameBgr, outcome.analysis.fixture,
                                               toolConfigs);

    std::vector<domain::ToolCheck> toolChecks;
    toolChecks.reserve(outcome.toolResults.size());
    for (const auto& result : outcome.toolResults) {
        toolChecks.push_back({result.name, result.ok, result.measured, result.detail});
    }

    // 3. Veredicto combinado (lógica pura de domain/).
    outcome.verdict = domain::combineVerdict(check, toolChecks);

    // 4. Historial + estadísticas (fallo de BD = avisado, nunca oculta el
    //    veredicto ni tumba la inspección).
    std::vector<unsigned char> thumbnail;
    if (!outcome.analysis.normalized.empty()) {
        cv::Mat small;
        cv::resize(outcome.analysis.normalized, small,
                   cv::Size(options_.thumbnailSize, options_.thumbnailSize), 0.0, 0.0,
                   cv::INTER_AREA);
        cv::imencode(".jpg", small, thumbnail, {cv::IMWRITE_JPEG_QUALITY, 80});
    }
    auto saved = history_.saveInspection(pieceId, outcome.referenceVersion, outcome.verdict,
                                         outcome.toolResults, thumbnail);
    if (saved.isOk()) {
        outcome.historyId = saved.value();
    } else {
        outcome.persistError = saved.error().message;
        core::logError(outcome.persistError);
    }

    return ResultT::ok(std::move(outcome));
}

core::Result<int> InspectionEngine::updateReference(std::int64_t pieceId,
                                                    const std::vector<float>& embedding) {
    using ResultT = core::Result<int>;

    if (embedding.empty()) {
        return ResultT::err("No hay embedding de la inspección para incorporar");
    }

    auto stored = pieces_.loadLatestReference(pieceId);
    if (!stored.isOk()) {
        return ResultT::err(stored.error().message);
    }

    ml::ReferenceBuilder builder(stored.value().reference);
    if (auto added = builder.add(embedding); !added.isOk()) {
        return ResultT::err(added.error().message);
    }
    auto updated = builder.build();
    if (!updated.isOk()) {
        return ResultT::err(updated.error().message);
    }

    auto version = pieces_.saveReference(pieceId, updated.value());
    if (!version.isOk()) {
        return ResultT::err(version.error().message);
    }
    core::logInfo("Referencia de pieza " + std::to_string(pieceId) +
                  " actualizada a versión " + std::to_string(version.value()));
    return version;
}

}  // namespace pci::engine
