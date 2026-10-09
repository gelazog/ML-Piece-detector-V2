#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "core/result.h"
#include "database/statement.h"

struct sqlite3;

namespace pci::database {

// RAII sobre la API C de SQLite. Toda la frontera devuelve Result: una BD
// corrupta o bloqueada es un error controlado, nunca un crash.
//
// UNA conexión compartida entre hilos (GUI, inspección, análisis). SQLite no
// distingue hilos dentro de una conexión: una sentencia que llega mientras
// otro hilo tiene un BEGIN abierto pasa a formar parte de SU transacción (y se
// pierde con su ROLLBACK), y un segundo BEGIN falla con «cannot start a
// transaction within a transaction». Por eso todo acceso pasa por un mutex
// recursivo:
//   - cada `Statement` lo retiene desde `prepare` hasta que se destruye, así
//     que bind/step/column y el `lastInsertId` posterior son atómicos;
//   - una `Transaction` lo retiene desde BEGIN hasta COMMIT/ROLLBACK, así que
//     ningún otro hilo puede colar sentencias dentro.
// Recursivo porque el mismo hilo prepara sentencias dentro de su transacción.
class Db {
public:
    // Transacción RAII: retiene el cerrojo de la conexión durante toda su vida.
    // Si se destruye sin `commit()` (error, return temprano, excepción), hace
    // ROLLBACK. Solo movible.
    class Transaction {
    public:
        ~Transaction();
        Transaction(Transaction&& other) noexcept;
        Transaction& operator=(Transaction&&) = delete;
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        core::Result<void> commit();
        // Deshace explícitamente; el destructor lo hace solo si hace falta.
        void rollback();

    private:
        friend class Db;
        Transaction(Db& db, std::unique_lock<std::recursive_mutex> lock)
            : db_(&db), lock_(std::move(lock)) {}

        Db* db_ = nullptr;
        std::unique_lock<std::recursive_mutex> lock_;
        bool active_ = true;
    };

    // Abre (o crea) el archivo y aplica los PRAGMA de la conexión:
    // foreign_keys, WAL y busy_timeout. Falla de forma controlada si el
    // archivo no es una base de datos SQLite válida.
    static core::Result<std::unique_ptr<Db>> open(const std::string& path);
    ~Db();

    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    core::Result<void> exec(const std::string& sql);
    // La sentencia devuelta retiene el cerrojo de la conexión hasta destruirse:
    // no hay que guardarla más allá de la función que la usa.
    core::Result<Statement> prepare(const std::string& sql);

    // prepare + bindAll + step, para las sentencias que solo escriben. Devuelve
    // el primer error tal cual llega (de prepare, de un bind o del step). La
    // sentencia, y con ella el cerrojo, se suelta al volver: quien necesite
    // `changes()` o `lastInsertId()` de ESTA escritura tiene que usar `prepare`
    // y quedársela, o otro hilo podría escribir en medio.
    template <typename... Args>
    core::Result<void> run(const std::string& sql, const Args&... args) {
        auto stmt = prepare(sql);
        if (!stmt.isOk()) {
            return core::Result<void>::err(stmt.error().message);
        }
        if (auto bound = stmt.value().bindAll(args...); !bound.isOk()) {
            return bound;
        }
        if (auto step = stmt.value().step(); !step.isOk()) {
            return core::Result<void>::err(step.error().message);
        }
        return core::Result<void>::ok();
    }

    // Toma el cerrojo (esperando si otro hilo está en una transacción) y hace
    // BEGIN. La única forma de abrir una transacción: no ejecutes "BEGIN;" con
    // `exec`, porque el cerrojo se soltaría al volver y otro hilo se colaría.
    core::Result<Transaction> begin();

    // Ejecuta body dentro de BEGIN/COMMIT; si body falla o lanza, ROLLBACK.
    // Otros hilos esperan a que termine.
    core::Result<void> transaction(const std::function<core::Result<void>()>& body);

    [[nodiscard]] std::int64_t lastInsertId() const;
    // Filas afectadas por el último INSERT/UPDATE/DELETE.
    [[nodiscard]] int changes() const;

private:
    explicit Db(sqlite3* db) : db_(db) {}

    sqlite3* db_ = nullptr;
    mutable std::recursive_mutex mutex_;
};

}  // namespace pci::database
