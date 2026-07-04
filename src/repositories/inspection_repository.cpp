#include "repositories/inspection_repository.h"

#include <utility>

#include "database/statement.h"

namespace pci::repositories {

core::Result<std::int64_t> InspectionRepository::saveInspection(
    std::int64_t pieceId, int referenceVersion, const domain::InspectionVerdict& verdict,
    const std::vector<inspection::ToolRunResult>& toolResults,
    const std::vector<unsigned char>& thumbnailJpeg) {
    using ResultT = core::Result<std::int64_t>;

    std::int64_t historyId = -1;
    const auto result = db_.transaction([&]() -> core::Result<void> {
        auto insert = db_.prepare(
            "INSERT INTO InspectionHistory (piece_id, reference_version, verdict, "
            "similarity, thumbnail) VALUES (?, ?, ?, ?, ?);");
        if (!insert.isOk()) {
            return core::Result<void>::err(insert.error().message);
        }
        auto& h = insert.value();
        if (auto b = h.bindInt(1, pieceId); !b.isOk()) return b;
        if (auto b = h.bindInt(2, referenceVersion); !b.isOk()) return b;
        if (auto b = h.bindText(3, verdict.ok ? "OK" : "NG"); !b.isOk()) return b;
        if (auto b = h.bindDouble(4, verdict.embedding.similarity); !b.isOk()) return b;
        if (auto b = h.bindBlob(5, thumbnailJpeg); !b.isOk()) return b;
        if (auto step = h.step(); !step.isOk()) {
            return core::Result<void>::err(step.error().message);
        }
        historyId = db_.lastInsertId();

        // Resultado de apariencia (si se evaluó) y resumen de herramientas.
        auto insertResult = [&](const std::string& kind, bool ok,
                                const std::string& detail) -> core::Result<void> {
            auto stmt = db_.prepare(
                "INSERT INTO InspectionResults (inspection_id, kind, ok, detail) "
                "VALUES (?, ?, ?, ?);");
            if (!stmt.isOk()) {
                return core::Result<void>::err(stmt.error().message);
            }
            auto& s = stmt.value();
            if (auto b = s.bindInt(1, historyId); !b.isOk()) return b;
            if (auto b = s.bindText(2, kind); !b.isOk()) return b;
            if (auto b = s.bindInt(3, ok ? 1 : 0); !b.isOk()) return b;
            if (auto b = s.bindText(4, detail); !b.isOk()) return b;
            auto step = s.step();
            if (!step.isOk()) {
                return core::Result<void>::err(step.error().message);
            }
            return core::Result<void>::ok();
        };

        if (verdict.embedding.evaluated) {
            if (auto r = insertResult("embedding", !verdict.embedding.anomalous,
                                      "similitud=" + std::to_string(verdict.embedding.similarity));
                !r.isOk()) {
                return r;
            }
        }
        if (auto r = insertResult("final", verdict.ok, verdict.summary); !r.isOk()) {
            return r;
        }

        // Resultados por herramienta (solo herramientas persistidas: id >= 0).
        for (const auto& tool : toolResults) {
            if (tool.toolId < 0) {
                continue;
            }
            auto stmt = db_.prepare(
                "INSERT INTO ToolResults (inspection_id, tool_id, ok, measured, detail) "
                "VALUES (?, ?, ?, ?, ?);");
            if (!stmt.isOk()) {
                return core::Result<void>::err(stmt.error().message);
            }
            auto& s = stmt.value();
            if (auto b = s.bindInt(1, historyId); !b.isOk()) return b;
            if (auto b = s.bindInt(2, tool.toolId); !b.isOk()) return b;
            if (auto b = s.bindInt(3, tool.ok ? 1 : 0); !b.isOk()) return b;
            if (auto b = s.bindDouble(4, tool.measured); !b.isOk()) return b;
            if (auto b = s.bindText(5, tool.detail); !b.isOk()) return b;
            if (auto step = s.step(); !step.isOk()) {
                return core::Result<void>::err(step.error().message);
            }
            const std::int64_t toolResultId = db_.lastInsertId();

            auto measurement = db_.prepare(
                "INSERT INTO Measurements (tool_result_id, name, value, unit) "
                "VALUES (?, 'medida', ?, 'px');");
            if (!measurement.isOk()) {
                return core::Result<void>::err(measurement.error().message);
            }
            auto& m = measurement.value();
            if (auto b = m.bindInt(1, toolResultId); !b.isOk()) return b;
            if (auto b = m.bindDouble(2, tool.measured); !b.isOk()) return b;
            if (auto step = m.step(); !step.isOk()) {
                return core::Result<void>::err(step.error().message);
            }
        }

        // Estadísticas del día (upsert).
        auto stats = db_.prepare(
            "INSERT INTO Statistics (piece_id, date, total, ok_count, ng_count) "
            "VALUES (?, date('now', 'localtime'), 1, ?, ?) "
            "ON CONFLICT(piece_id, date) DO UPDATE SET total = total + 1, "
            "ok_count = ok_count + excluded.ok_count, "
            "ng_count = ng_count + excluded.ng_count;");
        if (!stats.isOk()) {
            return core::Result<void>::err(stats.error().message);
        }
        auto& st = stats.value();
        if (auto b = st.bindInt(1, pieceId); !b.isOk()) return b;
        if (auto b = st.bindInt(2, verdict.ok ? 1 : 0); !b.isOk()) return b;
        if (auto b = st.bindInt(3, verdict.ok ? 0 : 1); !b.isOk()) return b;
        auto step = st.step();
        if (!step.isOk()) {
            return core::Result<void>::err(step.error().message);
        }
        return core::Result<void>::ok();
    });

    if (!result.isOk()) {
        return ResultT::err("No se pudo guardar la inspección: " + result.error().message);
    }
    return ResultT::ok(historyId);
}

core::Result<std::vector<InspectionRepository::HistoryEntry>>
InspectionRepository::recentForPiece(std::int64_t pieceId, int limit) {
    using ResultT = core::Result<std::vector<HistoryEntry>>;

    auto stmt = db_.prepare(
        "SELECT id, started_at, verdict, similarity, COALESCE(reference_version, 0) "
        "FROM InspectionHistory WHERE piece_id = ? ORDER BY id DESC LIMIT ?;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto b = stmt.value().bindInt(1, pieceId); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = stmt.value().bindInt(2, limit); !b.isOk()) {
        return ResultT::err(b.error().message);
    }

    std::vector<HistoryEntry> entries;
    while (true) {
        auto row = stmt.value().step();
        if (!row.isOk()) {
            return ResultT::err(row.error().message);
        }
        if (!row.value()) {
            break;
        }
        auto& s = stmt.value();
        HistoryEntry entry;
        entry.id = s.columnInt(0);
        entry.startedAt = s.columnText(1);
        entry.verdict = s.columnText(2);
        entry.similarity = s.columnDouble(3);
        entry.referenceVersion = static_cast<int>(s.columnInt(4));
        entries.push_back(std::move(entry));
    }
    return ResultT::ok(std::move(entries));
}

core::Result<InspectionRepository::DayStats> InspectionRepository::todayStats(
    std::int64_t pieceId) {
    using ResultT = core::Result<DayStats>;

    auto stmt = db_.prepare(
        "SELECT total, ok_count, ng_count FROM Statistics "
        "WHERE piece_id = ? AND date = date('now', 'localtime');");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto b = stmt.value().bindInt(1, pieceId); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    auto row = stmt.value().step();
    if (!row.isOk()) {
        return ResultT::err(row.error().message);
    }

    DayStats stats;
    if (row.value()) {
        stats.total = static_cast<int>(stmt.value().columnInt(0));
        stats.okCount = static_cast<int>(stmt.value().columnInt(1));
        stats.ngCount = static_cast<int>(stmt.value().columnInt(2));
    }
    return ResultT::ok(stats);
}

}  // namespace pci::repositories
