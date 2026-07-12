#pragma once

#include <opencv2/core.hpp>

#include <optional>
#include <string>

#include "core/result.h"
#include "domain/capture_quality.h"
#include "engine/embed_fn.h"
#include "ml/reference.h"
#include "vision/orientation_anchor.h"
#include "vision/pipeline.h"

namespace pci::engine {

// Sesión de registro guiado de una pieza: valida la calidad de cada frame,
// extrae su embedding y lo acumula (Welford). No guarda imágenes: solo el
// primer recorte normalizado (para miniatura/plantilla) y la aritmética.
class RegistrationSession {
public:
    // embedFn debe ser válida: el registro ES la referencia de embeddings.
    // anchor (opcional): rasgo distintivo que fija la orientación de todas
    // las capturas — imprescindible para piezas simétricas.
    // pipelineConfig: mismos ajustes de detección que usará la inspección.
    RegistrationSession(EmbedFn embedFn, int targetCount = 30, int minimumCount = 5,
                        std::optional<vision::OrientationAnchor> anchor = std::nullopt,
                        vision::PipelineConfig pipelineConfig = {});

    struct SampleFeedback {
        bool accepted = false;
        std::string reason;  // motivo del rechazo (vacío si fue aceptada)
        int count = 0;
        domain::QualityMetrics metrics;
    };

    // Procesa un frame candidato (síncrono; llamar desde hilo de trabajo).
    core::Result<SampleFeedback> addFrame(const cv::Mat& frameBgr);

    [[nodiscard]] int count() const { return builder_.count(); }
    [[nodiscard]] int target() const { return targetCount_; }
    [[nodiscard]] int minimum() const { return minimumCount_; }
    [[nodiscard]] bool readyToFinish() const { return builder_.count() >= minimumCount_; }
    [[nodiscard]] const cv::Mat& firstNormalized() const { return firstNormalized_; }

    core::Result<ml::Reference> finish() const;

private:
    EmbedFn embedFn_;
    int targetCount_;
    int minimumCount_;
    std::optional<vision::OrientationAnchor> anchor_;
    vision::PipelineConfig pipelineConfig_;
    ml::ReferenceBuilder builder_;
    cv::Mat firstNormalized_;
};

}  // namespace pci::engine
