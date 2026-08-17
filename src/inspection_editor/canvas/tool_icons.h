#pragma once

#include <QIcon>

#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"

namespace pci::inspection {

// Iconos vectoriales dibujados en código (sin assets externos), en el color
// de texto del tema para verse bien en claro y oscuro.
QIcon moveModeIcon();
QIcon toolIcon(ToolType type);

// Icono de una FAMILIA, para la franja de la paleta. Tiene que decir la familia
// de un vistazo y no ser un adorno: son cinco pastillas que se miran de reojo,
// y cinco pastillas indistinguibles serían peor que cinco palabras. Hay un
// barrido que exige que ningún par se parezca.
//
// El `switch` no lleva `default`, igual que `toolIcon`: una familia nueva rompe
// la compilación hasta que alguien le dibuje el suyo, que es la garantía de que
// la franja no se llene de huecos con el tiempo.
QIcon categoryIcon(ToolCategory category);
QIcon anchorIcon();
QIcon regionIcon();
// Zona libre: el mismo trazo discontinuo, pero con forma de polígono irregular
// y los vértices marcados. Al lado del rectangular tiene que distinguirse sin
// leer el texto, porque van uno junto al otro.
QIcon freeZoneIcon();
// Papelera: borrar la herramienta seleccionada. El símbolo es universal a
// propósito — un icono que haya que aprender no sirve para la acción que más
// caro sale equivocar.
QIcon deleteIcon();
// Papelera con aspa: borrar TODAS. Se distingue de la anterior de un vistazo,
// que es lo que hace falta cuando están una al lado de la otra.
QIcon deleteAllIcon();

}  // namespace pci::inspection
