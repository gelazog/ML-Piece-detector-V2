#pragma once

#include <string>

#include "core/result.h"
#include "database/db.h"

namespace pci::repositories {

// Ajustes clave/valor (cámara elegida, umbrales, etc.).
class SettingsRepository {
public:
    explicit SettingsRepository(database::Db& db) : db_(db) {}

    core::Result<void> setString(const std::string& key, const std::string& value);
    // Devuelve defaultValue si la clave no existe.
    core::Result<std::string> getString(const std::string& key,
                                        const std::string& defaultValue = "");

    core::Result<void> setInt(const std::string& key, int value);
    core::Result<int> getInt(const std::string& key, int defaultValue = 0);

private:
    database::Db& db_;
};

}  // namespace pci::repositories
