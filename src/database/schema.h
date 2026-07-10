#pragma once

#include "core/result.h"
#include "database/db.h"

namespace pci::database {

// Crea o actualiza el esquema mediante PRAGMA user_version. Idempotente:
// llamarlo sobre una BD ya migrada no hace nada. Falla de forma controlada si
// la BD fue creada por una versión más nueva de la aplicación.
core::Result<void> migrate(Db& db);

// Versión de esquema que esta build entiende.
// v2: rasgo distintivo de orientación (anchor_*) en Pieces.
inline constexpr int kSchemaVersion = 2;

}  // namespace pci::database
