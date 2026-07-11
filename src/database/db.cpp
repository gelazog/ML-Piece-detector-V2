#include "database/db.h"

#include <sqlite3.h>

#include <exception>
#include <utility>

#include "core/logging.h"
#include "database/statement.h"

namespace pci::database {

namespace {

std::string errorOf(sqlite3* db) {
    const char* message = sqlite3_errmsg(db);
    return message != nullptr ? message : "error desconocido de SQLite";
}

}  // namespace

core::Result<std::unique_ptr<Db>> Db::open(const std::string& path) {
    using ResultT = core::Result<std::unique_ptr<Db>>;

    sqlite3* raw = nullptr;
    if (sqlite3_open(path.c_str(), &raw) != SQLITE_OK) {
        const std::string message = raw != nullptr ? errorOf(raw) : "sin memoria";
        sqlite3_close(raw);
        return ResultT::err("No se pudo abrir la base de datos '" + path + "': " + message);
    }

    auto db = std::unique_ptr<Db>(new Db(raw));
    sqlite3_busy_timeout(raw, 3000);

    // WAL falla con SQLITE_NOTADB si el archivo no es una BD válida: eso
    // convierte un archivo corrupto en un error controlado desde el open.
    for (const char* pragma :
         {"PRAGMA journal_mode=WAL;", "PRAGMA foreign_keys=ON;", "PRAGMA synchronous=NORMAL;"}) {
        if (auto result = db->exec(pragma); !result.isOk()) {
            return ResultT::err("Base de datos inválida o corrupta ('" + path +
                                "'): " + result.error().message);
        }
    }

    return ResultT::ok(std::move(db));
}

Db::~Db() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

core::Result<void> Db::exec(const std::string& sql) {
    char* message = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        std::string text = message != nullptr ? message : errorOf(db_);
        sqlite3_free(message);
        return core::Result<void>::err(text);
    }
    return core::Result<void>::ok();
}

core::Result<Statement> Db::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return core::Result<Statement>::err("Error preparando SQL: " + errorOf(db_));
    }
    return core::Result<Statement>::ok(Statement(db_, stmt));
}

core::Result<void> Db::transaction(const std::function<core::Result<void>()>& body) {
    if (auto begin = exec("BEGIN;"); !begin.isOk()) {
        return begin;
    }
    core::Result<void> result = core::Result<void>::err("cuerpo de transacción no ejecutado");
    try {
        result = body();
    } catch (const std::exception& e) {
        result = core::Result<void>::err(std::string("Excepción en transacción: ") + e.what());
    }
    if (!result.isOk()) {
        if (auto rollback = exec("ROLLBACK;"); !rollback.isOk()) {
            core::logError("ROLLBACK falló: " + rollback.error().message);
        }
        return result;
    }
    return exec("COMMIT;");
}

std::int64_t Db::lastInsertId() const {
    return sqlite3_last_insert_rowid(db_);
}

int Db::changes() const {
    return sqlite3_changes(db_);
}

}  // namespace pci::database
