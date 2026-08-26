#include "repositories/detection_profile_repository.h"

#include <algorithm>
#include <string>
#include <array>
#include <cstdint>

#include "database/statement.h"

namespace pci::repositories {

namespace {

// Las once columnas de opciones, en un orden que comparten el guardado y la
// lectura. Escrito UNA sola vez: cuando estaban repetidas en tres consultas,
// añadir un campo a dos de ellas y olvidarlo en la tercera no se notaba.
constexpr const char* kProfileColumns =
    "manual_threshold, polarity, blur_kernel, morph_kernel, method, "
    "split_touching_pieces, recover_highlights_by, background_key, "
    "background_b, background_g, background_r";

// Los perfiles vienen de la BD, que puede haber sido tocada a mano o venir de
// otra versión: los valores se sanean al leerlos en vez de confiar en ellos.
//
// LEE LOS OCHO CAMPOS, y antes leía cuatro. Los otros cuatro —el método, la
// separación de piezas que se tocan, la recuperación de brillos y la clave de
// color de fondo— se añadieron a `SegmentationOptions` después de que existiera
// esta tabla, cada uno en su momento, y ninguno se acordó de ella.
//
// El efecto era el peor posible: el operador afina la detección, la ve
// funcionar, la guarda como perfil —«contraluz», «mesa roja»— y al volver a
// cargarla la mitad de lo que ajustó ha vuelto a fábrica. Sin fallar y sin
// avisar: solo detecta peor.
//
// Se lee por índice y en el orden de `kProfileColumns`, para que añadir un campo
// obligue a tocar los dos sitios a la vez.
vision::SegmentationOptions sanitize(database::Statement& row, int first) {
    const auto number = [&row, first](int offset) -> std::int64_t {
        return row.columnInt(first + offset);
    };
    vision::SegmentationOptions options;
    const std::int64_t threshold = number(0);
    options.manualThreshold =
        (threshold < 0) ? -1 : static_cast<int>(std::clamp<std::int64_t>(threshold, 0, 255));
    options.polarity = static_cast<vision::SegmentationPolarity>(
        std::clamp<std::int64_t>(number(1), 0, 2));
    options.blurKernel = static_cast<int>(std::clamp<std::int64_t>(number(2), 0, 99));
    options.morphKernel = static_cast<int>(std::clamp<std::int64_t>(number(3), 0, 99));
    options.method =
        static_cast<vision::SegmentationMethod>(std::clamp<std::int64_t>(number(4), 0, 1));
    options.splitTouchingPieces = number(5) != 0;
    // El aflojado va en NIVELES y no en un si/no: por encima de 20 desborda —con
    // 30, la bandeja de cien tuercas se funde en 64— así que se acota a lo medido.
    options.recoverHighlightsBy = static_cast<int>(std::clamp<std::int64_t>(number(6), 0, 20));
    options.backgroundKey = static_cast<vision::SegmentationOptions::BackgroundKey>(
        std::clamp<std::int64_t>(number(7), 0, 2));
    const auto channel = [](std::int64_t v) {
        return static_cast<unsigned char>(std::clamp<std::int64_t>(v, 0, 255));
    };
    options.background = cv::Vec3b(channel(number(8)), channel(number(9)), channel(number(10)));
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
        std::string("INSERT INTO DetectionProfiles (name, ") + kProfileColumns +
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(name) DO UPDATE SET manual_threshold=excluded.manual_threshold, "
        "polarity=excluded.polarity, blur_kernel=excluded.blur_kernel, "
        "morph_kernel=excluded.morph_kernel, method=excluded.method, "
        "split_touching_pieces=excluded.split_touching_pieces, "
        "recover_highlights_by=excluded.recover_highlights_by, "
        "background_key=excluded.background_key, background_b=excluded.background_b, "
        "background_g=excluded.background_g, background_r=excluded.background_r;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    auto& s = stmt.value();
    if (auto b = s.bindText(1, name); !b.isOk()) return ResultT::err(b.error().message);
    // En el MISMO orden que `kProfileColumns` y que `sanitize`. Los tres van
    // juntos a propósito: un campo que solo se añada a dos de los tres se pierde
    // en silencio, que es justo lo que les pasó a los cuatro que faltaban.
    const std::array<int, 11> values{
        options.manualThreshold,
        static_cast<int>(options.polarity),
        options.blurKernel,
        options.morphKernel,
        static_cast<int>(options.method),
        options.splitTouchingPieces ? 1 : 0,
        options.recoverHighlightsBy,
        static_cast<int>(options.backgroundKey),
        options.background[0],
        options.background[1],
        options.background[2],
    };
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (auto b = s.bindInt(static_cast<int>(i) + 2, values[i]); !b.isOk()) {
            return ResultT::err(b.error().message);
        }
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
        std::string("SELECT id, name, ") + kProfileColumns + " "
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
        profile.options = sanitize(stmt.value(), 2);
        profiles.push_back(std::move(profile));
    }
    return ResultT::ok(std::move(profiles));
}

core::Result<DetectionProfile> DetectionProfileRepository::load(std::int64_t profileId) {
    using ResultT = core::Result<DetectionProfile>;
    auto stmt = db_.prepare(
        std::string("SELECT id, name, ") + kProfileColumns + " "
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
    profile.options = sanitize(stmt.value(), 2);
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
