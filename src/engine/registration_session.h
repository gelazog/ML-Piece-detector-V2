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
    // embedFn PUEDE ser nula (G1): sin modelo ONNX la sesión funciona en modo
    // "solo herramientas" — valida la calidad de cada frame y captura el
    // recorte para la miniatura, pero no construye referencia de apariencia.
    // La pieza queda registrada y se inspecciona solo con las herramientas.
    // anchor (opcional): rasgo distintivo que fija la orientación de todas
    // las capturas — imprescindible para piezas simétricas.
    // pipelineConfig: mismos ajustes de detección que usará la inspección.
    RegistrationSession(EmbedFn embedFn, int targetCount = 30, int minimumCount = 5,
                        std::optional<vision::OrientationAnchor> anchor = std::nullopt,
                        vision::PipelineConfig pipelineConfig = {},
                        double orientationOffsetDeg = 0.0);

    struct SampleFeedback {
        bool accepted = false;
        std::string reason;  // motivo del rechazo (vacío si fue aceptada)
        int count = 0;
        domain::QualityMetrics metrics;
    };

    // Procesa un frame candidato (síncrono; llamar desde hilo de trabajo).
    core::Result<SampleFeedback> addFrame(const cv::Mat& frameBgr);

    // Capturas aceptadas (con o sin embeddings, según el modo).
    [[nodiscard]] int count() const { return embedFn_ ? builder_.count() : accepted_; }
    // true = sin modelo: no habrá comparación de apariencia para esta pieza.
    [[nodiscard]] bool toolsOnly() const { return !embedFn_; }
    [[nodiscard]] int target() const { return targetCount_; }
    [[nodiscard]] int minimum() const { return minimumCount_; }
    [[nodiscard]] bool readyToFinish() const { return count() >= minimumCount_; }
    [[nodiscard]] const cv::Mat& firstNormalized() const { return firstNormalized_; }

    // En modo "solo herramientas" devuelve una referencia VACÍA (mean vacío):
    // quien la reciba debe NO guardarla, para que la inspección sepa que esta
    // pieza no tiene apariencia con la que comparar.
    core::Result<ml::Reference> finish() const;

private:
    EmbedFn embedFn_;
    int targetCount_;
    int minimumCount_;
    std::optional<vision::OrientationAnchor> anchor_;
    vision::PipelineConfig pipelineConfig_;
    double orientationOffsetDeg_ = 0.0;
    ml::ReferenceBuilder builder_;
    int accepted_ = 0;  // capturas válidas en modo "solo herramientas"
    cv::Mat firstNormalized_;
};

}  // namespace pci::engine
