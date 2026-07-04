#include "domain/verdict.h"

namespace pci::domain {

InspectionVerdict combineVerdict(const EmbeddingCheck& embedding,
                                 const std::vector<ToolCheck>& tools) {
    InspectionVerdict verdict;
    verdict.embedding = embedding;
    verdict.tools = tools;

    int failedTools = 0;
    for (const auto& tool : tools) {
        if (!tool.ok) {
            ++failedTools;
        }
    }

    const bool appearanceOk = !embedding.evaluated || !embedding.anomalous;
    verdict.ok = appearanceOk && failedTools == 0;

    if (verdict.ok) {
        verdict.summary = embedding.evaluated
                              ? "OK"
                              : "OK (sin comparación de apariencia: " + embedding.note + ")";
        return verdict;
    }

    std::string reasons;
    if (!appearanceOk) {
        reasons += "anomalía de apariencia";
    }
    if (failedTools > 0) {
        if (!reasons.empty()) {
            reasons += "; ";
        }
        reasons += std::to_string(failedTools) + " herramienta(s) fuera de tolerancia";
    }
    verdict.summary = "NG: " + reasons;
    return verdict;
}

}  // namespace pci::domain
