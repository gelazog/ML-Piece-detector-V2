#pragma once

#include <cstdint>
#include <string>

#include "core/result.h"

namespace pci::inspection {

enum class ToolType {
    Caliper,
    Circle,
    PointToLine,
    EdgeFlaw,
    Blob,
    Ruler,
    LineToLine,
    Angle,
    PolyBlob,
    Position,
    Arc,
    Shaft,
    Thread,
    Gear,
    ConstructedPoint,
    ConstructedLine,
    MedianAxis,
    Region,
    Symmetry,
    Polygon,
    EdgeDefects,
    Clearance,
    Straightness,
    Roundness,
    Orientation,
    CentreOffset,
    BoltPattern,
    Profile,
    Extremes,
    Chamfer
};

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
    // Nombre de OTRA herramienta cuyo elemento derivado sirve de referencia
    // (X0). Vacío = la herramienta se basta sola, que es el caso de las catorce
    // primeras. Se persiste dentro de `paramsJson`, que existía sin usarse y es
    // justo el sitio previsto para parámetros por herramienta: así no hace
    // falta migrar el esquema.
    std::string reference;
    // Segunda referencia (X1). La mitad de las construcciones necesitan dos
    // elementos —punto medio de dos, intersección de dos rectas, recta por dos
    // puntos—, y el GD&T con marco de dos datums la necesitará igual. Vacía en
    // todas las herramientas que se bastan con una o con ninguna.
    std::string reference2;
};

}  // namespace pci::inspection
