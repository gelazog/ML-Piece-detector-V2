#pragma once

#include <string>
#include <utility>
#include <vector>

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

    core::Result<void> setDouble(const std::string& key, double value);
    core::Result<double> getDouble(const std::string& key, double defaultValue = 0.0);

    // Todos los ajustes tal cual están guardados, para exportarlos (O4).
    // Olvidar un ajuste, que NO es lo mismo que ponerlo a su valor por defecto:
    // varios sitios distinguen «el operador eligió esto» de «no ha elegido
    // nada», y sin borrar de verdad no hay forma de volver al segundo estado.
    // Borrar una clave que no existe no es un error.
    core::Result<void> remove(const std::string& key);

    core::Result<std::vector<std::pair<std::string, std::string>>> listAll();

private:
    database::Db& db_;
};

}  // namespace pci::repositories
