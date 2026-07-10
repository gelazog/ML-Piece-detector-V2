#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.h"

struct sqlite3;
struct sqlite3_stmt;

namespace pci::database {

// RAII de sqlite3_stmt, solo movible. Los índices de bind empiezan en 1 y los
// de columna en 0, igual que en la API C.
class Statement {
public:
    Statement(sqlite3* db, sqlite3_stmt* stmt) : db_(db), stmt_(stmt) {}
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
};

}  // namespace pci::database
