#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

#include "core/result.h"
#include "domain/verdict.h"
#include "engine/embed_fn.h"
#include "inspection_editor/execution/tool_executor.h"
#include "repositories/inspection_repository.h"
#include "repositories/piece_repository.h"
#include "repositories/tool_repository.h"
#include "vision/pipeline.h"
#include "vision/types.h"

namespace pci::engine {

struct EngineOptions {
    double kSigma = 3.0;     // banda de anomalía: simMean - max(k·σ, 0.02)
    int thumbnailSize = 96;  // miniatura JPEG guardada en el historial
    vision::PipelineConfig pipeline;  // detección: umbral, polaridad, zona
    double mmPerPixel = 0.0;          // escala calibrada para los detalles
    inspection::LengthUnit unit = inspection::LengthUnit::Auto;
    std::string templateName = "principal";  // plantilla de herramientas activa
};

// Miniatura JPEG cuadrada de una imagen (BGR o gris); vacía si la imagen lo es.
std::vector<unsigned char> encodeThumbnailJpeg(const cv::Mat& image, int size = 128,
                                               int quality = 80);

// Inspección completa de un frame contra una pieza registrada: apariencia por
// embeddings + herramientas geométricas + persistencia de historial. También
// ejecuta el aprendizaje incremental (nueva versión de referencia).
class InspectionEngine {
public:
    // embedFn puede ser nula: se inspecciona solo con herramientas (avisado
    // en el veredicto). Los repositorios deben sobrevivir al engine.
    InspectionEngine(EmbedFn embedFn, repositories::PieceRepository& pieces,
                     repositories::ToolRepository& tools,
                     repositories::InspectionRepository& history,
                     EngineOptions options = {});

    struct Outcome {
        domain::InspectionVerdict verdict;
        int referenceVersion = 0;
        std::int64_t historyId = -1;      // -1 si no se pudo persistir
        std::string persistError;         // motivo si historyId == -1
        std::vector<float> embedding;     // para "actualizar referencia"
        vision::PieceAnalysis analysis;   // para overlay
        std::vector<inspection::ToolRunResult> toolResults;
    };

    // Síncrono (inferencia incluida): llamar desde un hilo de trabajo.
    core::Result<Outcome> inspect(const cv::Mat& frameBgr, std::int64_t pieceId);

    // Aprendizaje incremental: continúa la referencia vigente con el embedding
    // de una inspección confirmada como correcta y guarda una versión nueva.
    core::Result<int> updateReference(std::int64_t pieceId,
                                      const std::vector<float>& embedding);

    // Ajustes de detección (umbral, polaridad, zona). Llamar solo sin una
    // inspección en vuelo (la UI garantiza un solo vuelo a la vez).
    void setPipelineConfig(const vision::PipelineConfig& config) {
        options_.pipeline = config;
    }
    void setMmPerPixel(double mmPerPixel) { options_.mmPerPixel = mmPerPixel; }
    void setUnit(inspection::LengthUnit unit) { options_.unit = unit; }
    void setTemplateName(const std::string& name) { options_.templateName = name; }

private:
    EmbedFn embedFn_;
    repositories::PieceRepository& pieces_;
    repositories::ToolRepository& tools_;
    repositories::InspectionRepository& history_;
    EngineOptions options_;
};

}  // namespace pci::engine
