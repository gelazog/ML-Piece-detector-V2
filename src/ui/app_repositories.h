#pragma once

#include "engine/embed_fn.h"

namespace pci::repositories {
class DetectionProfileRepository;
class InspectionRepository;
class MeasureRecipeRepository;
class PieceRepository;
class SettingsRepository;
class ToolRepository;
}  // namespace pci::repositories

namespace pci::engine {
class InspectionEngine;
}

namespace pci::ui {

// Servicios disponibles para la UI. Cualquiera puede ser nullptr (BD sin
// abrir, modelo ONNX ausente): la app funciona degradada, nunca crashea.
struct AppRepositories {
    repositories::SettingsRepository* settings = nullptr;
    repositories::PieceRepository* pieces = nullptr;
    repositories::ToolRepository* tools = nullptr;
    repositories::InspectionRepository* inspections = nullptr;
    repositories::DetectionProfileRepository* detectionProfiles = nullptr;
    // Las recetas de medición que se guarda el operador. Las de fábrica viven
    // en el código y no pasan por aquí.
    repositories::MeasureRecipeRepository* measureRecipes = nullptr;
    engine::InspectionEngine* engine = nullptr;
    engine::EmbedFn embedFn;  // nula si el modelo no está disponible
};

}  // namespace pci::ui
