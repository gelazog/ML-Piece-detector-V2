#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/result.h"

struct sqlite3;
struct sqlite3_stmt;

namespace pci::database {

// RAII de sqlite3_stmt, solo movible. Los índices de bind empiezan en 1 y los
// de columna en 0, igual que en la API C.
//
// Retiene el cerrojo de la conexión (ver `Db`) durante toda su vida: otro hilo
// no puede ejecutar nada entre el bind y el último step, ni entre el step y el
// `lastInsertId` que lo sigue.
class Statement {
public:
    Statement(sqlite3* db, sqlite3_stmt* stmt, std::unique_lock<std::recursive_mutex> lock)
        : db_(db), stmt_(stmt), lock_(std::move(lock)) {}
    ~Statement();

    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    core::Result<void> bindInt(int index, std::int64_t value);
    core::Result<void> bindDouble(int index, double value);
    core::Result<void> bindText(int index, const std::string& value);
    core::Result<void> bindBlob(int index, const std::vector<unsigned char>& value);

    // true = hay fila disponible; false = terminó sin más filas.
    core::Result<bool> step();

    [[nodiscard]] std::int64_t columnInt(int index) const;
    [[nodiscard]] double columnDouble(int index) const;
    [[nodiscard]] std::string columnText(int index) const;
    [[nodiscard]] std::vector<unsigned char> columnBlob(int index) const;
    [[nodiscard]] bool columnIsNull(int index) const;

private:
    core::Result<void> checkBind(int code) const;

    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
    // Declarado el último: se suelta DESPUÉS del sqlite3_finalize del destructor.
    std::unique_lock<std::recursive_mutex> lock_;
};

}  // namespace pci::database
