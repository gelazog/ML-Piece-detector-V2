#pragma once

#include <string>
#include <vector>

#include "core/result.h"
#include "inspection_editor/tools/tool_types.h"

namespace pci::inspection {

// (De)serialización de una plantilla completa (lista de herramientas) a JSON,
// para exportarla a un archivo y reimportarla en otra pieza o PC. Reutiliza el
// formato de geometría de tool_geometry. Qt-free y testeable sin GUI.

// Serializa las herramientas a un JSON con versión + secuencia "tools".
std::string exportTemplateJson(const std::vector<ToolConfig>& tools);

// Parsea el JSON producido por exportTemplateJson. Valida el tipo, el nombre y
// que la geometría de cada herramienta sea coherente con su tipo; devuelve error
// controlado si el archivo está corrupto. Los ids quedan en -1 (herramientas
// nuevas para la pieza/plantilla destino).
core::Result<std::vector<ToolConfig>> importTemplateJson(const std::string& json);

}  // namespace pci::inspection
