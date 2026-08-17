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

    // Restablecer: OLVIDAR los ajustes, no escribirles sus valores de fábrica.
    //
    // La diferencia es la que ya explica `remove` justo arriba, y es la que hace
    // que esto no pueda desincronizarse nunca. Cada sitio que LEE un ajuste
    // lleva su valor por defecto documentado en la propia llamada
    // —`getInt("det_blur", 5)`—, así que borrando la clave el programa vuelve
    // exactamente a lo que hace en una máquina recién instalada. Escribir aquí
    // una segunda copia de esos valores crearía dos listas que hay que mantener
    // a la vez, y a la primera que alguien cambiara una sola, «restablecer»
    // dejaría la aplicación en un estado que no es ni el suyo ni el de fábrica.
    //
    // Con `prefix` vacío se olvida TODO. Con un prefijo («det_», «cam_») solo esa
    // familia, que es lo que permite restablecer una pestaña sin tocar las demás.
    //
    // Devuelve cuántos ajustes se olvidaron: hace falta para poder decírselo al
    // operador, y «no había nada que restablecer» es una respuesta distinta de
    // «se restablecieron 14 cosas».
    core::Result<int> forget(const std::string& prefix = std::string());

    core::Result<std::vector<std::pair<std::string, std::string>>> listAll();

private:
    database::Db& db_;
};

}  // namespace pci::repositories
