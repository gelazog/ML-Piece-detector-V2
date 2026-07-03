#include "repositories/piece_repository.h"

#include <utility>

#include "database/blob_codec.h"
#include "database/statement.h"

namespace pci::repositories {

using database::blobToFloats;
using database::floatsToBlob;

core::Result<std::int64_t> PieceRepository::createPiece(const std::string& name) {
    using ResultT = core::Result<std::int64_t>;

    if (name.empty()) {
        return ResultT::err("El nombre de la pieza no puede estar vacío");
    }

    auto stmt = db_.prepare("INSERT INTO Pieces (name) VALUES (?);");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto bind = stmt.value().bindText(1, name); !bind.isOk()) {
        return ResultT::err(bind.error().message);
    }
    if (auto step = stmt.value().step(); !step.isOk()) {
        return ResultT::err("No se pudo crear la pieza '" + name +
                            "': " + step.error().message);
    }
    return ResultT::ok(db_.lastInsertId());
}

core::Result<std::vector<PieceInfo>> PieceRepository::listPieces() {
    using ResultT = core::Result<std::vector<PieceInfo>>;

    auto stmt = db_.prepare("SELECT id, name, created_at FROM Pieces ORDER BY name;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }

    std::vector<PieceInfo> pieces;
    while (true) {
        auto row = stmt.value().step();
        if (!row.isOk()) {
            return ResultT::err(row.error().message);
        }
        if (!row.value()) {
            break;
        }
        PieceInfo info;
        info.id = stmt.value().columnInt(0);
        info.name = stmt.value().columnText(1);
        info.createdAt = stmt.value().columnText(2);
        pieces.push_back(std::move(info));
    }
    return ResultT::ok(std::move(pieces));
}

core::Result<int> PieceRepository::saveReference(std::int64_t pieceId,
                                                 const ml::Reference& reference) {
    using ResultT = core::Result<int>;

    if (reference.mean.empty() || reference.mean.size() != reference.stddev.size()) {
        return ResultT::err("Referencia inválida: vectores vacíos o inconsistentes");
    }

    int newVersion = 0;
    const auto result = db_.transaction([&]() -> core::Result<void> {
        auto maxStmt = db_.prepare(
            "SELECT COALESCE(MAX(version), 0) FROM Embeddings WHERE piece_id = ?;");
        if (!maxStmt.isOk()) {
            return core::Result<void>::err(maxStmt.error().message);
        }
        if (auto bind = maxStmt.value().bindInt(1, pieceId); !bind.isOk()) {
            return bind;
        }
        auto row = maxStmt.value().step();
        if (!row.isOk() || !row.value()) {
            return core::Result<void>::err("No se pudo consultar la última versión");
        }
        newVersion = static_cast<int>(maxStmt.value().columnInt(0)) + 1;

        auto insert = db_.prepare(
            "INSERT INTO Embeddings (piece_id, version, dim, mean, stddev, sample_count, "
            "sim_mean, sim_std, sim_min) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
        if (!insert.isOk()) {
            return core::Result<void>::err(insert.error().message);
        }
        auto& s = insert.value();
        if (auto b = s.bindInt(1, pieceId); !b.isOk()) return b;
        if (auto b = s.bindInt(2, newVersion); !b.isOk()) return b;
        if (auto b = s.bindInt(3, static_cast<std::int64_t>(reference.mean.size()));
            !b.isOk()) {
            return b;
        }
        if (auto b = s.bindBlob(4, floatsToBlob(reference.mean)); !b.isOk()) return b;
        if (auto b = s.bindBlob(5, floatsToBlob(reference.stddev)); !b.isOk()) return b;
        if (auto b = s.bindInt(6, reference.sampleCount); !b.isOk()) return b;
        if (auto b = s.bindDouble(7, reference.simMean); !b.isOk()) return b;
        if (auto b = s.bindDouble(8, reference.simStd); !b.isOk()) return b;
        if (auto b = s.bindDouble(9, reference.simMin); !b.isOk()) return b;

        auto step = s.step();
        if (!step.isOk()) {
            return core::Result<void>::err("No se pudo guardar la referencia: " +
                                           step.error().message);
        }
        return core::Result<void>::ok();
    });

    if (!result.isOk()) {
        return ResultT::err(result.error().message);
    }
    return ResultT::ok(newVersion);
}

core::Result<StoredReference> PieceRepository::loadLatestReference(std::int64_t pieceId) {
    using ResultT = core::Result<StoredReference>;

    auto stmt = db_.prepare(
        "SELECT version, mean, stddev, sample_count, sim_mean, sim_std, sim_min "
        "FROM Embeddings WHERE piece_id = ? ORDER BY version DESC LIMIT 1;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto bind = stmt.value().bindInt(1, pieceId); !bind.isOk()) {
        return ResultT::err(bind.error().message);
    }
    auto row = stmt.value().step();
    if (!row.isOk()) {
        return ResultT::err(row.error().message);
    }
    if (!row.value()) {
        return ResultT::err("La pieza " + std::to_string(pieceId) +
                            " no tiene ninguna referencia guardada");
    }

    auto& s = stmt.value();
    StoredReference stored;
    stored.version = static_cast<int>(s.columnInt(0));

    auto mean = blobToFloats(s.columnBlob(1));
    if (!mean.isOk()) {
        return ResultT::err(mean.error().message);
    }
    auto stddev = blobToFloats(s.columnBlob(2));
    if (!stddev.isOk()) {
        return ResultT::err(stddev.error().message);
    }

    stored.reference.mean = std::move(mean.value());
    stored.reference.stddev = std::move(stddev.value());
    stored.reference.sampleCount = static_cast<int>(s.columnInt(3));
    stored.reference.simMean = s.columnDouble(4);
    stored.reference.simStd = s.columnDouble(5);
    stored.reference.simMin = s.columnDouble(6);
    return ResultT::ok(std::move(stored));
}

core::Result<std::vector<int>> PieceRepository::listReferenceVersions(std::int64_t pieceId) {
    using ResultT = core::Result<std::vector<int>>;

    auto stmt = db_.prepare(
        "SELECT version FROM Embeddings WHERE piece_id = ? ORDER BY version;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    if (auto bind = stmt.value().bindInt(1, pieceId); !bind.isOk()) {
        return ResultT::err(bind.error().message);
    }

    std::vector<int> versions;
    while (true) {
        auto row = stmt.value().step();
        if (!row.isOk()) {
            return ResultT::err(row.error().message);
        }
        if (!row.value()) {
            break;
        }
        versions.push_back(static_cast<int>(stmt.value().columnInt(0)));
    }
    return ResultT::ok(std::move(versions));
}

}  // namespace pci::repositories
