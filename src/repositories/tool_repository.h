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
    core::Result<std::int64_t> save(std::int64_t pieceId, const inspection::ToolConfig& config);
    core::Result<std::vector<inspection::ToolConfig>> listForPiece(std::int64_t pieceId);
    core::Result<void> remove(std::int64_t toolId);

private:
    database::Db& db_;
};

}  // namespace pci::repositories
