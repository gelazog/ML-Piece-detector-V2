#include "repositories/tool_repository.h"

#include <utility>

#include "database/statement.h"
#include "inspection_editor/tools/tool_geometry.h"

namespace pci::repositories {

core::Result<std::int64_t> ToolRepository::save(std::int64_t pieceId,
                                                const inspection::ToolConfig& config) {
    using ResultT = core::Result<std::int64_t>;

    if (config.name.empty()) {
        return ResultT::err("La herramienta necesita un nombre");
    }
    if (config.geometryJson.empty()) {
        return ResultT::err("La herramienta '" + config.name + "' no tiene geometría");
    }

    const bool isUpdate = config.id >= 0;
    auto stmt = db_.prepare(
        isUpdate ? "UPDATE InspectionTools SET type=?, name=?, geometry=?, params=?, "
                   "tolerance_min=?, tolerance_max=?, enabled=? WHERE id=? AND piece_id=?;"
                 : "INSERT INTO InspectionTools (type, name, geometry, params, tolerance_min, "
                   "tolerance_max, enabled, piece_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }

    auto& s = stmt.value();
    if (auto b = s.bindText(1, inspection::toolTypeName(config.type)); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindText(2, config.name); !b.isOk()) return ResultT::err(b.error().message);
    if (auto b = s.bindText(3, config.geometryJson); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindText(4, config.paramsJson); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindDouble(5, config.toleranceMin); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindDouble(6, config.toleranceMax); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindInt(7, config.enabled ? 1 : 0); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindInt(8, isUpdate ? config.id : pieceId); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (isUpdate) {
        if (auto b = s.bindInt(9, pieceId); !b.isOk()) return ResultT::err(b.error().message);
    }

    if (auto step = s.step(); !step.isOk()) {
        return ResultT::err("No se pudo guardar la herramienta: " + step.error().message);
    }
    if (isUpdate && db_.changes() == 0) {
        // La fila ya no existe (p. ej. un borrado deshecho con Ctrl+Z):
        // reinsertar en lugar de perder la herramienta en silencio.
        inspection::ToolConfig fresh = config;
        fresh.id = -1;
        return save(pieceId, fresh);
    }
    return ResultT::ok(isUpdate ? config.id : db_.lastInsertId());
}

core::Result<std::vector<inspection::ToolConfig>> ToolRepository::listForPiece(
    std::int64_t pieceId) {
    using ResultT = core::Result<std::vector<inspection::ToolConfig>>;

    auto stmt = db_.prepare(
        "SELECT id, type, name, geometry, params, tolerance_min, tolerance_max, enabled "
        "FROM InspectionTools WHERE piece_id = ? ORDER BY id;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto bind = stmt.value().bindInt(1, pieceId); !bind.isOk()) {
        return ResultT::err(bind.error().message);
    }

    std::vector<inspection::ToolConfig> tools;
    while (true) {
        auto row = stmt.value().step();
        if (!row.isOk()) {
            return ResultT::err(row.error().message);
        }
        if (!row.value()) {
            break;
        }

        auto& s = stmt.value();
        auto type = inspection::toolTypeFromName(s.columnText(1));
        if (!type.isOk()) {
            return ResultT::err(type.error().message);
        }

        inspection::ToolConfig config;
        config.id = s.columnInt(0);
        config.type = type.value();
        config.name = s.columnText(2);
        config.geometryJson = s.columnText(3);
        config.paramsJson = s.columnText(4);
        config.toleranceMin = s.columnDouble(5);
        config.toleranceMax = s.columnDouble(6);
        config.enabled = s.columnInt(7) != 0;
        tools.push_back(std::move(config));
    }
    return ResultT::ok(std::move(tools));
}

core::Result<void> ToolRepository::remove(std::int64_t toolId) {
    auto stmt = db_.prepare("DELETE FROM InspectionTools WHERE id = ?;");
    if (!stmt.isOk()) {
        return core::Result<void>::err(stmt.error().message);
    }
    if (auto bind = stmt.value().bindInt(1, toolId); !bind.isOk()) {
        return bind;
    }
    auto step = stmt.value().step();
    if (!step.isOk()) {
        return core::Result<void>::err(step.error().message);
    }
    return core::Result<void>::ok();
}

}  // namespace pci::repositories
