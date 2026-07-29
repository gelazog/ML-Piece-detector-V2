#include "domain/verdict.h"

#include <cmath>

namespace pci::domain {

PositionCheck evaluatePosition(double radius, double maxRadius, double angleDeg,
                               double maxAngleDeg, bool axisReliable) {
    PositionCheck check;
    check.radius = radius;
    check.maxRadius = maxRadius;
    check.angleDeg = angleDeg;
    check.maxAngleDeg = maxAngleDeg;

    check.radiusEvaluated = maxRadius > 0.0;
    // El giro solo se juzga si el operador puso tolerancia Y el eje de la pieza
    // es de fiar: en piezas casi simétricas el eje principal salta y daría NG
    // falsos (hallazgo de la revisión de diseño previa a M4).
    check.angleEvaluated = maxAngleDeg > 0.0 && axisReliable;
    check.evaluated = check.radiusEvaluated || check.angleEvaluated;

    const bool radiusOk = !check.radiusEvaluated || radius <= maxRadius;
    const bool angleOk = !check.angleEvaluated || std::abs(angleDeg) <= maxAngleDeg;
    check.ok = radiusOk && angleOk;

    if (maxAngleDeg > 0.0 && !axisReliable) {
        check.note = "giro no evaluado: la pieza es casi simétrica y su eje no es fiable";
    }
    return check;
}

InspectionVerdict combineVerdict(const EmbeddingCheck& embedding,
                                 const std::vector<ToolCheck>& tools,
                                 const PositionCheck& position) {
    InspectionVerdict verdict;
    verdict.embedding = embedding;
    verdict.tools = tools;
    verdict.position = position;

    int failedTools = 0;
    for (const auto& tool : tools) {
        if (!tool.ok) {
            ++failedTools;
        }
    }

    const bool appearanceOk = !embedding.evaluated || !embedding.anomalous;
    const bool positionOk = !position.evaluated || position.ok;
    verdict.ok = appearanceOk && failedTools == 0 && positionOk;

    if (verdict.ok) {
        verdict.summary = embedding.evaluated
                              ? "OK"
                              : "OK (sin comparación de apariencia: " + embedding.note + ")";
        return verdict;
    }

    std::string reasons;
    const auto addReason = [&reasons](const std::string& text) {
        if (!reasons.empty()) {
            reasons += "; ";
        }
        reasons += text;
    };
    if (!appearanceOk) {
        addReason("anomalía de apariencia");
    }
    if (failedTools > 0) {
        addReason(std::to_string(failedTools) + " herramienta(s) fuera de tolerancia");
    }
    if (position.evaluated && !position.ok) {
        if (position.radiusEvaluated && position.radius > position.maxRadius) {
            addReason("pieza descentrada");
        }
        if (position.angleEvaluated && std::abs(position.angleDeg) > position.maxAngleDeg) {
            addReason("pieza girada");
        }
    }
    verdict.summary = "NG: " + reasons;
    return verdict;
}

}  // namespace pci::domain
