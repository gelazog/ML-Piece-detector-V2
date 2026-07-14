#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/result.h"
#include "database/db.h"
#include "ml/reference.h"
#include "vision/orientation_anchor.h"

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
    core::Result<bool> nameExists(const std::string& name);
    core::Result<void> renamePiece(std::int64_t pieceId, const std::string& newName);
    // Elimina la pieza y, en cascada, sus referencias, herramientas e
    // historial de inspecciones. Irreversible.
    core::Result<void> removePiece(std::int64_t pieceId);

    // Ajuste manual de orientación en grados (0 = usar la detectada): gira el
    // sistema de coordenadas de la pieza para dejar el eje donde el usuario
    // quiera. Aplica en vivo, registro e inspección.
    core::Result<void> saveOrientationOffset(std::int64_t pieceId, double offsetDeg);
    core::Result<double> loadOrientationOffset(std::int64_t pieceId);

    // Miniatura JPEG de la pieza registrada (recorte normalizado), para la
    // comparación visual referencia vs pieza actual.
    core::Result<void> saveThumbnail(std::int64_t pieceId,
                                     const std::vector<unsigned char>& jpeg);
    // Vacío si la pieza no tiene miniatura guardada.
    core::Result<std::vector<unsigned char>> loadThumbnail(std::int64_t pieceId);

    // Rasgo distintivo de orientación (nullopt si la pieza no tiene uno).
    core::Result<void> saveAnchor(std::int64_t pieceId,
                                  const vision::OrientationAnchor& anchor);
    core::Result<void> clearAnchor(std::int64_t pieceId);
    core::Result<std::optional<vision::OrientationAnchor>> loadAnchor(std::int64_t pieceId);

    core::Result<int> saveReference(std::int64_t pieceId, const ml::Reference& reference);
    core::Result<StoredReference> loadLatestReference(std::int64_t pieceId);
    core::Result<std::vector<int>> listReferenceVersions(std::int64_t pieceId);

private:
    database::Db& db_;
};

}  // namespace pci::repositories
