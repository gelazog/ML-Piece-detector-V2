#include "repositories/calibration_repository.h"

#include <string>
#include <utility>

#include "database/statement.h"

namespace pci::repositories {

namespace {

// Las columnas en un orden compartido por el guardado y las tres lecturas.
// Escrito una sola vez, y por un motivo reciente: los perfiles de detección
// tenían esta misma lista repetida en tres consultas, y así se les añadió un
// campo a dos de ellas y se olvidó en la tercera durante meses.
constexpr const char* kColumns =
    "id, created_at, mm_per_pixel, camera_dist_mm, fov_deg, calibrated_width, "
    "calibrated_height, camera, method, reference, notes";

CalibrationRecord readRow(database::Statement& row) {
    CalibrationRecord entry;
    entry.id = row.columnInt(0);
    entry.createdAt = row.columnText(1);
    entry.scale.mmPerPixel = row.columnDouble(2);
    entry.scale.cameraDistanceMm = row.columnDouble(3);
    entry.scale.horizontalFovDeg = row.columnDouble(4);
    entry.scale.calibratedWidth = static_cast<int>(row.columnInt(5));
    entry.scale.calibratedHeight = static_cast<int>(row.columnInt(6));
    entry.camera = row.columnText(7);
    entry.method = row.columnText(8);
    entry.reference = row.columnText(9);
    entry.notes = row.columnText(10);
    return entry;
}

}  // namespace

core::Result<std::int64_t> CalibrationRepository::record(const CalibrationRecord& entry) {
    using ResultT = core::Result<std::int64_t>;
    if (!(entry.scale.mmPerPixel > 0.0)) {
        // Anotar una calibración sin escala sería anotar que no hay
        // calibración, y eso ya se sabe sin escribirlo: lo que dejaría es una
        // fila a la que unas inspecciones apuntan como si tuvieran escala.
        return ResultT::err("Una calibración sin escala px→mm no es una calibración");
    }
    auto stmt = db_.prepare(
        "INSERT INTO Calibrations (mm_per_pixel, camera_dist_mm, fov_deg, "
        "calibrated_width, calibrated_height, camera, method, reference, notes) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    auto& s = stmt.value();
    if (auto b = s.bindDouble(1, entry.scale.mmPerPixel); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindDouble(2, entry.scale.cameraDistanceMm); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindDouble(3, entry.scale.horizontalFovDeg); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindInt(4, entry.scale.calibratedWidth); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindInt(5, entry.scale.calibratedHeight); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindText(6, entry.camera); !b.isOk()) return ResultT::err(b.error().message);
    if (auto b = s.bindText(7, entry.method); !b.isOk()) return ResultT::err(b.error().message);
    if (auto b = s.bindText(8, entry.reference); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindText(9, entry.notes); !b.isOk()) return ResultT::err(b.error().message);
    if (auto step = s.step(); !step.isOk()) {
        return ResultT::err(step.error().message);
    }
    return ResultT::ok(db_.lastInsertId());
}

core::Result<std::vector<CalibrationRecord>> CalibrationRepository::list() {
    using ResultT = core::Result<std::vector<CalibrationRecord>>;
    // De la más reciente a la más antigua: la que interesa casi siempre es la
    // última, y buscarla al final de una lista larga es trabajo de más.
    auto stmt = db_.prepare(std::string("SELECT ") + kColumns +
                            " FROM Calibrations ORDER BY id DESC;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    std::vector<CalibrationRecord> all;
    while (true) {
        auto row = stmt.value().step();
        if (!row.isOk()) {
            return ResultT::err(row.error().message);
        }
        if (!row.value()) {
            break;
        }
        all.push_back(readRow(stmt.value()));
    }
    return ResultT::ok(std::move(all));
}

core::Result<CalibrationRecord> CalibrationRepository::load(std::int64_t id) {
    using ResultT = core::Result<CalibrationRecord>;
    auto stmt = db_.prepare(std::string("SELECT ") + kColumns +
                            " FROM Calibrations WHERE id = ?;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto b = stmt.value().bindInt(1, id); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    auto row = stmt.value().step();
    if (!row.isOk()) {
        return ResultT::err(row.error().message);
    }
    if (!row.value()) {
        return ResultT::err("No hay ninguna calibración con el número " +
                            std::to_string(id));
    }
    return ResultT::ok(readRow(stmt.value()));
}

core::Result<CalibrationRecord> CalibrationRepository::current() {
    using ResultT = core::Result<CalibrationRecord>;
    auto stmt = db_.prepare(std::string("SELECT ") + kColumns +
                            " FROM Calibrations ORDER BY id DESC LIMIT 1;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    auto row = stmt.value().step();
    if (!row.isOk()) {
        return ResultT::err(row.error().message);
    }
    if (!row.value()) {
        // Sin ninguna anotada. No es un error: es una instalación que viene de
        // antes de que se llevara registro, y el id 0 lo dice.
        return ResultT::ok(CalibrationRecord{});
    }
    return ResultT::ok(readRow(stmt.value()));
}

core::Result<int> CalibrationRepository::inspectionsUsing(std::int64_t calibrationId) {
    using ResultT = core::Result<int>;
    auto stmt = db_.prepare(
        "SELECT COUNT(*) FROM InspectionHistory WHERE calibration_id = ?;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto b = stmt.value().bindInt(1, calibrationId); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    auto row = stmt.value().step();
    if (!row.isOk() || !row.value()) {
        return ResultT::err("No se pudo contar las inspecciones");
    }
    return ResultT::ok(static_cast<int>(stmt.value().columnInt(0)));
}

}  // namespace pci::repositories
