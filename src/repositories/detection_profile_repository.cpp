#include "repositories/detection_profile_repository.h"

#include <algorithm>
#include <cstdint>

#include "database/statement.h"

namespace pci::repositories {

namespace {

// Los perfiles vienen de la BD, que puede haber sido tocada a mano o venir de
// otra versión: los valores se sanean al leerlos en vez de confiar en ellos.
vision::SegmentationOptions sanitize(std::int64_t threshold, std::int64_t polarity,
                                     std::int64_t blur, std::int64_t morph) {
    vision::SegmentationOptions options;
    options.manualThreshold =
        (threshold < 0) ? -1 : static_cast<int>(std::clamp<std::int64_t>(threshold, 0, 255));
    options.polarity = static_cast<vision::SegmentationPolarity>(
        std::clamp<std::int64_t>(polarity, 0, 2));
    options.blurKernel = static_cast<int>(std::clamp<std::int64_t>(blur, 0, 99));
    options.morphKernel = static_cast<int>(std::clamp<std::int64_t>(morph, 0, 99));
    return options;
}

}  // namespace

core::Result<std::int64_t> DetectionProfileRepository::save(
    const std::string& name, const vision::SegmentationOptions& options) {
    using ResultT = core::Result<std::int64_t>;
    if (name.empty()) {
        return ResultT::err("El perfil de detección necesita un nombre");
    }

    // Upsert por nombre: el nombre es la identidad para el operador.
    auto stmt = db_.prepare(
        "INSERT INTO DetectionProfiles (name, manual_threshold, polarity, blur_kernel, "
        "morph_kernel) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(name) DO UPDATE SET manual_threshold=excluded.manual_threshold, "
        "polarity=excluded.polarity, blur_kernel=excluded.blur_kernel, "
        "morph_kernel=excluded.morph_kernel;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    auto& s = stmt.value();
    if (auto b = s.bindText(1, name); !b.isOk()) return ResultT::err(b.error().message);
    if (auto b = s.bindInt(2, options.manualThreshold); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindInt(3, static_cast<int>(options.polarity)); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindInt(4, options.blurKernel); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto b = s.bindInt(5, options.morphKernel); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    if (auto step = s.step(); !step.isOk()) {
        return ResultT::err(step.error().message);
    }

    // Con ON CONFLICT el id insertado puede no ser el de la fila: se relee.
    auto lookup = db_.prepare("SELECT id FROM DetectionProfiles WHERE name = ?;");
    if (!lookup.isOk()) {
        return ResultT::err(lookup.error().message);
    }
    if (auto b = lookup.value().bindText(1, name); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    auto row = lookup.value().step();
    if (!row.isOk() || !row.value()) {
        return ResultT::err("No se pudo releer el perfil '" + name + "'");
    }
    return ResultT::ok(lookup.value().columnInt(0));
}

core::Result<std::vector<DetectionProfile>> DetectionProfileRepository::list() {
    using ResultT = core::Result<std::vector<DetectionProfile>>;
    auto stmt = db_.prepare(
        "SELECT id, name, manual_threshold, polarity, blur_kernel, morph_kernel "
        "FROM DetectionProfiles ORDER BY name COLLATE NOCASE;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    std::vector<DetectionProfile> profiles;
    while (true) {
        auto row = stmt.value().step();
        if (!row.isOk()) {
            return ResultT::err(row.error().message);
        }
        if (!row.value()) {
            break;
        }
        DetectionProfile profile;
        profile.id = stmt.value().columnInt(0);
        profile.name = stmt.value().columnText(1);
        profile.options = sanitize(stmt.value().columnInt(2), stmt.value().columnInt(3),
                                   stmt.value().columnInt(4), stmt.value().columnInt(5));
        profiles.push_back(std::move(profile));
    }
    return ResultT::ok(std::move(profiles));
}

core::Result<DetectionProfile> DetectionProfileRepository::load(std::int64_t profileId) {
    using ResultT = core::Result<DetectionProfile>;
    auto stmt = db_.prepare(
        "SELECT id, name, manual_threshold, polarity, blur_kernel, morph_kernel "
        "FROM DetectionProfiles WHERE id = ?;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto b = stmt.value().bindInt(1, profileId); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    auto row = stmt.value().step();
    if (!row.isOk()) {
        return ResultT::err(row.error().message);
    }
    if (!row.value()) {
        return ResultT::err("El perfil de detección " + std::to_string(profileId) +
                            " no existe");
    }
    DetectionProfile profile;
    profile.id = stmt.value().columnInt(0);
    profile.name = stmt.value().columnText(1);
    profile.options = sanitize(stmt.value().columnInt(2), stmt.value().columnInt(3),
                               stmt.value().columnInt(4), stmt.value().columnInt(5));
    return ResultT::ok(std::move(profile));
}

core::Result<void> DetectionProfileRepository::rename(std::int64_t profileId,
                                                      const std::string& newName) {
    if (newName.empty()) {
        return core::Result<void>::err("El perfil de detección necesita un nombre");
    }
    auto stmt = db_.prepare("UPDATE DetectionProfiles SET name = ? WHERE id = ?;");
    if (!stmt.isOk()) {
        return core::Result<void>::err(stmt.error().message);
    }
    if (auto b = stmt.value().bindText(1, newName); !b.isOk()) return b;
    if (auto b = stmt.value().bindInt(2, profileId); !b.isOk()) return b;
    if (auto step = stmt.value().step(); !step.isOk()) {
        return core::Result<void>::err("Ya existe un perfil con ese nombre");
    }
    return core::Result<void>::ok();
}

core::Result<void> DetectionProfileRepository::remove(std::int64_t profileId) {
    // Las piezas que lo usaban vuelven a los ajustes globales: sin esto
    // quedarían apuntando a una fila inexistente y la detección fallaría en
    // silencio la próxima vez que se seleccionaran.
    return db_.transaction([this, profileId]() -> core::Result<void> {
        auto clear = db_.prepare(
            "UPDATE Pieces SET detection_profile_id = 0 WHERE detection_profile_id = ?;");
        if (!clear.isOk()) {
            return core::Result<void>::err(clear.error().message);
        }
        if (auto b = clear.value().bindInt(1, profileId); !b.isOk()) return b;
        if (auto step = clear.value().step(); !step.isOk()) {
            return core::Result<void>::err(step.error().message);
        }

        auto stmt = db_.prepare("DELETE FROM DetectionProfiles WHERE id = ?;");
        if (!stmt.isOk()) {
            return core::Result<void>::err(stmt.error().message);
        }
        if (auto b = stmt.value().bindInt(1, profileId); !b.isOk()) return b;
        if (auto step = stmt.value().step(); !step.isOk()) {
            return core::Result<void>::err(step.error().message);
        }
        return core::Result<void>::ok();
    });
}

core::Result<void> DetectionProfileRepository::assignToPiece(std::int64_t pieceId,
                                                             std::int64_t profileId) {
    auto stmt = db_.prepare("UPDATE Pieces SET detection_profile_id = ? WHERE id = ?;");
    if (!stmt.isOk()) {
        return core::Result<void>::err(stmt.error().message);
    }
    if (auto b = stmt.value().bindInt(1, profileId); !b.isOk()) return b;
    if (auto b = stmt.value().bindInt(2, pieceId); !b.isOk()) return b;
    if (auto step = stmt.value().step(); !step.isOk()) {
        return core::Result<void>::err(step.error().message);
    }
    return core::Result<void>::ok();
}

core::Result<std::int64_t> DetectionProfileRepository::profileForPiece(std::int64_t pieceId) {
    using ResultT = core::Result<std::int64_t>;
    auto stmt = db_.prepare("SELECT detection_profile_id FROM Pieces WHERE id = ?;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto b = stmt.value().bindInt(1, pieceId); !b.isOk()) {
        return ResultT::err(b.error().message);
    }
    auto row = stmt.value().step();
    if (!row.isOk()) {
        return ResultT::err(row.error().message);
    }
    if (!row.value()) {
        return ResultT::err("La pieza " + std::to_string(pieceId) + " no existe");
    }
    return ResultT::ok(stmt.value().columnInt(0));
}

}  // namespace pci::repositories
