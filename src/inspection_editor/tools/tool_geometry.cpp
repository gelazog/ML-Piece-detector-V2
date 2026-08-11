#include "inspection_editor/tools/tool_geometry.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <type_traits>

namespace pci::inspection {

const char* toolTypeName(ToolType type) {
    switch (type) {
        case ToolType::Caliper: return "caliper";
        case ToolType::Circle: return "circle";
        case ToolType::PointToLine: return "point_to_line";
        case ToolType::EdgeFlaw: return "edge_flaw";
        case ToolType::Blob: return "blob";
        case ToolType::Ruler: return "ruler";
        case ToolType::LineToLine: return "line_to_line";
        case ToolType::Angle: return "angle";
        case ToolType::PolyBlob: return "poly_blob";
        case ToolType::Position: return "position";
        case ToolType::Arc: return "arc";
        case ToolType::Shaft: return "shaft";
        case ToolType::Thread: return "thread";
        case ToolType::Gear: return "gear";
        case ToolType::ConstructedPoint: return "constructed_point";
        case ToolType::ConstructedLine: return "constructed_line";
    }
    return "unknown";
}

const std::array<ToolType, 16>& allToolTypes() {
    static const std::array<ToolType, 16> kTypes{
        ToolType::Caliper,  ToolType::Circle,   ToolType::PointToLine, ToolType::EdgeFlaw,
        ToolType::Blob,     ToolType::Ruler,    ToolType::LineToLine,  ToolType::Angle,
        ToolType::PolyBlob, ToolType::Position, ToolType::Arc,         ToolType::Shaft,
        ToolType::Thread,   ToolType::Gear,     ToolType::ConstructedPoint,
        ToolType::ConstructedLine};
    return kTypes;
}

core::Result<ToolType> toolTypeFromName(const std::string& name) {
    for (const ToolType type : allToolTypes()) {
        if (name == toolTypeName(type)) {
            return core::Result<ToolType>::ok(type);
        }
    }
    return core::Result<ToolType>::err("Tipo de herramienta desconocido: '" + name + "'");
}

const std::array<ToolCategory, 5>& allToolCategories() {
    static const std::array<ToolCategory, 5> kCategories{
        ToolCategory::BasicShape, ToolCategory::InLine, ToolCategory::Construction,
        ToolCategory::Gdt, ToolCategory::TurnedAndExtremes};
    return kCategories;
}

ToolCategory categoryOf(ToolType type) {
    switch (type) {
        // Forma y región: lo que describe la silueta o cuenta lo que hay dentro.
        case ToolType::Blob:
        case ToolType::PolyBlob:
        case ToolType::EdgeFlaw:
            return ToolCategory::BasicShape;
        // La cota directa, que es lo que se pide en un plano.
        case ToolType::Caliper:
        case ToolType::Circle:
        case ToolType::PointToLine:
        case ToolType::Ruler:
        case ToolType::LineToLine:
        case ToolType::Angle:
        case ToolType::Arc:
            return ToolCategory::InLine;
        // Posición es una tolerancia geométrica: mide contra una referencia.
        case ToolType::Position:
            return ToolCategory::Gdt;
        // No miden: se calculan a partir de otras y existen para ser el datum
        // contra el que miden las de GD&T.
        case ToolType::ConstructedPoint:
        case ToolType::ConstructedLine:
            return ToolCategory::Construction;
        case ToolType::Shaft:
        case ToolType::Thread:
        case ToolType::Gear:
            return ToolCategory::TurnedAndExtremes;
    }
    return ToolCategory::InLine;
}

const char* categoryLabel(ToolCategory category) {
    switch (category) {
        case ToolCategory::BasicShape: return "Figuras básicas";
        case ToolCategory::InLine: return "Medición en línea";
        case ToolCategory::Construction: return "Construcciones";
        case ToolCategory::Gdt: return "GD&T";
        case ToolCategory::TurnedAndExtremes: return "Máx./mín. y torneadas";
    }
    return "?";
}

const char* categoryDescription(ToolCategory category) {
    switch (category) {
        case ToolCategory::BasicShape:
            return "Figuras básicas — qué forma tiene la pieza y qué hay dentro:\n"
                   "área, perímetro, simetría, lados, manchas y defectos del borde.";
        case ToolCategory::InLine:
            return "Medición en línea — la cota directa, la que aparece en un plano:\n"
                   "distancias, diámetros, radios y ángulos.";
        case ToolCategory::Construction:
            return "Construcciones — puntos y rectas derivados de otras herramientas\n"
                   "(punto medio, intersección, bisectriz, eje medio). No miden por sí\n"
                   "solas: existen para servir de referencia a las de GD&T.";
        case ToolCategory::Gdt:
            return "GD&T — tolerancias geométricas medidas contra una referencia\n"
                   "declarada: rectitud, redondez, paralelismo, posición verdadera.\n"
                   "Sobre una silueta 2D hay cotas que no se pueden dar, y se dice.";
        case ToolCategory::TurnedAndExtremes:
            return "Máximos, mínimos y piezas torneadas — la medida más grande y la\n"
                   "más pequeña en cualquier dirección, y lo propio del torno:\n"
                   "diámetros, chaflanes, roscas y engranajes.";
    }
    return "";
}

std::vector<ToolType> toolsInCategory(ToolCategory category) {
    std::vector<ToolType> tools;
    for (const ToolType type : allToolTypes()) {
        if (categoryOf(type) == category) {
            tools.push_back(type);
        }
    }
    return tools;
}

std::string paramsWithReferences(const ToolReferences& references) {
    if (references.first.empty() && references.second.empty()) {
        return "{}";
    }
    cv::FileStorage fs(".json", cv::FileStorage::WRITE | cv::FileStorage::MEMORY);
    // Se escriben siempre las dos claves cuando hay alguna: leer una clave
    // ausente ya se trata como vacía, pero un fichero con la mitad de las
    // claves invita a suponer que la otra "no aplica" en vez de "está vacía".
    fs << "ref" << references.first;
    fs << "ref2" << references.second;
    return fs.releaseAndGetString();
}

ToolReferences referencesFromParams(const std::string& paramsJson) {
    if (paramsJson.empty() || paramsJson == "{}") {
        return {};
    }
    try {
        cv::FileStorage fs(paramsJson, cv::FileStorage::READ | cv::FileStorage::MEMORY);
        if (!fs.isOpened()) {
            return {};
        }
        const auto read = [&fs](const char* key) -> std::string {
            const cv::FileNode node = fs[key];
            if (node.empty() || !node.isString()) {
                return {};
            }
            return static_cast<std::string>(node);
        };
        return ToolReferences{read("ref"), read("ref2")};
    } catch (const cv::Exception&) {
        // Parámetros corruptos: se ignoran. Una referencia ilegible hace que la
        // herramienta se comporte como si no la tuviera, y eso se nota al
        // medir; reventar aquí tumbaría la carga de la plantilla entera.
        return {};
    }
}

const std::array<PointConstruction, 4>& allPointConstructions() {
    static const std::array<PointConstruction, 4> kModes{
        PointConstruction::Midpoint, PointConstruction::Intersection,
        PointConstruction::Projection, PointConstruction::CircleCenter};
    return kModes;
}

const std::array<LineConstruction, 4>& allLineConstructions() {
    static const std::array<LineConstruction, 4> kModes{
        LineConstruction::ThroughTwoPoints, LineConstruction::Bisector,
        LineConstruction::ParallelThrough, LineConstruction::PerpendicularThrough};
    return kModes;
}

const char* constructionLabel(PointConstruction mode) {
    switch (mode) {
        case PointConstruction::Midpoint: return "Punto medio de dos";
        case PointConstruction::Intersection: return "Intersección de dos rectas";
        case PointConstruction::Projection: return "Proyección de un punto sobre una recta";
        case PointConstruction::CircleCenter: return "Centro de un círculo";
    }
    return "?";
}

const char* constructionLabel(LineConstruction mode) {
    switch (mode) {
        case LineConstruction::ThroughTwoPoints: return "Recta por dos puntos";
        case LineConstruction::Bisector: return "Bisectriz de dos rectas";
        case LineConstruction::ParallelThrough: return "Paralela por un punto";
        case LineConstruction::PerpendicularThrough: return "Perpendicular por un punto";
    }
    return "?";
}

std::array<OperandKind, 2> operandsOf(PointConstruction mode) {
    switch (mode) {
        case PointConstruction::Midpoint:
            return {OperandKind::Point, OperandKind::Point};
        case PointConstruction::Intersection:
            return {OperandKind::Line, OperandKind::Line};
        case PointConstruction::Projection:
            return {OperandKind::Point, OperandKind::Line};
        case PointConstruction::CircleCenter:
            return {OperandKind::Circle, OperandKind::Unused};
    }
    return {OperandKind::Unused, OperandKind::Unused};
}

std::array<OperandKind, 2> operandsOf(LineConstruction mode) {
    switch (mode) {
        case LineConstruction::ThroughTwoPoints:
            return {OperandKind::Point, OperandKind::Point};
        case LineConstruction::Bisector:
            return {OperandKind::Line, OperandKind::Line};
        case LineConstruction::ParallelThrough:
        case LineConstruction::PerpendicularThrough:
            return {OperandKind::Line, OperandKind::Point};
    }
    return {OperandKind::Unused, OperandKind::Unused};
}

const char* operandKindLabel(OperandKind kind) {
    switch (kind) {
        // "con punto" y no "punto" porque un Círculo vale donde se pide un
        // punto: aporta su centro. Decir "punto" a secas haría pensar que no.
        case OperandKind::Point: return "una herramienta con punto (o un círculo)";
        case OperandKind::Line: return "una herramienta con recta";
        case OperandKind::Circle: return "un círculo";
        case OperandKind::Unused: return "nada";
    }
    return "?";
}

const char* toolTypeLabel(ToolType type) {
    switch (type) {
        case ToolType::Caliper: return "Caliper";
        case ToolType::Circle: return "Círculo";
        case ToolType::PointToLine: return "Punto-Línea";
        case ToolType::EdgeFlaw: return "Borde liso";
        case ToolType::Blob: return "Blob";
        case ToolType::Ruler: return "Regla";
        case ToolType::LineToLine: return "Línea-Línea";
        case ToolType::Angle: return "Ángulo";
        case ToolType::PolyBlob: return "Blob poligonal";
        case ToolType::Position: return "Posición";
        case ToolType::Arc: return "Arco";
        case ToolType::Shaft: return "Eje / Diámetro";
        case ToolType::Thread: return "Rosca";
        case ToolType::Gear: return "Engranaje";
        case ToolType::ConstructedPoint: return "Punto construido";
        case ToolType::ConstructedLine: return "Recta construida";
    }
    return "?";
}

const char* toolTypeDescription(ToolType type) {
    switch (type) {
        case ToolType::Caliper:
            return "Caliper — mide la distancia entre dos bordes (px).\n"
                   "Dibuja una línea que CRUCE perpendicularmente los dos bordes a medir\n"
                   "(p. ej. de lado a lado del ancho de un brazo o una ranura).";
        case ToolType::Circle:
            return "Círculo — mide el diámetro y la redondez de un contorno circular.\n"
                   "Arrastra desde el CENTRO del círculo (o agujero) hasta su borde;\n"
                   "el borde se busca en una banda alrededor de ese radio.";
        case ToolType::PointToLine:
            return "Punto-Línea — mide la distancia perpendicular de un borde a una\n"
                   "línea de referencia. Dibuja la línea de referencia; el escaneo que\n"
                   "localiza el borde queda perpendicular en su punto medio\n"
                   "(muévelo con Mover/Elegir si hace falta).";
        case ToolType::EdgeFlaw:
            return "Borde liso — detecta irregularidades (muescas, rebabas, golpes) en\n"
                   "un borde que debería ser recto. Dibuja una línea SOBRE el borde a\n"
                   "vigilar; se mide la desviación máxima respecto a la recta ideal.\n"
                   "OJO: solo ve lo que cae dentro del largo de escaneo (campo\n"
                   "Escaneos/largo); una muesca más profunda que esa ventana pasa\n"
                   "desapercibida — súbelo si esperas defectos grandes.";
        case ToolType::Blob:
            return "Blob — cuenta manchas, agujeros o elementos dentro de una región.\n"
                   "Arrastra un rectángulo sobre la zona a vigilar; por defecto busca\n"
                   "elementos oscuros sobre fondo claro (área mínima 20 px²).";
        case ToolType::Ruler:
            return "Regla — distancia directa entre dos puntos fijos de la pieza\n"
                   "(no busca bordes: mide exactamente lo que trazas). Con la escala\n"
                   "calibrada, la medida sale en mm/cm. Ideal para medir al vuelo.";
        case ToolType::LineToLine:
            return "Línea-Línea — ángulo entre dos líneas de referencia.\n"
                   "Traza la primera línea y luego la segunda (dos arrastres); mide el\n"
                   "ángulo entre ambas en grados y también su separación. Útil para\n"
                   "verificar paralelismo o el ángulo entre dos bordes de la pieza.";
        case ToolType::Angle:
            return "Ángulo — mide el ángulo de una esquina en grados.\n"
                   "Arrastra del VÉRTICE al extremo del primer lado y luego marca el\n"
                   "extremo del segundo lado; se mide el ángulo interior (0°..180°)\n"
                   "con tolerancia en grados. Ideal para chaflanes y esquinas.";
        case ToolType::PolyBlob:
            return "Blob poligonal — cuenta manchas dentro de una región de forma\n"
                   "libre. Haz clic para ir marcando los vértices del polígono y\n"
                   "cierra haciendo clic sobre el primero. Igual que el Blob pero\n"
                   "para zonas irregulares que un rectángulo no cubre bien.";
        case ToolType::Position:
            return "Posición — vigila DÓNDE cae un rasgo respecto al cero del tablero\n"
                   "de referencia (Ver ▸ Tablero). Marca el rasgo con un clic-arrastre;\n"
                   "se mide su desviación (radial, en X o en Y) y se compara con las\n"
                   "tolerancias. Con el cero en la pieza la desviación es fija: usa el\n"
                   "centro de la imagen o un punto fijado para que signifique algo.";
        case ToolType::Arc:
            return "Arco — mide el RADIO de una esquina redondeada o un redondeo.\n"
                   "Marca tres puntos SOBRE el arco: los dos extremos y uno\n"
                   "intermedio, igual que al comprobarlo con una plantilla de radios.\n"
                   "El Círculo no sirve aquí: pide un centro y un contorno cerrado,\n"
                   "y en una esquina no hay ninguno de los dos.";
        case ToolType::Shaft:
            return "Eje / Diámetro — para piezas de torno vistas de perfil.\n"
                   "Traza el EJE a lo largo de la pieza, por el medio; se exploran\n"
                   "los dos bordes y se miden de una vez el DIÁMETRO, la CONICIDAD\n"
                   "(si no es cilíndrica) y la RECTITUD. Un calíper mide en un solo\n"
                   "punto y ahí no se distingue un cilindro de un cono.";
        case ToolType::Thread:
            return "Rosca — mide PASO, diámetro exterior y de fondo, y ángulo de\n"
                   "flanco de un tornillo visto DE PERFIL. Traza el eje a lo largo\n"
                   "de la parte roscada: el perfil se repite una vez por vuelta, y\n"
                   "de ese periodo sale el paso. Con la escala calibrada propone\n"
                   "además la designación métrica (M6×1, M8×1.25...).\n"
                   "Necesita ver varias vueltas y buen contraste de borde.";
        case ToolType::Gear:
            return "Engranaje — cuenta los DIENTES y mide diámetro de cabeza, de\n"
                   "raíz, módulo y excentricidad. La rueda debe verse DE CARA.\n"
                   "Arrastra del centro del engranaje hacia fuera, pasando la punta\n"
                   "de los dientes; el perfil radial se repite una vez por diente.\n"
                   "El módulo exige calibración px→mm: sin escala real no existe.";
        case ToolType::ConstructedPoint:
            return "Punto construido — NO mide: calcula un punto a partir de otras\n"
                   "herramientas para que sirva de referencia. Punto medio de dos,\n"
                   "corte de dos rectas, proyección de un punto sobre una recta o\n"
                   "centro de un círculo. Colócalo con un clic (solo fija dónde se\n"
                   "escribe) y elige la construcción y sus dos referencias.";
        case ToolType::ConstructedLine:
            return "Recta construida — NO mide: calcula una recta a partir de otras\n"
                   "herramientas para usarla como DATUM. Por dos puntos, bisectriz de\n"
                   "dos rectas (si son paralelas, la recta media), o paralela y\n"
                   "perpendicular a una recta por un punto. Es lo que le falta al\n"
                   "GD&T para medir contra algo declarado y no contra un supuesto.";
    }
    return "";
}

void suggestTolerances(ToolType type, double measured, double& toleranceMin,
                       double& toleranceMax) {
    switch (type) {
        case ToolType::Blob:
        case ToolType::PolyBlob:
        case ToolType::Gear:
            // Conteo: se exige exactamente lo que hay en la pieza buena. En el
            // engranaje eso son los dientes, que o son los que toca o la rueda
            // es otra.
            toleranceMin = measured;
            toleranceMax = measured;
            return;
        case ToolType::EdgeFlaw:
            // Desviación: la pieza buena define el piso; techo holgado.
            toleranceMin = 0.0;
            toleranceMax = std::max(measured * 1.5, 2.0);
            return;
        case ToolType::Position:
            // Desviación respecto al cero: se admite desde 0 hasta un margen
            // alrededor de donde cayó la pieza buena.
            toleranceMin = 0.0;
            toleranceMax = std::max(measured * 1.25, 5.0);
            return;
        case ToolType::ConstructedPoint:
        case ToolType::ConstructedLine:
            // Una construcción no se juzga: no hay medida que pueda estar fuera
            // de tolerancia, solo un elemento que se pudo calcular o no. Se deja
            // la banda abierta para que nunca sea la causa de un NG; lo que sí
            // es NG es que no se pueda construir, y eso lo dice `ok=false`.
            toleranceMin = 0.0;
            toleranceMax = 1e9;
            return;
        case ToolType::LineToLine:
        case ToolType::Angle: {
            // Ángulo en grados: banda de ±2° alrededor del valor de la pieza buena.
            const double band = 2.0;
            toleranceMin = std::max(0.0, measured - band);
            toleranceMax = measured + band;
            return;
        }
        case ToolType::Caliper:
        case ToolType::Circle:
        case ToolType::PointToLine:
        case ToolType::Arc:
        case ToolType::Shaft:
        case ToolType::Thread:
        case ToolType::Ruler: {
            // Banda de ±10% con un mínimo de ±2 px para medidas pequeñas.
            const double band = std::max(measured * 0.10, 2.0);
            toleranceMin = std::max(0.0, measured - band);
            toleranceMax = measured + band;
            return;
        }
    }
}

ToolType typeOf(const ToolGeometry& geometry) {
    return std::visit(
        [](const auto& g) -> ToolType {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry>) {
                return ToolType::Caliper;
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                return ToolType::Circle;
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                return ToolType::PointToLine;
            } else if constexpr (std::is_same_v<T, EdgeFlawGeometry>) {
                return ToolType::EdgeFlaw;
            } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                return ToolType::Blob;
            } else if constexpr (std::is_same_v<T, RulerGeometry>) {
                return ToolType::Ruler;
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                return ToolType::LineToLine;
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                return ToolType::Angle;
            } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                return ToolType::PolyBlob;
            } else if constexpr (std::is_same_v<T, PositionGeometry>) {
                return ToolType::Position;
            } else if constexpr (std::is_same_v<T, ArcGeometry>) {
                return ToolType::Arc;
            } else if constexpr (std::is_same_v<T, ShaftGeometry>) {
                return ToolType::Shaft;
            } else if constexpr (std::is_same_v<T, ThreadGeometry>) {
                return ToolType::Thread;
            } else if constexpr (std::is_same_v<T, GearGeometry>) {
                return ToolType::Gear;
            } else if constexpr (std::is_same_v<T, ConstructedPointGeometry>) {
                return ToolType::ConstructedPoint;
            } else if constexpr (std::is_same_v<T, ConstructedLineGeometry>) {
                return ToolType::ConstructedLine;
            } else {
                // Sin rama genérica a propósito. Antes esta cadena acababa en un
                // `else` que devolvía Position, así que al añadir un tipo nuevo
                // la herramienta se reportaba como Posición sin que nada
                // fallara al compilar. Ahora no compila hasta que se le asigne
                // su ToolType.
                static_assert(alwaysFalse<T>, "geometría sin ToolType asignado");
            }
        },
        geometry);
}

void translateGeometry(ToolGeometry& geometry, const cv::Point2f& delta) {
    std::visit(
        [&delta](auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry> ||
                          std::is_same_v<T, EdgeFlawGeometry> ||
                          std::is_same_v<T, RulerGeometry>) {
                g.p0 += delta;
                g.p1 += delta;
            } else if constexpr (std::is_same_v<T, CircleGeometry> ||
                                 std::is_same_v<T, BlobGeometry>) {
                g.center += delta;
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                g.lineA += delta;
                g.lineB += delta;
                g.scanA += delta;
                g.scanB += delta;
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                g.a0 += delta;
                g.a1 += delta;
                g.b0 += delta;
                g.b1 += delta;
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                g.vertex += delta;
                g.end0 += delta;
                g.end1 += delta;
            } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                for (auto& v : g.vertices) {
                    v += delta;
                }
            } else if constexpr (std::is_same_v<T, PositionGeometry>) {
                g.point += delta;
            } else if constexpr (std::is_same_v<T, ArcGeometry>) {
                g.start += delta;
                g.mid += delta;
                g.end += delta;
            } else if constexpr (std::is_same_v<T, ShaftGeometry> ||
                                 std::is_same_v<T, ThreadGeometry>) {
                g.axisFrom += delta;
                g.axisTo += delta;
            } else if constexpr (std::is_same_v<T, GearGeometry>) {
                g.center += delta;
            } else if constexpr (std::is_same_v<T, ConstructedPointGeometry> ||
                                 std::is_same_v<T, ConstructedLineGeometry>) {
                // Solo se mueve la etiqueta: el elemento lo dictan las
                // referencias, y arrastrarlo no puede cambiar dónde cae. Que se
                // deje mover es a propósito — la etiqueta estorba a menudo.
                g.anchor += delta;
            } else {
                // Igual que en typeOf: esta cadena no puede acabar sin rama. Al
                // no tener `else`, un tipo nuevo simplemente NO se trasladaba —
                // la herramienta se quedaba clavada al arrastrarla y nada
                // fallaba al compilar.
                static_assert(alwaysFalse<T>, "geometría que no sabe trasladarse");
            }
        },
        geometry);
}

namespace {

std::string writeJson(const std::function<void(cv::FileStorage&)>& body) {
    cv::FileStorage fs("{}", cv::FileStorage::WRITE | cv::FileStorage::MEMORY |
                                 cv::FileStorage::FORMAT_JSON);
    body(fs);
    return fs.releaseAndGetString();
}

// Lectura con validación: una clave ausente es un error controlado, no un 0.
class JsonReader {
public:
    explicit JsonReader(const std::string& json)
        : fs_(json,
              cv::FileStorage::READ | cv::FileStorage::MEMORY | cv::FileStorage::FORMAT_JSON) {}

    core::Result<double> number(const char* key) {
        const cv::FileNode node = fs_[key];
        if (node.empty() || !node.isReal()) {
            if (node.empty() || !node.isInt()) {
                return core::Result<double>::err(std::string("Geometría corrupta: falta '") +
                                                 key + "'");
            }
        }
        const double value = static_cast<double>(node.real());
        // Un JSON con 1e400 desborda a infinito y OpenCV lo acepta sin rechistar.
        // Si se dejara pasar, la herramienta quedaría en el infinito y todas sus
        // medidas saldrían NaN: es preferible rechazar la geometría entera.
        if (!std::isfinite(value)) {
            return core::Result<double>::err(std::string("Geometría corrupta: '") + key +
                                             "' no es un número finito");
        }
        return core::Result<double>::ok(value);
    }

    // Clave opcional (campos añadidos después de la v1 del formato).
    double numberOr(const char* key, double fallback) {
        const cv::FileNode node = fs_[key];
        if (node.empty() || (!node.isReal() && !node.isInt())) {
            return fallback;
        }
        const double value = static_cast<double>(node.real());
        return std::isfinite(value) ? value : fallback;
    }

    // Secuencia plana [x0,y0,x1,y1,...] como lista de puntos.
    std::vector<cv::Point2f> points(const char* key) {
        std::vector<cv::Point2f> pts;
        const cv::FileNode node = fs_[key];
        if (node.empty() || !node.isSeq()) {
            return pts;
        }
        std::vector<double> flat;
        for (const auto& n : node) {
            if (n.isReal() || n.isInt()) {
                flat.push_back(static_cast<double>(n.real()));
            }
        }
        for (std::size_t i = 0; i + 1 < flat.size(); i += 2) {
            if (!std::isfinite(flat[i]) || !std::isfinite(flat[i + 1])) {
                return {};  // un vértice no finito invalida el polígono entero
            }
            pts.emplace_back(static_cast<float>(flat[i]), static_cast<float>(flat[i + 1]));
        }
        return pts;
    }

private:
    cv::FileStorage fs_;
};

// Lee el modo de una construcción comprobando que el número guardado sea uno de
// los que existen. `numberOr` con un valor por defecto no sirve aquí: un modo
// desconocido —una plantilla escrita por una versión posterior, unos params
// tocados a mano— se degradaría en silencio al primero de la lista, y la
// herramienta calcularía una cosa distinta de la que el operador configuró sin
// que nada lo dijera.
template <typename Mode, std::size_t N>
core::Result<Mode> readConstruction(JsonReader& reader, const std::array<Mode, N>& modes) {
    const auto raw = reader.number("mode");
    if (!raw.isOk()) {
        return core::Result<Mode>::err(raw.error().message);
    }
    const int value = static_cast<int>(raw.value());
    for (const Mode mode : modes) {
        if (static_cast<int>(mode) == value) {
            return core::Result<Mode>::ok(mode);
        }
    }
    return core::Result<Mode>::err("Construcción desconocida: " + std::to_string(value));
}

}  // namespace

std::string toJson(const ToolGeometry& geometry) {
    return std::visit(
        [](const auto& g) -> std::string {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "x0" << g.p0.x << "y0" << g.p0.y << "x1" << g.p1.x << "y1" << g.p1.y
                       << "band" << g.bandWidth;
                });
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "r" << g.radius << "band"
                       << g.searchBand << "rays" << g.rayCount;
                });
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "lax" << g.lineA.x << "lay" << g.lineA.y << "lbx" << g.lineB.x
                       << "lby" << g.lineB.y << "sax" << g.scanA.x << "say" << g.scanA.y
                       << "sbx" << g.scanB.x << "sby" << g.scanB.y;
                });
            } else if constexpr (std::is_same_v<T, EdgeFlawGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "x0" << g.p0.x << "y0" << g.p0.y << "x1" << g.p1.x << "y1" << g.p1.y
                       << "scanLen" << g.scanLength << "scans" << g.scanCount;
                });
            } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "w" << g.width << "h"
                       << g.height << "minArea" << g.minArea << "dark" << (g.darkBlobs ? 1 : 0);
                });
            } else if constexpr (std::is_same_v<T, RulerGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "x0" << g.p0.x << "y0" << g.p0.y << "x1" << g.p1.x << "y1" << g.p1.y;
                });
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "ax0" << g.a0.x << "ay0" << g.a0.y << "ax1" << g.a1.x << "ay1"
                       << g.a1.y << "bx0" << g.b0.x << "by0" << g.b0.y << "bx1" << g.b1.x
                       << "by1" << g.b1.y;
                });
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "vx" << g.vertex.x << "vy" << g.vertex.y << "e0x" << g.end0.x
                       << "e0y" << g.end0.y << "e1x" << g.end1.x << "e1y" << g.end1.y;
                });
            } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    // Los vértices van como secuencia plana [x0,y0,x1,y1,...].
                    fs << "verts" << "[:";
                    for (const auto& v : g.vertices) {
                        fs << v.x << v.y;
                    }
                    fs << "]";
                    fs << "minArea" << g.minArea << "dark" << (g.darkBlobs ? 1 : 0);
                });
            } else if constexpr (std::is_same_v<T, PositionGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "px" << g.point.x << "py" << g.point.y << "axis"
                       << static_cast<int>(g.axis);
                });
            } else if constexpr (std::is_same_v<T, ArcGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "sx" << g.start.x << "sy" << g.start.y << "mx" << g.mid.x << "my"
                       << g.mid.y << "ex" << g.end.x << "ey" << g.end.y << "band"
                       << g.searchBand << "rays" << g.rayCount;
                });
            } else if constexpr (std::is_same_v<T, ShaftGeometry> ||
                                 std::is_same_v<T, ThreadGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "ax" << g.axisFrom.x << "ay" << g.axisFrom.y << "bx" << g.axisTo.x
                       << "by" << g.axisTo.y << "band" << g.searchBand << "stations"
                       << g.stations;
                });
            } else if constexpr (std::is_same_v<T, GearGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "cx" << g.center.x << "cy" << g.center.y << "rin"
                       << g.innerRadius << "rout" << g.outerRadius << "rays" << g.rayCount;
                });
            } else if constexpr (std::is_same_v<T, ConstructedPointGeometry> ||
                                 std::is_same_v<T, ConstructedLineGeometry>) {
                return writeJson([&](cv::FileStorage& fs) {
                    fs << "mode" << static_cast<int>(g.mode) << "ax" << g.anchor.x << "ay"
                       << g.anchor.y;
                });
            } else {
                static_assert(alwaysFalse<T>, "geometría que no sabe serializarse");
            }
        },
        geometry);
}

core::Result<ToolGeometry> geometryFromJson(ToolType type, const std::string& json) {
    using ResultT = core::Result<ToolGeometry>;

    try {
        JsonReader reader(json);
        auto f = [&reader](const char* key) { return reader.number(key); };

        switch (type) {
            case ToolType::Caliper: {
                CaliperGeometry g;
                auto x0 = f("x0"), y0 = f("y0"), x1 = f("x1"), y1 = f("y1"), band = f("band");
                for (const auto* r : {&x0, &y0, &x1, &y1, &band}) {
                    if (!r->isOk()) return ResultT::err(r->error().message);
                }
                g.p0 = {static_cast<float>(x0.value()), static_cast<float>(y0.value())};
                g.p1 = {static_cast<float>(x1.value()), static_cast<float>(y1.value())};
                g.bandWidth = static_cast<float>(band.value());
                return ResultT::ok(g);
            }
            case ToolType::Circle: {
                CircleGeometry g;
                auto cx = f("cx"), cy = f("cy"), r = f("r"), band = f("band");
                for (const auto* v : {&cx, &cy, &r, &band}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.radius = static_cast<float>(r.value());
                g.searchBand = static_cast<float>(band.value());
                // "rays" llegó después: los JSON viejos usan el valor por defecto.
                g.rayCount = static_cast<int>(reader.numberOr("rays", g.rayCount));
                return ResultT::ok(g);
            }
            case ToolType::PointToLine: {
                PointToLineGeometry g;
                auto lax = f("lax"), lay = f("lay"), lbx = f("lbx"), lby = f("lby");
                auto sax = f("sax"), say = f("say"), sbx = f("sbx"), sby = f("sby");
                for (const auto* v : {&lax, &lay, &lbx, &lby, &sax, &say, &sbx, &sby}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.lineA = {static_cast<float>(lax.value()), static_cast<float>(lay.value())};
                g.lineB = {static_cast<float>(lbx.value()), static_cast<float>(lby.value())};
                g.scanA = {static_cast<float>(sax.value()), static_cast<float>(say.value())};
                g.scanB = {static_cast<float>(sbx.value()), static_cast<float>(sby.value())};
                return ResultT::ok(g);
            }
            case ToolType::EdgeFlaw: {
                EdgeFlawGeometry g;
                auto x0 = f("x0"), y0 = f("y0"), x1 = f("x1"), y1 = f("y1");
                auto len = f("scanLen"), scans = f("scans");
                for (const auto* v : {&x0, &y0, &x1, &y1, &len, &scans}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.p0 = {static_cast<float>(x0.value()), static_cast<float>(y0.value())};
                g.p1 = {static_cast<float>(x1.value()), static_cast<float>(y1.value())};
                g.scanLength = static_cast<float>(len.value());
                g.scanCount = static_cast<int>(scans.value());
                return ResultT::ok(g);
            }
            case ToolType::Ruler: {
                RulerGeometry g;
                auto x0 = f("x0"), y0 = f("y0"), x1 = f("x1"), y1 = f("y1");
                for (const auto* v : {&x0, &y0, &x1, &y1}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.p0 = {static_cast<float>(x0.value()), static_cast<float>(y0.value())};
                g.p1 = {static_cast<float>(x1.value()), static_cast<float>(y1.value())};
                return ResultT::ok(g);
            }
            case ToolType::Blob: {
                BlobGeometry g;
                auto cx = f("cx"), cy = f("cy"), w = f("w"), h = f("h");
                auto minArea = f("minArea"), dark = f("dark");
                for (const auto* v : {&cx, &cy, &w, &h, &minArea, &dark}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.width = static_cast<float>(w.value());
                g.height = static_cast<float>(h.value());
                g.minArea = static_cast<float>(minArea.value());
                g.darkBlobs = dark.value() != 0.0;
                return ResultT::ok(g);
            }
            case ToolType::LineToLine: {
                LineToLineGeometry g;
                auto ax0 = f("ax0"), ay0 = f("ay0"), ax1 = f("ax1"), ay1 = f("ay1");
                auto bx0 = f("bx0"), by0 = f("by0"), bx1 = f("bx1"), by1 = f("by1");
                for (const auto* v : {&ax0, &ay0, &ax1, &ay1, &bx0, &by0, &bx1, &by1}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.a0 = {static_cast<float>(ax0.value()), static_cast<float>(ay0.value())};
                g.a1 = {static_cast<float>(ax1.value()), static_cast<float>(ay1.value())};
                g.b0 = {static_cast<float>(bx0.value()), static_cast<float>(by0.value())};
                g.b1 = {static_cast<float>(bx1.value()), static_cast<float>(by1.value())};
                return ResultT::ok(g);
            }
            case ToolType::Angle: {
                AngleGeometry g;
                auto vx = f("vx"), vy = f("vy"), e0x = f("e0x"), e0y = f("e0y");
                auto e1x = f("e1x"), e1y = f("e1y");
                for (const auto* v : {&vx, &vy, &e0x, &e0y, &e1x, &e1y}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.vertex = {static_cast<float>(vx.value()), static_cast<float>(vy.value())};
                g.end0 = {static_cast<float>(e0x.value()), static_cast<float>(e0y.value())};
                g.end1 = {static_cast<float>(e1x.value()), static_cast<float>(e1y.value())};
                return ResultT::ok(g);
            }
            case ToolType::PolyBlob: {
                PolyBlobGeometry g;
                g.vertices = reader.points("verts");
                if (g.vertices.size() < 3) {
                    return ResultT::err("Blob poligonal: se necesitan al menos 3 vértices");
                }
                auto minArea = f("minArea"), dark = f("dark");
                for (const auto* v : {&minArea, &dark}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.minArea = static_cast<float>(minArea.value());
                g.darkBlobs = dark.value() != 0.0;
                return ResultT::ok(g);
            }
            case ToolType::Position: {
                PositionGeometry g;
                auto px = f("px"), py = f("py");
                for (const auto* v : {&px, &py}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.point = {static_cast<float>(px.value()), static_cast<float>(py.value())};
                const int axis = static_cast<int>(reader.numberOr("axis", 0.0));
                g.axis = (axis == 1)   ? PositionAxis::X
                         : (axis == 2) ? PositionAxis::Y
                                       : PositionAxis::Radial;
                return ResultT::ok(g);
            }
            case ToolType::Arc: {
                ArcGeometry g;
                auto sx = f("sx"), sy = f("sy"), mx = f("mx"), my = f("my"), ex = f("ex"),
                     ey = f("ey"), band = f("band");
                for (const auto* v : {&sx, &sy, &mx, &my, &ex, &ey, &band}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.start = {static_cast<float>(sx.value()), static_cast<float>(sy.value())};
                g.mid = {static_cast<float>(mx.value()), static_cast<float>(my.value())};
                g.end = {static_cast<float>(ex.value()), static_cast<float>(ey.value())};
                g.searchBand = static_cast<float>(band.value());
                g.rayCount = static_cast<int>(reader.numberOr("rays", 24.0));
                return ResultT::ok(g);
            }
            case ToolType::Shaft: {
                ShaftGeometry g;
                auto ax = f("ax"), ay = f("ay"), bx = f("bx"), by = f("by"), band = f("band");
                for (const auto* v : {&ax, &ay, &bx, &by, &band}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.axisFrom = {static_cast<float>(ax.value()), static_cast<float>(ay.value())};
                g.axisTo = {static_cast<float>(bx.value()), static_cast<float>(by.value())};
                g.searchBand = static_cast<float>(band.value());
                g.stations = static_cast<int>(reader.numberOr("stations", 32.0));
                return ResultT::ok(g);
            }
            case ToolType::Thread: {
                ThreadGeometry g;
                auto ax = f("ax"), ay = f("ay"), bx = f("bx"), by = f("by"), band = f("band");
                for (const auto* v : {&ax, &ay, &bx, &by, &band}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.axisFrom = {static_cast<float>(ax.value()), static_cast<float>(ay.value())};
                g.axisTo = {static_cast<float>(bx.value()), static_cast<float>(by.value())};
                g.searchBand = static_cast<float>(band.value());
                g.stations = static_cast<int>(reader.numberOr("stations", 240.0));
                return ResultT::ok(g);
            }
            case ToolType::Gear: {
                GearGeometry g;
                auto cx = f("cx"), cy = f("cy"), rin = f("rin"), rout = f("rout");
                for (const auto* v : {&cx, &cy, &rin, &rout}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.center = {static_cast<float>(cx.value()), static_cast<float>(cy.value())};
                g.innerRadius = static_cast<float>(rin.value());
                g.outerRadius = static_cast<float>(rout.value());
                g.rayCount = static_cast<int>(reader.numberOr("rays", 1440.0));
                return ResultT::ok(g);
            }
            case ToolType::ConstructedPoint: {
                ConstructedPointGeometry g;
                auto ax = f("ax"), ay = f("ay");
                for (const auto* v : {&ax, &ay}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.anchor = {static_cast<float>(ax.value()), static_cast<float>(ay.value())};
                // Una construcción desconocida NO se degrada a la primera: eso
                // convertiría un fichero de otra versión en una medida creíble
                // que no es la que el operador configuró.
                const auto mode = readConstruction(reader, allPointConstructions());
                if (!mode.isOk()) return ResultT::err(mode.error().message);
                g.mode = mode.value();
                return ResultT::ok(g);
            }
            case ToolType::ConstructedLine: {
                ConstructedLineGeometry g;
                auto ax = f("ax"), ay = f("ay");
                for (const auto* v : {&ax, &ay}) {
                    if (!v->isOk()) return ResultT::err(v->error().message);
                }
                g.anchor = {static_cast<float>(ax.value()), static_cast<float>(ay.value())};
                const auto mode = readConstruction(reader, allLineConstructions());
                if (!mode.isOk()) return ResultT::err(mode.error().message);
                g.mode = mode.value();
                return ResultT::ok(g);
            }
        }
        return ResultT::err("Tipo de herramienta no soportado");
    } catch (const cv::Exception& e) {
        return ResultT::err(std::string("JSON de geometría inválido: ") + e.what());
    }
}

}  // namespace pci::inspection
