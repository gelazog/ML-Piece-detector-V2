#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.h"
#include "database/db.h"
#include "vision/segmentation.h"

namespace pci::repositories {

// Perfil de detección con nombre: el mismo juego de ajustes de `Detección…`
// (umbral, polaridad, suavizado y morfología) guardado bajo una etiqueta que
// el operador reconoce — "luz brillante", "contraluz", "pieza negra"…
struct DetectionProfile {
    std::int64_t id = -1;  // -1 = aún no guardado
    std::string name;
    vision::SegmentationOptions options;
};

// Perfiles de detección (O3). Cada pieza puede apuntar a uno; sin perfil
// asignado se usan los ajustes globales de siempre, así que nada cambia para
// quien no los use.
class DetectionProfileRepository {
public:
    explicit DetectionProfileRepository(database::Db& db) : db_(db) {}

    // Crea o actualiza por NOMBRE (el nombre es único): guardar dos veces con
    // la misma etiqueta sobrescribe, que es lo que el operador espera.
    core::Result<std::int64_t> save(const std::string& name,
                                    const vision::SegmentationOptions& options);
    core::Result<std::vector<DetectionProfile>> list();
    core::Result<DetectionProfile> load(std::int64_t profileId);
    core::Result<void> rename(std::int64_t profileId, const std::string& newName);
    // Al borrar un perfil, las piezas que lo usaban vuelven a los ajustes
    // globales en vez de quedarse apuntando a una fila que ya no existe.
    core::Result<void> remove(std::int64_t profileId);

    // Perfil asignado a una pieza (0 = ninguno, usar los ajustes globales).
    core::Result<void> assignToPiece(std::int64_t pieceId, std::int64_t profileId);
    core::Result<std::int64_t> profileForPiece(std::int64_t pieceId);

private:
    database::Db& db_;
};

}  // namespace pci::repositories
