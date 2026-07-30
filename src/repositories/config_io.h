#pragma once

#include <string>

#include "core/result.h"
#include "repositories/detection_profile_repository.h"
#include "repositories/settings_repository.h"

namespace pci::repositories {

// Exportar/importar la configuración que NO está ligada a una pieza concreta
// (O4): calibración de escala, ajustes y perfiles de detección, atajos de
// teclado y preferencias. Sirve para clonar la puesta a punto a otra PC de la
// línea sin repetirla a mano.
//
// Deliberadamente NO incluye piezas, plantillas, referencias ni historial: eso
// es el trabajo de la línea, se comparte exportando plantillas (que ya existe)
// y copiar la base de datos entera es más honesto que un import a medias.

struct ConfigSummary {
    int settings = 0;
    int profiles = 0;
};

// Escribe un `.json` legible con la configuración actual.
core::Result<ConfigSummary> exportConfig(const std::string& path, SettingsRepository& settings,
                                         DetectionProfileRepository& profiles);

// Lee un `.json` exportado y lo aplica: los ajustes se sobrescriben clave a
// clave y los perfiles se guardan por nombre (el mismo nombre actualiza).
// Falla de forma controlada si el archivo no es un export de esta app.
core::Result<ConfigSummary> importConfig(const std::string& path, SettingsRepository& settings,
                                         DetectionProfileRepository& profiles);

}  // namespace pci::repositories
