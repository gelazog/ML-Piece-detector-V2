#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/result.h"
#include "database/db.h"
#include "domain/measurement_mode.h"
#include "ml/reference.h"
#include "vision/board_frame.h"
#include "vision/orientation_anchor.h"

namespace pci::repositories {

struct PieceInfo {
    std::int64_t id = 0;
    std::string name;
    std::string createdAt;
};

// Cómo se mide una pieza: el modo elegido y, con él, el tablero de referencia
// (que solo tiene efecto en el modo Especial, pero se guarda siempre para no
// perder la configuración al cambiar de modo y volver).
struct PieceMeasurement {
    domain::MeasurementMode mode = domain::MeasurementMode::Real;
    vision::BoardConfig board;
    // Reglas del modo Especial (M4). 0 = no vigilar esa desviación.
    double maxOffsetPx = 0.0;   // distancia máxima del centro al cero
    double maxAngleDeg = 0.0;   // giro máximo respecto a los ejes del tablero
    // Cuántas piezas se esperan en la imagen (C5). Va con la pieza y no en los
    // ajustes globales porque "seis tornillos en bandeja" es una propiedad del
    // trabajo.
    //
    // 0 = automático: cuenta las que haya y no se queja del número.
    // N >= 1 = manual: tienen que ser exactamente N.
    //
    // De fábrica, automático. El porqué —y por qué antes era 1— está en la
    // migración v9 de `database/schema.cpp`.
    int expectedPieces = 0;
    // Si esta pieza se mira en MOSAICO: todas las piezas del encuadre
    // recortadas y numeradas, una al lado de otra.
    //
    // Va con la pieza por lo mismo que `expectedPieces`: «bandeja de cien» es
    // una propiedad del trabajo. Quien pasa de una bandeja a una pieza suelta
    // en el mismo turno no tiene por que acordarse de abrir y cerrar un panel.
    bool showMosaic = false;
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

    // LA RECETA DE MEDICIÓN de esta pieza (v15): qué cotas se proponen al
    // medir automáticamente. Vacío = ninguna, que es como se comportaba antes.
    //
    // Se guarda el NOMBRE de la receta y no un identificador: las recetas viven
    // en el código (`inspection_editor/measure_recipe.*`) y no en la base, así
    // que una tabla de recetas sería una copia que se queda vieja. Quien lea un
    // nombre que ya no existe tiene que tratarlo como «sin receta», por lo
    // mismo que un perfil de detección borrado devuelve a los ajustes globales.
    core::Result<void> saveMeasureRecipe(std::int64_t pieceId, const std::string& name);
    core::Result<std::string> loadMeasureRecipe(std::int64_t pieceId);

    // Modo de medición y tablero de la pieza (v5). Una pieza guardada antes de
    // la migración devuelve modo Real y tablero centrado en la pieza, que es
    // exactamente como se comportaba.
    core::Result<void> saveMeasurement(std::int64_t pieceId,
                                       const PieceMeasurement& measurement);
    core::Result<PieceMeasurement> loadMeasurement(std::int64_t pieceId);

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

    // VARIANTES ADMISIBLES de la misma pieza: dos acabados, dos proveedores,
    // dos lotes. Cada una conserva su media y su banda, y una pieza es buena si
    // ALGUNA la reconoce (`ml::matchVariants`).
    //
    // El motivo no es comodidad: mezclarlas en una sola media deja CIEGA la
    // referencia. La media se coloca entre los grupos, la banda se ensancha —de
    // 0,98 a 0,68 en el caso medido— y un defecto que se detectaba pasa. No da
    // falsos NG, que es lo que lo hace peligroso.
    //
    // «principal» es la de siempre, y es el valor por defecto en todas partes:
    // quien no use variantes no puede notar que esto existe.
    static constexpr const char* kMainVariant = "principal";

    core::Result<int> saveReference(std::int64_t pieceId, const ml::Reference& reference,
                                    const std::string& variant = kMainVariant);
    core::Result<StoredReference> loadLatestReference(
        std::int64_t pieceId, const std::string& variant = kMainVariant);
    core::Result<std::vector<int>> listReferenceVersions(
        std::int64_t pieceId, const std::string& variant = kMainVariant);

    // Los nombres de las variantes que tiene esta pieza, en orden alfabético.
    core::Result<std::vector<std::string>> listVariants(std::int64_t pieceId);

    // La ÚLTIMA referencia de CADA variante, que es lo que hace falta para
    // juzgar: se compara contra todas y basta que una reconozca la pieza.
    core::Result<std::vector<ml::Reference>> loadAllVariantReferences(std::int64_t pieceId);

    // Borra una variante entera, con todas sus versiones. La «principal» no se
    // puede borrar: sin ninguna referencia la pieza dejaría de poder juzgarse, y
    // eso no puede ser el resultado de quitar un acabado secundario.
    core::Result<void> deleteVariant(std::int64_t pieceId, const std::string& variant);

private:
    database::Db& db_;
};

}  // namespace pci::repositories
