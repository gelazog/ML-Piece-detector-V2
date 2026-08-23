#include "repositories/inspection_repository.h"

#include <algorithm>

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
                "INSERT INTO ToolResults (inspection_id, tool_id, ok, measured, detail, "
                "piece_index) VALUES (?, ?, ?, ?, ?, ?);");
            if (!stmt.isOk()) {
                return core::Result<void>::err(stmt.error().message);
            }
            auto& s = stmt.value();
            if (auto b = s.bindInt(1, historyId); !b.isOk()) return b;
            if (auto b = s.bindInt(2, tool.toolId); !b.isOk()) return b;
            if (auto b = s.bindInt(3, tool.ok ? 1 : 0); !b.isOk()) return b;
            if (auto b = s.bindDouble(4, tool.measured); !b.isOk()) return b;
            if (auto b = s.bindText(5, tool.detail); !b.isOk()) return b;
            if (auto b = s.bindInt(6, tool.pieceIndex); !b.isOk()) return b;
            if (auto step = s.step(); !step.isOk()) {
                return core::Result<void>::err(step.error().message);
            }
            const std::int64_t toolResultId = db_.lastInsertId();

            // La unidad se guarda de VERDAD. Antes iba «px» fija para todo, con
            // ángulos y recuentos incluidos: una columna que siempre dice lo
            // mismo no es un dato, y esta además mentía. El histórico existe
            // para poder releerlo, y un valor sin su unidad correcta no se
            // puede releer.
            auto measurement = db_.prepare(
                "INSERT INTO Measurements (tool_result_id, name, value, unit) "
                "VALUES (?, 'medida', ?, ?);");
            if (!measurement.isOk()) {
                return core::Result<void>::err(measurement.error().message);
            }
            auto& m = measurement.value();
            if (auto b = m.bindInt(1, toolResultId); !b.isOk()) return b;
            if (auto b = m.bindDouble(2, tool.measured); !b.isOk()) return b;
            if (auto b = m.bindText(3, inspection::measuredUnitKey(tool.kind)); !b.isOk()) {
                return b;
            }
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

core::Result<std::vector<InspectionRepository::ReportRow>>
InspectionRepository::reportForPiece(std::int64_t pieceId, const ReportWindow& window,
                                     int limit, int* discarded) {
    if (discarded != nullptr) {
        *discarded = 0;
    }
    // El filtro de periodo se escribe una vez y se usa en las dos consultas —la
    // que cuenta y la que trae— para que no puedan discrepar.
    const std::string period =
        " AND (?2 = '' OR h.started_at >= ?2) AND (?3 = '' OR h.started_at <= ?3)";

    // CUÁNTAS HAY DE VERDAD, antes de recortar. Sin esto no se puede decir que
    // se ha truncado, y un informe truncado en silencio da un rendimiento
    // calculado sobre parte del turno con pinta de ser el del turno entero.
    if (discarded != nullptr) {
        auto counter = db_.prepare("SELECT COUNT(*) FROM InspectionHistory h "
                                   "WHERE h.piece_id = ?1" + period + ";");
        if (counter.isOk()) {
            auto& c = counter.value();
            if (c.bindInt(1, pieceId).isOk() && c.bindText(2, window.from).isOk() &&
                c.bindText(3, window.to).isOk()) {
                if (auto row = c.step(); row.isOk() && row.value()) {
                    const int total = static_cast<int>(c.columnInt(0));
                    *discarded = std::max(0, total - limit);
                }
            }
        }
    }

    // Los motivos se traen en la MISMA consulta con dos subconsultas. Hacerlo
    // con una consulta por inspección serían cientos de idas y vueltas a la base
    // por cada informe, y un informe de turno se pide con el turno acabando.
    //
    // Y se ordena DESCENDENTE para recortar. Ascendente, el tope se quedaba con
    // las inspecciones MÁS VIEJAS de la pieza: a una con historial le daba un
    // informe de hace meses y lo titulaba «turno». La lista se le da la vuelta
    // después, porque quien lo lee lo quiere en orden cronológico.
    auto stmt = db_.prepare(
        "SELECT h.started_at, h.verdict, h.similarity, h.reference_version, "
        "  COALESCE((SELECT group_concat(DISTINCT t.name) FROM ToolResults tr "
        "            JOIN InspectionTools t ON t.id = tr.tool_id "
        "            WHERE tr.inspection_id = h.id AND tr.ok = 0), ''), "
        "  COALESCE((SELECT group_concat(DISTINCT ir.kind) FROM InspectionResults ir "
        "            WHERE ir.inspection_id = h.id AND ir.ok = 0 "
        "              AND ir.kind <> 'final'), '') "
        "FROM InspectionHistory h WHERE h.piece_id = ?1" + period +
        " ORDER BY h.started_at DESC LIMIT ?4;");
    if (!stmt.isOk()) {
        return core::Result<std::vector<ReportRow>>::err(stmt.error().message);
    }
    auto& s = stmt.value();
    if (auto b = s.bindInt(1, pieceId); !b.isOk()) {
        return core::Result<std::vector<ReportRow>>::err(b.error().message);
    }
    if (auto b = s.bindText(2, window.from); !b.isOk()) {
        return core::Result<std::vector<ReportRow>>::err(b.error().message);
    }
    if (auto b = s.bindText(3, window.to); !b.isOk()) {
        return core::Result<std::vector<ReportRow>>::err(b.error().message);
    }
    if (auto b = s.bindInt(4, limit); !b.isOk()) {
        return core::Result<std::vector<ReportRow>>::err(b.error().message);
    }

    // Los tipos internos, dichos como los diría un operador. Un informe que pone
    // «embedding» obliga a saber qué es eso para leerlo.
    const auto sayKind = [](const std::string& kind) -> std::string {
        if (kind == "embedding") {
            return "apariencia";
        }
        if (kind == "count") {
            return "recuento de piezas";
        }
        if (kind == "position") {
            return "posición en el tablero";
        }
        return kind;
    };

    std::vector<ReportRow> rows;
    while (true) {
        auto step = s.step();
        if (!step.isOk()) {
            return core::Result<std::vector<ReportRow>>::err(step.error().message);
        }
        if (!step.value()) {
            break;
        }
        ReportRow row;
        row.startedAt = s.columnText(0);
        row.verdict = s.columnText(1);
        row.similarity = s.columnDouble(2);
        row.referenceVersion = static_cast<int>(s.columnInt(3));
        const std::string tools = s.columnText(4);
        if (!tools.empty()) {
            row.reason = tools;
        } else {
            std::string kinds = s.columnText(5);
            std::string said;
            std::size_t start = 0;
            while (start <= kinds.size()) {
                const std::size_t comma = kinds.find(',', start);
                const std::string one =
                    kinds.substr(start, comma == std::string::npos ? std::string::npos
                                                                   : comma - start);
                if (!one.empty()) {
                    if (!said.empty()) {
                        said += ", ";
                    }
                    said += sayKind(one);
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
            row.reason = said;
        }
        rows.push_back(std::move(row));
    }
    // De vuelta a orden cronológico: se pidió al revés solo para que el tope se
    // quedara con las últimas.
    std::reverse(rows.begin(), rows.end());
    return core::Result<std::vector<ReportRow>>::ok(std::move(rows));
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

core::Result<std::vector<InspectionRepository::DailyStat>>
InspectionRepository::dailyStats(std::int64_t pieceId, int days) {
    using ResultT = core::Result<std::vector<DailyStat>>;

    const std::string sinceModifier = "-" + std::to_string(days > 0 ? days - 1 : 0) + " days";
    auto stmt = db_.prepare(
        "SELECT date, total, ok_count, ng_count FROM Statistics "
        "WHERE piece_id = ? AND date >= date('now', 'localtime', ?) ORDER BY date;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto b = stmt.value().bindInt(1, pieceId); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = stmt.value().bindText(2, sinceModifier); !b.isOk()) {
        return ResultT::err(b.error().message);
    }

    std::vector<DailyStat> stats;
    while (true) {
        auto row = stmt.value().step();
        if (!row.isOk()) {
            return ResultT::err(row.error().message);
        }
        if (!row.value()) {
            break;
        }
        auto& s = stmt.value();
        DailyStat day;
        day.date = s.columnText(0);
        day.total = static_cast<int>(s.columnInt(1));
        day.okCount = static_cast<int>(s.columnInt(2));
        day.ngCount = static_cast<int>(s.columnInt(3));
        stats.push_back(std::move(day));
    }
    return ResultT::ok(std::move(stats));
}

}  // namespace pci::repositories
