#pragma once

namespace pci::repositories {
class PieceRepository;
class SettingsRepository;
class ToolRepository;
}  // namespace pci::repositories

namespace pci::ui {

// Repositorios disponibles para la UI. Cualquiera puede ser nullptr si la BD
// no pudo abrirse: la app funciona degradada sin persistencia.
struct AppRepositories {
    repositories::SettingsRepository* settings = nullptr;
    repositories::PieceRepository* pieces = nullptr;
    repositories::ToolRepository* tools = nullptr;
};

}  // namespace pci::ui
