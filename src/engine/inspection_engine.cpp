#include "engine/inspection_engine.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <utility>

#include "core/logging.h"
#include "ml/reference.h"
#include "vision/orientation_anchor.h"
#include "vision/pipeline.h"

namespace pci::engine {

std::vector<unsigned char> encodeThumbnailJpeg(const cv::Mat& image, int size, int quality) {
    if (image.empty() || size <= 0) {
        return {};
    }
    cv::Mat small;
    cv::resize(image, small, cv::Size(size, size), 0.0, 0.0, cv::INTER_AREA);
    std::vector<unsigned char> jpeg;
    cv::imencode(".jpg", small, jpeg, {cv::IMWRITE_JPEG_QUALITY, quality});
    return jpeg;
}

InspectionEngine::InspectionEngine(EmbedFn embedFn, repositories::PieceRepository& pieces,
                                   repositories::ToolRepository& tools,
                                   repositories::InspectionRepository& history,
                                   EngineOptions options)
    : embedFn_(std::move(embedFn)), pieces_(pieces), tools_(tools), history_(history),
      options_(options) {}

core::Result<InspectionEngine::Outcome> InspectionEngine::inspect(const cv::Mat& frameBgr,
                                                                  std::int64_t pieceId) {
    using ResultT = core::Result<Outcome>;

    auto analysis = vision::analyzeFrame(frameBgr, options_.pipeline);
    if (!analysis.isOk()) {
        return ResultT::err("No se pudo analizar el frame: " + analysis.error().message);
    }

    // Rasgo distintivo de la pieza (si tiene): fija la orientación aunque la
    // pieza sea simétrica o llegue girada 180°.
    if (auto anchor = pieces_.loadAnchor(pieceId); anchor.isOk() && anchor.value()) {
        if (auto applied =
                vision::applyAnchor(frameBgr, *anchor.value(), analysis.value());
            !applied.isOk()) {
            core::logWarning("No se pudo aplicar el rasgo distintivo: " +
                             applied.error().message);
        }
    }

    // Ajuste manual de orientación de la pieza (0 = usar la detectada).
    if (auto offset = pieces_.loadOrientationOffset(pieceId); offset.isOk()) {
        if (auto applied =
                vision::applyOrientationOffset(frameBgr, offset.value(), analysis.value());
            !applied.isOk()) {
            core::logWarning("No se pudo aplicar el ajuste de orientación: " +
                             applied.error().message);
        }
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
    if (auto listed = tools_.listForPiece(pieceId, options_.templateName); listed.isOk()) {
        toolConfigs = std::move(listed.value());
    } else {
        core::logWarning("No se pudieron cargar las herramientas: " +
                         listed.error().message);
    }
    outcome.toolResults = inspection::runTools(
        frameBgr, outcome.analysis.fixture, toolConfigs, options_.mmPerPixel, options_.unit);

    std::vector<domain::ToolCheck> toolChecks;
    toolChecks.reserve(outcome.toolResults.size());
    for (const auto& result : outcome.toolResults) {
        toolChecks.push_back({result.name, result.ok, result.measured, result.detail});
    }

    // 3. Veredicto combinado (lógica pura de domain/).
    outcome.verdict = domain::combineVerdict(check, toolChecks);

    // 4. Historial + estadísticas (fallo de BD = avisado, nunca oculta el
    //    veredicto ni tumba la inspección).
    const std::vector<unsigned char> thumbnail =
        encodeThumbnailJpeg(outcome.analysis.normalized, options_.thumbnailSize);
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
