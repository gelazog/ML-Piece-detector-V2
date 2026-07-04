#pragma once

#include <cstdint>
#include <string>

#include "core/result.h"

namespace pci::inspection {

enum class ToolType { Caliper, Circle, PointToLine, EdgeFlaw, Blob };

const char* toolTypeName(ToolType type);
core::Result<ToolType> toolTypeFromName(const std::string& name);

// Configuración persistible de una herramienta. La geometría vive SIEMPRE en
// coordenadas de pieza (fixture): si la pieza llega rotada, la herramienta la
// sigue. Se serializa como JSON en la tabla InspectionTools.
struct ToolConfig {
    std::int64_t id = -1;  // -1 = aún no guardada
    ToolType type = ToolType::Caliper;
    std::string name;
    std::string geometryJson;
    std::string paramsJson = "{}";
    double toleranceMin = 0.0;
    double toleranceMax = 1e9;
    bool enabled = true;
};

}  // namespace pci::inspection
