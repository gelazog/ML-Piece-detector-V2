#pragma once

#include <string>
#include <vector>

namespace pci::domain {

struct EmbeddingCheck {
    bool evaluated = false;  // false = sin modelo o sin referencia guardada
    double similarity = 0.0;
    double threshold = 0.0;
    bool anomalous = false;
    std::string note;  // p. ej. "modelo no disponible"
};

struct ToolCheck {
    std::string name;
    bool ok = false;
    double measured = 0.0;
    std::string detail;
};

struct InspectionVerdict {
    bool ok = false;
    EmbeddingCheck embedding;
    std::vector<ToolCheck> tools;
    std::string summary;  // legible: "OK" o los motivos del NG
};

// Combina apariencia (embeddings) y mediciones geométricas: OK solo si la
// apariencia no es anómala (cuando pudo evaluarse) y todas las herramientas
// están dentro de tolerancia.
InspectionVerdict combineVerdict(const EmbeddingCheck& embedding,
                                 const std::vector<ToolCheck>& tools);

}  // namespace pci::domain
