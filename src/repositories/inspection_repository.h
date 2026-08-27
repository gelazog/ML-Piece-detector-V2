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
    //
    // `calibrationId` es CON QUÉ CALIBRACIÓN se midió esto, y es la mitad del
    // valor de guardarlo. Todos los milímetros de una inspección salen de un
    // único factor px→mm; si más tarde se descubre que ese factor estaba mal
    // —alguien movió la cámara, se cambió el objetivo—, sin este dato no hay
    // forma de saber qué veredictos hay que revisar.
    //
    // 0 significa «no se llevaba registro cuando se hizo», que es distinto de
    // «sin calibrar» y hay que poder distinguirlo: las inspecciones anteriores
    // a la tabla de calibraciones no pueden inventarse a cuál pertenecían.
    core::Result<std::int64_t> saveInspection(
        std::int64_t pieceId, int referenceVersion, const domain::InspectionVerdict& verdict,
        const std::vector<inspection::ToolRunResult>& toolResults,
        const std::vector<unsigned char>& thumbnailJpeg, std::int64_t calibrationId = 0);

    struct HistoryEntry {
        std::int64_t id = 0;
        std::string startedAt;
        std::string verdict;
        double similarity = 0.0;
        int referenceVersion = 0;
    };
    core::Result<std::vector<HistoryEntry>> recentForPiece(std::int64_t pieceId,
                                                           int limit = 20);

    // Las inspecciones de una pieza con el MOTIVO de cada rechazo, para el
    // informe de turno (`domain/shift_report.h`).
    //
    // No vale con `recentForPiece`: aquello devuelve fecha y veredicto, y un
    // turno con 47 rechazos es entonces un número. El motivo es lo que lo
    // convierte en «31 de los 47 fueron por el diámetro exterior», que ya dice
    // qué hay que ir a mirar.
    //
    // El motivo sale del NOMBRE de la herramienta que falló, no de su detalle:
    // el detalle lleva la medida concreta —«12,4 mm»— y con eso cada rechazo
    // sería un motivo distinto y no se agruparían nunca. Sin herramientas
    // fallidas, se usa el tipo de comprobación que falló.
    struct ReportRow {
        std::string startedAt;
        std::string verdict;
        double similarity = 0.0;
        int referenceVersion = 0;
        std::string reason;  // vacío si pasó
    };
    // EL PERIODO DEL INFORME. Vacio = sin limite por ese lado.
    //
    // Existe porque sin el, «informe del turno» era mentira: la consulta se
    // llevaba TODO el historial de la pieza. Y como ordenaba de mas antigua a
    // mas nueva antes de recortar, lo que salia eran las 2000 inspecciones MAS
    // VIEJAS — o sea que a una pieza con historial le daba un informe de hace
    // meses y lo titulaba «turno».
    struct ReportWindow {
        std::string from;  // "YYYY-MM-DD HH:MM:SS"
        std::string to;
    };

    // Tope de filas. Un turno de ocho horas a cinco segundos por pieza son 5 760
    // inspecciones, asi que el limite anterior —2 000— se quedaba corto para el
    // caso normal. Este cubre un dia entero de tres turnos con margen.
    static constexpr int kMaxReportRows = 25000;

    // `discarded`, si se pasa, recibe cuantas quedaron fuera del tope. Truncar
    // un informe en silencio es peor que no darlo: sale un rendimiento calculado
    // sobre una parte del turno, con pinta de ser el del turno entero.
    core::Result<std::vector<ReportRow>> reportForPiece(std::int64_t pieceId,
                                                        const ReportWindow& window = {},
                                                        int limit = kMaxReportRows,
                                                        int* discarded = nullptr);

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
