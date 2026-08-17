#include "repositories/settings_repository.h"

#include <cstdlib>

#include "database/statement.h"

namespace pci::repositories {

core::Result<int> SettingsRepository::forget(const std::string& prefix) {
    using ResultT = core::Result<int>;
    // Se cuentan primero y se borran después, en vez de fiarse de `changes()`:
    // el número que se le enseña al operador tiene que ser el de ajustes que
    // había, y una consulta que cuente lo mismo que borra no deja lugar a dudas.
    auto listed = listAll();
    if (!listed.isOk()) {
        return ResultT::err(listed.error().message);
    }
    int forgotten = 0;
    for (const auto& [key, value] : listed.value()) {
        (void)value;
        if (!prefix.empty() && key.rfind(prefix, 0) != 0) {
            continue;
        }
        if (auto removed = remove(key); !removed.isOk()) {
            return ResultT::err(removed.error().message);
        }
        ++forgotten;
    }
    return ResultT::ok(forgotten);
}

core::Result<void> SettingsRepository::remove(const std::string& key) {
    auto stmt = db_.prepare("DELETE FROM Settings WHERE key = ?;");
    if (!stmt.isOk()) {
        return core::Result<void>::err(stmt.error().message);
    }
    if (auto bind = stmt.value().bindText(1, key); !bind.isOk()) {
        return bind;
    }
    auto step = stmt.value().step();
    if (!step.isOk()) {
        return core::Result<void>::err(step.error().message);
    }
    return core::Result<void>::ok();
}

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

core::Result<void> SettingsRepository::setDouble(const std::string& key, double value) {
    return setString(key, std::to_string(value));
}

core::Result<double> SettingsRepository::getDouble(const std::string& key,
                                                   double defaultValue) {
    auto text = getString(key, "");
    if (!text.isOk()) {
        return core::Result<double>::err(text.error().message);
    }
    if (text.value().empty()) {
        return core::Result<double>::ok(defaultValue);
    }
    char* end = nullptr;
    const double value = std::strtod(text.value().c_str(), &end);
    if (end == text.value().c_str()) {
        return core::Result<double>::ok(defaultValue);
    }
    return core::Result<double>::ok(value);
}

core::Result<std::vector<std::pair<std::string, std::string>>> SettingsRepository::listAll() {
    using ResultT = core::Result<std::vector<std::pair<std::string, std::string>>>;
    auto stmt = db_.prepare("SELECT key, value FROM Settings ORDER BY key;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    std::vector<std::pair<std::string, std::string>> entries;
    while (true) {
        auto row = stmt.value().step();
        if (!row.isOk()) {
            return ResultT::err(row.error().message);
        }
        if (!row.value()) {
            break;
        }
        entries.emplace_back(stmt.value().columnText(0), stmt.value().columnText(1));
    }
    return ResultT::ok(std::move(entries));
}

}  // namespace pci::repositories
