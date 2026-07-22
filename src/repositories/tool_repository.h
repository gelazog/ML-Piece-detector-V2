#pragma once

#include <cstdint>
#include <vector>

#include "core/result.h"
#include "database/db.h"
#include "inspection_editor/tools/tool_types.h"

namespace pci::repositories {

// CRUD de herramientas de inspección por pieza (tabla InspectionTools).
class ToolRepository {
public:
    explicit ToolRepository(database::Db& db) : db_(db) {}

    // Inserta si config.id < 0; actualiza en caso contrario. Devuelve el id.
    // templateName agrupa herramientas: una pieza puede tener varias
    // plantillas y se inspecciona con la activa.
    core::Result<std::int64_t> save(std::int64_t pieceId, const inspection::ToolConfig& config,
                                    const std::string& templateName = "principal");
    core::Result<std::vector<inspection::ToolConfig>> listForPiece(
        std::int64_t pieceId, const std::string& templateName = "principal");
    core::Result<std::vector<std::string>> listTemplates(std::int64_t pieceId);
    core::Result<void> remove(std::int64_t toolId);

    // Gestión de plantillas (M1). Todas operan sobre las herramientas de una
    // pieza agrupadas por su columna `template`.
    // Renombra `from` a `to`; error si `to` ya existe o los nombres son vacíos.
    core::Result<void> renameTemplate(std::int64_t pieceId, const std::string& from,
                                      const std::string& to);
    // Borra todas las herramientas de la plantilla `name` de la pieza.
    core::Result<void> deleteTemplate(std::int64_t pieceId, const std::string& name);
    // Copia todas las herramientas de `from` a una plantilla nueva `to`; error
    // si `to` ya existe.
    core::Result<void> duplicateTemplate(std::int64_t pieceId, const std::string& from,
                                         const std::string& to);

private:
    // ¿La pieza tiene ya alguna herramienta en la plantilla `name`?
    core::Result<bool> templateExists(std::int64_t pieceId, const std::string& name);

    database::Db& db_;
};

}  // namespace pci::repositories
