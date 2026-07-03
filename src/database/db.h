#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/result.h"

struct sqlite3;

namespace pci::database {

class Statement;

// RAII sobre la API C de SQLite. Toda la frontera devuelve Result: una BD
// corrupta o bloqueada es un error controlado, nunca un crash.
class Db {
public:
    // Abre (o crea) el archivo y aplica los PRAGMA de la conexión:
    // foreign_keys, WAL y busy_timeout. Falla de forma controlada si el
    // archivo no es una base de datos SQLite válida.
    static core::Result<std::unique_ptr<Db>> open(const std::string& path);
    ~Db();

    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    core::Result<void> exec(const std::string& sql);
    core::Result<Statement> prepare(const std::string& sql);

    // Ejecuta body dentro de BEGIN/COMMIT; si body falla o lanza, ROLLBACK.
    core::Result<void> transaction(const std::function<core::Result<void>()>& body);

    [[nodiscard]] std::int64_t lastInsertId() const;
    [[nodiscard]] sqlite3* handle() const { return db_; }

private:
    explicit Db(sqlite3* db) : db_(db) {}

    sqlite3* db_ = nullptr;
};

}  // namespace pci::database
