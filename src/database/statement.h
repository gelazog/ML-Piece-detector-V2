#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/result.h"

struct sqlite3;
struct sqlite3_stmt;

namespace pci::database {

// El último error de la conexión, en texto. Lo comparten `Db` y `Statement`.
[[nodiscard]] std::string errorOf(sqlite3* db);

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
    core::Result<void> bindNull(int index);

    // Enlaza cada argumento a su parámetro, 1..n en el orden en que se pasan, y
    // devuelve el PRIMER error: los de después ya no se intentan. Elige el bindX
    // por el tipo: enteros (bool como 0/1), double/float, texto (std::string o
    // const char*), blob (vector<unsigned char>) y nullptr para NULL.
    template <typename... Args>
    core::Result<void> bindAll(const Args&... args) {
        auto result = core::Result<void>::ok();
        [[maybe_unused]] int index = 0;
        // `&&` evalúa de izquierda a derecha y se corta en el primer false.
        static_cast<void>(((result = bindOne(++index, args)).isOk() && ...));
        return result;
    }

    // true = hay fila disponible; false = terminó sin más filas.
    core::Result<bool> step();

    [[nodiscard]] std::int64_t columnInt(int index) const;
    [[nodiscard]] double columnDouble(int index) const;
    [[nodiscard]] std::string columnText(int index) const;
    [[nodiscard]] std::vector<unsigned char> columnBlob(int index) const;
    [[nodiscard]] bool columnIsNull(int index) const;

private:
    template <typename T>
    core::Result<void> bindOne(int index, const T& value) {
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return bindNull(index);
        } else if constexpr (std::is_integral_v<T>) {
            return bindInt(index, value);
        } else if constexpr (std::is_floating_point_v<T>) {
            return bindDouble(index, value);
        } else if constexpr (std::is_same_v<T, std::vector<unsigned char>>) {
            return bindBlob(index, value);
        } else {
            return bindText(index, value);
        }
    }

    core::Result<void> checkBind(int code) const;

    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
    // Declarado el último: se suelta DESPUÉS del sqlite3_finalize del destructor.
    std::unique_lock<std::recursive_mutex> lock_;
};

}  // namespace pci::database
