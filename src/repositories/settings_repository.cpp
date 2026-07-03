#include "repositories/settings_repository.h"

#include <cstdlib>

#include "database/statement.h"

namespace pci::repositories {

core::Result<void> SettingsRepository::setString(const std::string& key,
                                                 const std::string& value) {
    auto stmt = db_.prepare(
        "INSERT INTO Settings (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value;");
    if (!stmt.isOk()) {
        return core::Result<void>::err(stmt.error().message);
    }
    if (auto bind = stmt.value().bindText(1, key); !bind.isOk()) {
        return bind;
    }
    if (auto bind = stmt.value().bindText(2, value); !bind.isOk()) {
        return bind;
    }
    auto step = stmt.value().step();
    if (!step.isOk()) {
        return core::Result<void>::err(step.error().message);
    }
    return core::Result<void>::ok();
}

core::Result<std::string> SettingsRepository::getString(const std::string& key,
                                                        const std::string& defaultValue) {
    using ResultT = core::Result<std::string>;

    auto stmt = db_.prepare("SELECT value FROM Settings WHERE key = ?;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto bind = stmt.value().bindText(1, key); !bind.isOk()) {
        return ResultT::err(bind.error().message);
    }
    auto row = stmt.value().step();
    if (!row.isOk()) {
        return ResultT::err(row.error().message);
    }
    if (!row.value()) {
        return ResultT::ok(std::string(defaultValue));
    }
    return ResultT::ok(stmt.value().columnText(0));
}

core::Result<void> SettingsRepository::setInt(const std::string& key, int value) {
    return setString(key, std::to_string(value));
}

core::Result<int> SettingsRepository::getInt(const std::string& key, int defaultValue) {
    auto text = getString(key, std::to_string(defaultValue));
    if (!text.isOk()) {
        return core::Result<int>::err(text.error().message);
    }
    char* end = nullptr;
    const long value = std::strtol(text.value().c_str(), &end, 10);
    if (end == text.value().c_str() || (end != nullptr && *end != '\0')) {
        return core::Result<int>::ok(defaultValue);
    }
    return core::Result<int>::ok(static_cast<int>(value));
}

}  // namespace pci::repositories
