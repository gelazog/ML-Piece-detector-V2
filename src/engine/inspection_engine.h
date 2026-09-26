#pragma once

#include <mutex>
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
#include "vision/board_frame.h"
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
    // Tablero de referencia con el que se juzgan las herramientas de Posición.
    // Debe ser el mismo que ve el operador para que la inspección coincida con
    // la lectura en vivo.
    vision::BoardConfig board;
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
        // Piezas encontradas en el frame (C5/C6). `analysis` es siempre la
        // mayor; si se esperan varias, `toolResults` trae las medidas de todas,
        // cada una con su `pieceIndex`, y `pieceFixtures` sus fixtures para
        // poder dibujarlas.
        int piecesFound = 1;
        std::vector<vision::Fixture> pieceFixtures;
    };

    // Síncrono (inferencia incluida): llamar desde un hilo de trabajo.
    core::Result<Outcome> inspect(const cv::Mat& frameBgr, std::int64_t pieceId);

    // Aprendizaje incremental: continúa la referencia vigente con el embedding
    // de una inspección confirmada como correcta y guarda una versión nueva.
    core::Result<int> updateReference(std::int64_t pieceId,
                                      const std::vector<float>& embedding);

    // Ajustes de detección (umbral, polaridad, zona), sensibilidad, tablero…
    //
    // SE PUEDEN LLAMAR CON UNA INSPECCIÓN EN VUELO. Antes la cabecera pedía no
    // hacerlo («la UI garantiza un solo vuelo a la vez») y la UI no lo
    // garantizaba: `setBoardConfig` y `setKSigma` se llamaban desde la ventana
    // mientras la auto-inspección leía estas mismas opciones en el hilo de
    // trabajo, y «Aprender de esta captura» lanzaba otra inspección síncrona en
    // el hilo de la interfaz a la vez. Leer un `std::string` o un `BoardConfig`
    // mientras otro hilo lo reescribe es comportamiento indefinido, no una
    // lectura vieja.
    //
    // Ahora las opciones van tras un mutex y `inspect` trabaja con una COPIA
    // tomada al empezar: una inspección ve los ajustes de un solo instante, y un
    // cambio a mitad de camino vale para la siguiente.
    void setPipelineConfig(const vision::PipelineConfig& config) {
        const std::lock_guard<std::mutex> lock(optionsMutex_);
        options_.pipeline = config;
    }
    void setMmPerPixel(double mmPerPixel) {
        const std::lock_guard<std::mutex> lock(optionsMutex_);
        options_.mmPerPixel = mmPerPixel;
    }
    void setUnit(inspection::LengthUnit unit) {
        const std::lock_guard<std::mutex> lock(optionsMutex_);
        options_.unit = unit;
    }
    void setTemplateName(const std::string& name) {
        const std::lock_guard<std::mutex> lock(optionsMutex_);
        options_.templateName = name;
    }
    // Sensibilidad de anomalía de apariencia (Preferencias, O1).
    void setKSigma(double kSigma) {
        const std::lock_guard<std::mutex> lock(optionsMutex_);
        options_.kSigma = kSigma;
    }
    void setBoardConfig(const vision::BoardConfig& board) {
        const std::lock_guard<std::mutex> lock(optionsMutex_);
        options_.board = board;
    }

    // Las opciones de este instante, copiadas. Es lo que usa `inspect`.
    [[nodiscard]] EngineOptions options() const {
        const std::lock_guard<std::mutex> lock(optionsMutex_);
        return options_;
    }

private:
    EmbedFn embedFn_;
    repositories::PieceRepository& pieces_;
    repositories::ToolRepository& tools_;
    repositories::InspectionRepository& history_;
    mutable std::mutex optionsMutex_;
    EngineOptions options_;
};

}  // namespace pci::engine
