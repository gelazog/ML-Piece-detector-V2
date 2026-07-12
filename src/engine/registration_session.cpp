#include "engine/registration_session.h"

#include <utility>

#include "vision/pipeline.h"
#include "vision/quality_metrics.h"

namespace pci::engine {

RegistrationSession::RegistrationSession(EmbedFn embedFn, int targetCount, int minimumCount,
                                         std::optional<vision::OrientationAnchor> anchor,
                                         vision::PipelineConfig pipelineConfig,
                                         double orientationOffsetDeg)
    : embedFn_(std::move(embedFn)), targetCount_(targetCount), minimumCount_(minimumCount),
      anchor_(anchor), pipelineConfig_(std::move(pipelineConfig)),
      orientationOffsetDeg_(orientationOffsetDeg) {}

core::Result<RegistrationSession::SampleFeedback> RegistrationSession::addFrame(
    const cv::Mat& frameBgr) {
    using ResultT = core::Result<SampleFeedback>;

    if (!embedFn_) {
        return ResultT::err("Modelo de embeddings no disponible: no se puede registrar");
    }

    SampleFeedback feedback;
    auto analysis = vision::analyzeFrame(frameBgr, pipelineConfig_);
    if (analysis.isOk() && anchor_.has_value()) {
        if (auto applied = vision::applyAnchor(frameBgr, *anchor_, analysis.value());
            !applied.isOk()) {
            return ResultT::err(applied.error().message);
        }
    }
    if (analysis.isOk()) {
        if (auto applied = vision::applyOrientationOffset(frameBgr, orientationOffsetDeg_,
                                                          analysis.value());
            !applied.isOk()) {
            return ResultT::err(applied.error().message);
        }
    }
    feedback.metrics = vision::computeQualityMetrics(
        frameBgr, analysis.isOk() ? &analysis.value() : nullptr);

    if (const auto quality = domain::validateQuality(feedback.metrics); !quality.isOk()) {
        feedback.accepted = false;
        feedback.reason = quality.error().message;
        feedback.count = builder_.count();
        return ResultT::ok(std::move(feedback));
    }

    auto embedding = embedFn_(analysis.value().normalized);
    if (!embedding.isOk()) {
        return ResultT::err("Fallo extrayendo embedding: " + embedding.error().message);
    }
    if (auto added = builder_.add(embedding.value()); !added.isOk()) {
        return ResultT::err(added.error().message);
    }

    if (firstNormalized_.empty()) {
        firstNormalized_ = analysis.value().normalized.clone();
    }

    feedback.accepted = true;
    feedback.count = builder_.count();
    return ResultT::ok(std::move(feedback));
}

core::Result<ml::Reference> RegistrationSession::finish() const {
    if (builder_.count() < minimumCount_) {
        return core::Result<ml::Reference>::err(
            "Se necesitan al menos " + std::to_string(minimumCount_) +
            " capturas válidas (hay " + std::to_string(builder_.count()) + ")");
    }
    return builder_.build();
}

}  // namespace pci::engine
