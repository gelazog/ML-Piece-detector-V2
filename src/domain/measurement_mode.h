#pragma once

#include <string_view>

namespace pci::domain {

// Cómo se mide y se registra una pieza. La elección es por pieza y se persiste;
// no cambia lo que las herramientas saben hacer, sino qué reglas se aplican y
// qué ve el operador.
//
//  * Real: lo de siempre — el operador coloca las herramientas donde quiere y
//    cada una se juzga con sus tolerancias. Sin reglas de posición.
//  * Special: además del anterior, la pieza se mide respecto al tablero
//    centrado (centro = 0): desviación y giro entran en juego.
//
// Lógica pura (sin Qt, sin OpenCV): el tablero concreto (origen, punto fijado y
// ejes) vive en vision::BoardConfig y se guarda junto al modo en el repositorio.
enum class MeasurementMode { Real, Special };

// Clave estable para la base de datos. NUNCA cambiar estos textos: hay filas
// guardadas con ellos.
[[nodiscard]] std::string_view modeKey(MeasurementMode mode);
[[nodiscard]] MeasurementMode modeFromKey(std::string_view key,
                                          MeasurementMode fallback = MeasurementMode::Real);

// Nombre y explicación visibles para el operador (UTF-8, en español).
[[nodiscard]] const char* modeLabel(MeasurementMode mode);
[[nodiscard]] const char* modeDescription(MeasurementMode mode);

}  // namespace pci::domain
