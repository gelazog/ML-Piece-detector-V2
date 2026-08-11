#include "engine/inspection_engine.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <utility>

#include "core/logging.h"
#include "domain/measurement_mode.h"
#include "ml/reference.h"
#include "vision/orientation_anchor.h"
#include "vision/pipeline.h"

namespace pci::engine {

namespace {

// Por debajo de esta anisotropía el eje principal de la pieza no está definido
// (pieza casi redonda o simétrica): el giro medido salta y no se puede usar
// para dar NG. Mismo criterio que usa el estabilizador de fixture.
constexpr double kMinAnisotropyForAngle = 0.15;

}  // namespace

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
    // pieza sea simétrica o llegue girada 180°. Solo si se sigue la rotación.
    if (options_.pipeline.autoOrient) {
        if (auto anchor = pieces_.loadAnchor(pieceId); anchor.isOk() && anchor.value()) {
            if (auto applied =
                    vision::applyAnchor(frameBgr, *anchor.value(), analysis.value());
                !applied.isOk()) {
                core::logWarning("No se pudo aplicar el rasgo distintivo: " +
                                 applied.error().message);
            }
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
            const auto& storedReference = stored.value().reference;
            if (storedReference.mean.empty()) {
                // Pieza registrada en modo "solo herramientas" (G1): no hay
                // apariencia con la que comparar. Sin esta guarda, la similitud
                // contra un vector vacío sería 0 y todo saldría NG.
                check.note = "pieza registrada sin apariencia (solo herramientas)";
            } else if (auto embedding = embedFn_(outcome.analysis.normalized);
                       !embedding.isOk()) {
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

    // 1b. Piezas adicionales (C5/C6). Se buscan una sola vez y sirven para las
    //     dos cosas: contar y medir. La pieza 0 sigue siendo `outcome.analysis`
    //     —la que ya lleva aplicado el rasgo distintivo y el giro—, así que
    //     inspeccionar de una en una da exactamente lo mismo que antes.
    int expectedPieces = 1;
    if (auto measurement = pieces_.loadMeasurement(pieceId); measurement.isOk()) {
        expectedPieces = measurement.value().expectedPieces;
    }
    std::vector<vision::Fixture> extraFixtures;
    if (expectedPieces > 1) {
        if (auto all = vision::analyzeFrames(frameBgr, options_.pipeline); all.isOk()) {
            outcome.piecesFound = static_cast<int>(all.value().size());
            for (std::size_t i = 1; i < all.value().size(); ++i) {
                extraFixtures.push_back(all.value()[i].fixture);
            }
        } else {
            outcome.piecesFound = 0;
        }
    }
    outcome.pieceFixtures.push_back(outcome.analysis.fixture);
    for (const auto& fixture : extraFixtures) {
        outcome.pieceFixtures.push_back(fixture);
    }

    // 2. Herramientas geométricas sobre la imagen original (sin warp).
    std::vector<inspection::ToolConfig> toolConfigs;
    if (auto listed = tools_.listForPiece(pieceId, options_.templateName); listed.isOk()) {
        toolConfigs = std::move(listed.value());
    } else {
        core::logWarning("No se pudieron cargar las herramientas: " +
                         listed.error().message);
    }
    // El tablero se resuelve con el fixture de ESTA inspección: las
    // herramientas de Posición se juzgan contra el mismo cero que ve el
    // operador en vivo.
    const cv::Point2f boundsCenter = outcome.analysis.contour.rotatedRect.center;
    const vision::BoardFrame board = vision::resolveBoardFrame(
        options_.board, outcome.analysis.fixture, true,
        cv::Size(frameBgr.cols, frameBgr.rows), &boundsCenter);
    outcome.toolResults =
        inspection::runTools(frameBgr, outcome.analysis.fixture, toolConfigs,
                             options_.mmPerPixel, options_.unit, cv::Mat(), &board);

    // Las demás piezas se miden con LA MISMA plantilla. Sale casi gratis porque
    // las herramientas viven en coordenadas de pieza: cambiar de pieza es
    // cambiar de fixture y volver a ejecutar, sin tocar ni una herramienta.
    for (std::size_t i = 0; i < extraFixtures.size(); ++i) {
        const vision::BoardFrame pieceBoard = vision::resolveBoardFrame(
            options_.board, extraFixtures[i], true,
            cv::Size(frameBgr.cols, frameBgr.rows));
        auto results =
            inspection::runTools(frameBgr, extraFixtures[i], toolConfigs,
                                 options_.mmPerPixel, options_.unit, cv::Mat(), &pieceBoard);
        for (auto& result : results) {
            result.pieceIndex = static_cast<int>(i) + 1;
            outcome.toolResults.push_back(std::move(result));
        }
    }

    std::vector<domain::ToolCheck> toolChecks;
    toolChecks.reserve(outcome.toolResults.size());
    for (const auto& result : outcome.toolResults) {
        // Con varias piezas, el nombre lleva de cuál es. Sin eso, un NG diría
        // "Ø agujero fuera de tolerancia" y habría que adivinar en qué tornillo
        // de la bandeja mirar.
        const std::string name =
            result.pieceIndex > 0
                ? result.name + " (pieza " + std::to_string(result.pieceIndex + 1) + ")"
                : result.name;
        toolChecks.push_back({name, result.ok, result.measured, result.detail});
    }

    // 2b. Recuento de piezas (C5). Solo se cuenta si la pieza lo pide: buscar
    //     todas las piezas del frame cuesta, y quien inspecciona de una en una
    //     no tiene por qué pagarlo. `expectedPieces <= 1` es el caso de
    //     siempre y ni siquiera entra aquí.
    const domain::CountCheck countCheck =
        expectedPieces > 1 ? domain::evaluatePieceCount(expectedPieces, outcome.piecesFound)
                           : domain::CountCheck{};

    // 3. Reglas del modo Especial (M4): centrado y giro respecto al tablero.
    //    Solo se evalúan si la pieza está en modo Especial y con tolerancias
    //    configuradas; el eje de una pieza casi simétrica no es de fiar, así que
    //    la anisotropía decide si el giro se juzga o se deja pasar con nota.
    domain::PositionCheck positionCheck;
    if (auto measurement = pieces_.loadMeasurement(pieceId);
        measurement.isOk() &&
        measurement.value().mode == domain::MeasurementMode::Special) {
        const vision::BoardReading reading =
            vision::readPiece(board, outcome.analysis.fixture);
        const double angleOffset =
            vision::pieceAngleOffset(board, outcome.analysis.fixture);
        positionCheck = domain::evaluatePosition(
            reading.radius, measurement.value().maxOffsetPx, angleOffset,
            measurement.value().maxAngleDeg,
            outcome.analysis.fixture.anisotropy >= kMinAnisotropyForAngle);
    }

    // 4. Veredicto combinado (lógica pura de domain/).
    outcome.verdict = domain::combineVerdict(check, toolChecks, positionCheck, countCheck);

    // 5. Historial + estadísticas (fallo de BD = avisado, nunca oculta el
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
