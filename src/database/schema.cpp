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
    const auto version = stmt.value().columnInt(0);

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
    return db.transaction([&db]() -> core::Result<void> {
        if (auto ddl = db.exec(kSchemaV1); !ddl.isOk()) {
            return ddl;
        }
        return db.exec("PRAGMA user_version = " + std::to_string(kSchemaVersion) + ";");
    });
}

}  // namespace pci::database
