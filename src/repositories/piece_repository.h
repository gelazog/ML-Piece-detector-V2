#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.h"
#include "database/db.h"
#include "ml/reference.h"

namespace pci::repositories {

struct PieceInfo {
    std::int64_t id = 0;
    std::string name;
    std::string createdAt;
};

struct StoredReference {
    int version = 0;
    ml::Reference reference;
};

// Piezas y sus referencias de embeddings versionadas. Guardar una referencia
// siempre inserta una versión nueva; las anteriores nunca se borran.
class PieceRepository {
public:
    explicit PieceRepository(database::Db& db) : db_(db) {}

    core::Result<std::int64_t> createPiece(const std::string& name);
    core::Result<std::vector<PieceInfo>> listPieces();

    core::Result<int> saveReference(std::int64_t pieceId, const ml::Reference& reference);
    core::Result<StoredReference> loadLatestReference(std::int64_t pieceId);
    core::Result<std::vector<int>> listReferenceVersions(std::int64_t pieceId);

private:
    database::Db& db_;
};

}  // namespace pci::repositories
