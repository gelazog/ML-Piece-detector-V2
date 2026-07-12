#pragma once

#include <QIcon>

#include "inspection_editor/tools/tool_types.h"

namespace pci::inspection {

// Iconos vectoriales dibujados en código (sin assets externos), en el color
// de texto del tema para verse bien en claro y oscuro.
QIcon moveModeIcon();
QIcon toolIcon(ToolType type);
QIcon anchorIcon();
QIcon regionIcon();

}  // namespace pci::inspection
