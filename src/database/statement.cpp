#include "database/statement.h"

#include <sqlite3.h>

#include <utility>

namespace pci::database {

namespace {

std::string errorOf(sqlite3* db) {
    const char* message = sqlite3_errmsg(db);
    return message != nullptr ? message : "error desconocido de SQLite";
}

}  // namespace

Statement::~Statement() {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
    }
}

Statement::Statement(Statement&& other) noexcept
    : db_(other.db_), stmt_(std::exchange(other.stmt_, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
        db_ = other.db_;
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

core::Result<void> Statement::checkBind(int code) const {
    if (code != SQLITE_OK) {
        return core::Result<void>::err("Error en bind: " + errorOf(db_));
    }
    return core::Result<void>::ok();
}

core::Result<void> Statement::bindInt(int index, std::int64_t value) {
    return checkBind(sqlite3_bind_int64(stmt_, index, value));
}

core::Result<void> Statement::bindDouble(int index, double value) {
    return checkBind(sqlite3_bind_double(stmt_, index, value));
}

core::Result<void> Statement::bindText(int index, const std::string& value) {
    return checkBind(sqlite3_bind_text(stmt_, index, value.c_str(),
                                       static_cast<int>(value.size()), SQLITE_TRANSIENT));
}

core::Result<void> Statement::bindBlob(int index, const std::vector<unsigned char>& value) {
    return checkBind(sqlite3_bind_blob(stmt_, index, value.data(),
                                       static_cast<int>(value.size()), SQLITE_TRANSIENT));
}

core::Result<bool> Statement::step() {
    const int code = sqlite3_step(stmt_);
    if (code == SQLITE_ROW) {
        return core::Result<bool>::ok(true);
    }
    if (code == SQLITE_DONE) {
        return core::Result<bool>::ok(false);
    }
    return core::Result<bool>::err("Error ejecutando SQL: " + errorOf(db_));
}

std::int64_t Statement::columnInt(int index) const {
    return sqlite3_column_int64(stmt_, index);
}

double Statement::columnDouble(int index) const {
    return sqlite3_column_double(stmt_, index);
}

std::string Statement::columnText(int index) const {
    const unsigned char* text = sqlite3_column_text(stmt_, index);
    return text != nullptr ? reinterpret_cast<const char*>(text) : "";
}

bool Statement::columnIsNull(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

std::vector<unsigned char> Statement::columnBlob(int index) const {
    const void* data = sqlite3_column_blob(stmt_, index);
    const int size = sqlite3_column_bytes(stmt_, index);
    if (data == nullptr || size <= 0) {
        return {};
    }
    const auto* bytes = static_cast<const unsigned char*>(data);
    return {bytes, bytes + size};
}

}  // namespace pci::database
