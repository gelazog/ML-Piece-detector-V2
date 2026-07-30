#include "database/schema.h"

#include <string>

#include "core/logging.h"
#include "database/statement.h"

namespace pci::database {

namespace {

// Esquema v1 completo según el prompt maestro. Las referencias (tabla
// Embeddings) se versionan por pieza y nunca se borran. Sin imágenes
// completas: solo miniaturas BLOB.
const char* const kSchemaV1 = R"sql(
CREATE TABLE IF NOT EXISTS Pieces (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    created_at  TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    thumbnail   BLOB
);

CREATE TABLE IF NOT EXISTS Embeddings (
    id           INTEGER PRIMARY KEY,
    piece_id     INTEGER NOT NULL REFERENCES Pieces(id) ON DELETE CASCADE,
    version      INTEGER NOT NULL,
    dim          INTEGER NOT NULL,
    mean         BLOB NOT NULL,
    stddev       BLOB NOT NULL,
    sample_count INTEGER NOT NULL,
    sim_mean     REAL NOT NULL,
    sim_std      REAL NOT NULL,
    sim_min      REAL NOT NULL,
    created_at   TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    UNIQUE (piece_id, version)
);

CREATE TABLE IF NOT EXISTS InspectionTools (
    id            INTEGER PRIMARY KEY,
    piece_id      INTEGER NOT NULL REFERENCES Pieces(id) ON DELETE CASCADE,
    type          TEXT NOT NULL,
    name          TEXT NOT NULL,
    geometry      TEXT NOT NULL,
    params        TEXT NOT NULL DEFAULT '{}',
    tolerance_min REAL,
    tolerance_max REAL,
    enabled       INTEGER NOT NULL DEFAULT 1,
    created_at    TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE TABLE IF NOT EXISTS InspectionHistory (
    id                INTEGER PRIMARY KEY,
    piece_id          INTEGER NOT NULL REFERENCES Pieces(id) ON DELETE CASCADE,
    reference_version INTEGER,
    started_at        TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    verdict           TEXT,
    similarity        REAL,
    thumbnail         BLOB
);

CREATE TABLE IF NOT EXISTS InspectionResults (
    id            INTEGER PRIMARY KEY,
    inspection_id INTEGER NOT NULL REFERENCES InspectionHistory(id) ON DELETE CASCADE,
    kind          TEXT NOT NULL,
    ok            INTEGER NOT NULL,
    detail        TEXT
);

CREATE TABLE IF NOT EXISTS ToolResults (
    id            INTEGER PRIMARY KEY,
    inspection_id INTEGER NOT NULL REFERENCES InspectionHistory(id) ON DELETE CASCADE,
    tool_id       INTEGER NOT NULL REFERENCES InspectionTools(id) ON DELETE CASCADE,
    ok            INTEGER NOT NULL,
    measured      REAL,
    detail        TEXT
);

CREATE TABLE IF NOT EXISTS Measurements (
    id             INTEGER PRIMARY KEY,
    tool_result_id INTEGER NOT NULL REFERENCES ToolResults(id) ON DELETE CASCADE,
    name           TEXT NOT NULL,
    value          REAL NOT NULL,
    unit           TEXT NOT NULL DEFAULT 'px'
);

CREATE TABLE IF NOT EXISTS Settings (
    key   TEXT PRIMARY KEY,
    value TEXT
);

CREATE TABLE IF NOT EXISTS Statistics (
    id       INTEGER PRIMARY KEY,
    piece_id INTEGER NOT NULL REFERENCES Pieces(id) ON DELETE CASCADE,
    date     TEXT NOT NULL,
    total    INTEGER NOT NULL DEFAULT 0,
    ok_count INTEGER NOT NULL DEFAULT 0,
    ng_count INTEGER NOT NULL DEFAULT 0,
    UNIQUE (piece_id, date)
);

CREATE INDEX IF NOT EXISTS idx_embeddings_piece ON Embeddings(piece_id, version);
CREATE INDEX IF NOT EXISTS idx_tools_piece ON InspectionTools(piece_id);
CREATE INDEX IF NOT EXISTS idx_history_piece ON InspectionHistory(piece_id, started_at);
)sql";

// v2: rasgo distintivo (punto en coords de pieza + intensidad esperada) que
// resuelve la ambigüedad de 180° en piezas simétricas.
const char* const kMigrationV2 = R"sql(
ALTER TABLE Pieces ADD COLUMN anchor_x REAL;
ALTER TABLE Pieces ADD COLUMN anchor_y REAL;
ALTER TABLE Pieces ADD COLUMN anchor_intensity REAL;
)sql";

// v3: ajuste manual de orientación en grados (0 = usar la detectada).
const char* const kMigrationV3 = R"sql(
ALTER TABLE Pieces ADD COLUMN orientation_offset REAL NOT NULL DEFAULT 0;
)sql";

// v4: varias plantillas de herramientas por pieza.
const char* const kMigrationV4 = R"sql(
ALTER TABLE InspectionTools ADD COLUMN template TEXT NOT NULL DEFAULT 'principal';
)sql";

// v5: modo de medición por pieza (Real / Especial) y el tablero de referencia
// con el que se mide en el modo Especial (origen, punto fijado y si los ejes
// giran con la pieza). Los valores por defecto reproducen el comportamiento
// anterior: modo Real y tablero centrado en la pieza.
const char* const kMigrationV5 = R"sql(
ALTER TABLE Pieces ADD COLUMN measurement_mode TEXT NOT NULL DEFAULT 'real';
ALTER TABLE Pieces ADD COLUMN board_origin TEXT NOT NULL DEFAULT 'bounds';
ALTER TABLE Pieces ADD COLUMN board_fixed_x REAL NOT NULL DEFAULT 0;
ALTER TABLE Pieces ADD COLUMN board_fixed_y REAL NOT NULL DEFAULT 0;
ALTER TABLE Pieces ADD COLUMN board_follow_angle INTEGER NOT NULL DEFAULT 0;
)sql";

// v6: ajuste fino del cero del tablero y corrección del centrado automático.
// El origen 'piece' guardado por la v5 es el centro de MASA, que en piezas
// asimétricas no cae donde el operador ve el centro; el centrado automático
// pasa a ser 'bounds' (centro del contorno) y las piezas existentes se
// convierten, que es lo que se pretendía al elegir "centro de la pieza".
const char* const kMigrationV6 = R"sql(
ALTER TABLE Pieces ADD COLUMN board_offset_x REAL NOT NULL DEFAULT 0;
ALTER TABLE Pieces ADD COLUMN board_offset_y REAL NOT NULL DEFAULT 0;
UPDATE Pieces SET board_origin = 'bounds' WHERE board_origin = 'piece';
)sql";

// v7: reglas del modo Especial (M4) por pieza — desviación máxima del centro
// respecto al cero del tablero y giro máximo. 0 = no vigilar, así que las
// piezas existentes se comportan exactamente como antes.
const char* const kMigrationV7 = R"sql(
ALTER TABLE Pieces ADD COLUMN board_tol_radius REAL NOT NULL DEFAULT 0;
ALTER TABLE Pieces ADD COLUMN board_tol_angle REAL NOT NULL DEFAULT 0;
)sql";

// v8: perfiles de detección con nombre ("luz brillante", "contraluz"...) y la
// posibilidad de asignar uno a cada pieza. detection_profile_id = 0 significa
// "usar los ajustes globales", que es como se comportaba hasta ahora.
const char* const kMigrationV8 = R"sql(
CREATE TABLE IF NOT EXISTS DetectionProfiles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    manual_threshold INTEGER NOT NULL DEFAULT -1,
    polarity INTEGER NOT NULL DEFAULT 0,
    blur_kernel INTEGER NOT NULL DEFAULT 5,
    morph_kernel INTEGER NOT NULL DEFAULT 5,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
ALTER TABLE Pieces ADD COLUMN detection_profile_id INTEGER NOT NULL DEFAULT 0;
)sql";

const char* migrationFor(int targetVersion) {
    switch (targetVersion) {
        case 1: return kSchemaV1;
        case 2: return kMigrationV2;
        case 3: return kMigrationV3;
        case 4: return kMigrationV4;
        case 5: return kMigrationV5;
        case 6: return kMigrationV6;
        case 7: return kMigrationV7;
        case 8: return kMigrationV8;
    }
    return nullptr;
}

}  // namespace

core::Result<void> migrate(Db& db) {
    auto stmt = db.prepare("PRAGMA user_version;");
    if (!stmt.isOk()) {
        return core::Result<void>::err(stmt.error().message);
    }
    auto row = stmt.value().step();
    if (!row.isOk() || !row.value()) {
        return core::Result<void>::err("No se pudo leer la versión del esquema");
    }
    const auto version = static_cast<int>(stmt.value().columnInt(0));

    if (version == kSchemaVersion) {
        return core::Result<void>::ok();
    }
    if (version > kSchemaVersion) {
        return core::Result<void>::err(
            "La base de datos es de una versión más nueva de la aplicación (esquema " +
            std::to_string(version) + " > " + std::to_string(kSchemaVersion) + ")");
    }

    core::logInfo("Migrando esquema de BD de v" + std::to_string(version) + " a v" +
                  std::to_string(kSchemaVersion));
    // Migraciones secuenciales, cada paso en su propia transacción: una BD
    // vieja se actualiza escalón por escalón sin perder datos.
    for (int target = version + 1; target <= kSchemaVersion; ++target) {
        const char* ddl = migrationFor(target);
        if (ddl == nullptr) {
            return core::Result<void>::err("Migración desconocida a v" +
                                           std::to_string(target));
        }
        auto applied = db.transaction([&db, ddl, target]() -> core::Result<void> {
            if (auto result = db.exec(ddl); !result.isOk()) {
                return result;
            }
            return db.exec("PRAGMA user_version = " + std::to_string(target) + ";");
        });
        if (!applied.isOk()) {
            return applied;
        }
    }
    return core::Result<void>::ok();
}

}  // namespace pci::database
