#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.h"
#include "database/db.h"
#include "domain/verdict.h"
#include "inspection_editor/execution/tool_executor.h"

namespace pci::repositories {

// Historial de inspecciones + estadísticas diarias (tablas InspectionHistory,
// InspectionResults, ToolResults, Measurements y Statistics).
class InspectionRepository {
public:
    explicit InspectionRepository(database::Db& db) : db_(db) {}

    // Persiste una inspección completa en una transacción. Devuelve el id del
    // registro de historial.
    core::Result<std::int64_t> saveInspection(
        std::int64_t pieceId, int referenceVersion, const domain::InspectionVerdict& verdict,
        const std::vector<inspection::ToolRunResult>& toolResults,
        const std::vector<unsigned char>& thumbnailJpeg);

    struct HistoryEntry {
        std::int64_t id = 0;
        std::string startedAt;
        std::string verdict;
        double similarity = 0.0;
        int referenceVersion = 0;
    };
    core::Result<std::vector<HistoryEntry>> recentForPiece(std::int64_t pieceId,
                                                           int limit = 20);

    struct DayStats {
        int total = 0;
        int okCount = 0;
        int ngCount = 0;
    };
    core::Result<DayStats> todayStats(std::int64_t pieceId);

    struct DailyStat {
        std::string date;  // "YYYY-MM-DD"
        int total = 0;
        int okCount = 0;
        int ngCount = 0;
    };
    // Estadísticas OK/NG por día de una pieza, de los últimos `days` días
    // (incluye hoy), en orden cronológico. Reutiliza la tabla Statistics.
    core::Result<std::vector<DailyStat>> dailyStats(std::int64_t pieceId, int days = 30);

private:
    database::Db& db_;
};

}  // namespace pci::repositories
