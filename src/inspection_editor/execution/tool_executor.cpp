#include "inspection_editor/execution/tool_executor.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <set>
#include <cmath>
#include <numeric>
#include <cstdio>
#include <optional>
#include <utility>

#include "inspection_editor/execution/edge_detection.h"
#include "inspection_editor/execution/profiles.h"
#include "vision/fitting.h"
#include "vision/geometry_features.h"
#include "vision/periodicity.h"
#include "vision/plane_scale.h"
#include "vision/position_fixture.h"

namespace pci::inspection {

namespace {

constexpr double kPi = 3.14159265358979323846;

using vision::Fixture;
using vision::toImageCoords;

cv::Point2f toImg(const Fixture& f, const cv::Point2f& p) {
    return toImageCoords(f, p);
}

// Empaqueta la escala y la unidad elegida por el operador. Si imageToMm no está
// vacía, es la homografía imagen->mm de un marcador ArUco (D4): las longitudes
// entre dos puntos se miden por la homografía (perspectiva corregida).
struct Fmt {
    double mmPerPixel = 0.0;
    LengthUnit unit = LengthUnit::Auto;
    cv::Mat imageToMm;
    // Perpendicularidad de la cámara (0..1) medida por el marcador ArUco.
    // Negativo = no se sabe.
    double scaleQuality = -1.0;
};

std::string fmt2(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

// Longitud con unidades. Sin escala o unidad Px: píxeles. Con escala: mm, cm
// o automático (cm a partir de 10 cm) según la elección del operador.
std::string fmtLen(double px, const Fmt& f) {
    char buffer[64];
    if (f.mmPerPixel <= 0.0 || f.unit == LengthUnit::Pixels) {
        std::snprintf(buffer, sizeof(buffer), "%.1fpx", px);
        return buffer;
    }
    const double mm = px * f.mmPerPixel;
    const bool useCm = f.unit == LengthUnit::Centimeters ||
                       (f.unit == LengthUnit::Auto && mm >= 100.0);
    if (useCm) {
        std::snprintf(buffer, sizeof(buffer), "%.2fcm (%.1fpx)", mm / 10.0, px);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2fmm (%.1fpx)", mm, px);
    }
    return buffer;
}

// Formatea una medida ya conocida en mm junto a su equivalente en px.
std::string formatMmPx(double mm, double px, const Fmt& f) {
    char buffer[64];
    const bool useCm = f.unit == LengthUnit::Centimeters ||
                       (f.unit == LengthUnit::Auto && mm >= 100.0);
    if (useCm) {
        std::snprintf(buffer, sizeof(buffer), "%.2fcm (%.1fpx)", mm / 10.0, px);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2fmm (%.1fpx)", mm, px);
    }
    return buffer;
}

// Longitud entre dos puntos de imagen. Con homografía ArUco activa, los mm se
// miden mapeando ambos puntos al plano (corrige perspectiva); sin ella, cae a
// la escala constante. El texto en px siempre refleja la distancia en imagen.
std::string fmtLenPts(const cv::Point2f& a, const cv::Point2f& b, const Fmt& f) {
    if (f.imageToMm.empty() || f.unit == LengthUnit::Pixels) {
        return fmtLen(cv::norm(b - a), f);
    }
    return formatMmPx(vision::planeDistanceMm(f.imageToMm, a, b), cv::norm(b - a), f);
}

// Distancia perpendicular de un punto a una línea, con mm por homografía cuando
// hay marcador ArUco (mapea el punto y la línea al plano y mide allí).
std::string fmtPerpDist(const cv::Point2f& p, const cv::Point2f& lineA,
                        const cv::Point2f& lineB, double pxDist, const Fmt& f) {
    if (f.imageToMm.empty() || f.unit == LengthUnit::Pixels) {
        return fmtLen(pxDist, f);
    }
    std::vector<cv::Point2f> in{p, lineA, lineB};
    std::vector<cv::Point2f> out;
    cv::perspectiveTransform(in, out, f.imageToMm);
    const cv::Point2f delta = out[2] - out[1];
    const double len = cv::norm(delta);
    const double mm =
        len > 1e-9 ? std::abs(static_cast<double>(delta.x) * (out[0].y - out[1].y) -
                              static_cast<double>(delta.y) * (out[0].x - out[1].x)) /
                         len
                   : 0.0;
    return formatMmPx(mm, pxDist, f);
}

std::string fmtArea(double px2, const Fmt& f) {
    char buffer[64];
    if (f.mmPerPixel <= 0.0 || f.unit == LengthUnit::Pixels) {
        std::snprintf(buffer, sizeof(buffer), "%.0fpx²", px2);
        return buffer;
    }
    const double mm2 = px2 * f.mmPerPixel * f.mmPerPixel;
    const bool useCm = f.unit == LengthUnit::Centimeters ||
                       (f.unit == LengthUnit::Auto && mm2 >= 10000.0);
    if (useCm) {
        std::snprintf(buffer, sizeof(buffer), "%.2fcm² (%.0fpx²)", mm2 / 100.0, px2);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1fmm² (%.0fpx²)", mm2, px2);
    }
    return buffer;
}

// Avisos comunes a las herramientas que miden diámetros, radios y perfiles.
//
// Estas medidas salen de una silueta 2D, y hay dos formas de equivocarse que no
// se notan en el número: la cámara inclinada -un círculo se ve como elipse y el
// diámetro sale corto- y un borde sin contraste, donde el "borde" que se
// detecta no es el de la pieza. Las dos dan resultados creíbles y falsos, que
// es la peor manera de fallar, así que se dicen.
//
// `meanEdgeStrength` es el gradiente medio de los bordes que se usaron; por
// debajo de ~25 el borde ya es dudoso (`detectEdges` descarta por debajo de 8,
// y una silueta a contraluz da varios cientos).
void appendConditionWarnings(std::string& detail, const Fmt& fmt, double meanEdgeStrength) {
    if (fmt.scaleQuality >= 0.0 && fmt.scaleQuality < 0.75) {
        detail += " ⚠ cámara inclinada respecto al plano (calidad " +
                  fmt2(fmt.scaleQuality) + "): los diámetros salen cortos";
    }
    if (meanEdgeStrength > 0.0 && meanEdgeStrength < 25.0) {
        detail += " ⚠ borde de poco contraste (" + fmt2(meanEdgeStrength) +
                  "): mejora la iluminación, a ser posible a contraluz";
    }
}

// Gradiente medio de un conjunto de muestras de perfil.
template <typename Sample>
double meanStrength(const std::vector<Sample>& samples) {
    double sum = 0.0;
    int n = 0;
    for (const auto& s : samples) {
        if (s.found) {
            sum += s.strength;
            ++n;
        }
    }
    return n > 0 ? sum / n : 0.0;
}

bool withinTolerance(const ToolConfig& config, double value) {
    return value >= config.toleranceMin && value <= config.toleranceMax;
}

// ¿Queda un tramo del borde SIN VER lo bastante largo como para esconder un
// defecto? Lo comparten las tres herramientas que recorren un borde a base de
// escaneos perpendiculares: Borde liso, Rectitud y Rebabas y mellas.
//
// El umbral va en LONGITUD y no en número de escaneos, y esa es la corrección
// que costó descubrir. Contando escaneos, «dos seguidos» significa cosas
// distintas según lo fino que se muestree: con 120 escaneos sobre 280 px son
// 4,7 px de borde ciego —ruido de binarización— y con 20 escaneos son 29 px, o
// sea una mella entera escondida. Con la regla por recuento, subir la
// resolución del muestreo hacía SALTAR el aviso y bajarla lo silenciaba, que es
// exactamente al revés de lo que tiene que pasar.
//
// Lo que importa es cuánto borde te quedaste sin mirar, y eso se mide en
// píxeles: un suelo absoluto para el ruido de un escaneo suelto, y un término
// relativo al tramo, porque en un borde largo un hueco pequeño pesa menos.
[[nodiscard]] bool blindStretchMatters(int longestGap, double step, double spanLength) {
    const double blind = static_cast<double>(longestGap) * step;
    return blind > std::max(3.0, spanLength * 0.02);
}

ToolRunResult baseResult(const ToolConfig& config) {
    ToolRunResult result;
    result.toolId = config.id;
    result.name = config.name;
    result.type = config.type;
    return result;
}

// --- Geometría compartida: referencias y elementos derivados -------------
//
// Nació con las construcciones de `X1` y vive aquí arriba porque ya la usan
// tres familias: las construcciones, la Orientación (`G3`) y la Posición
// verdadera (`G4`). Estaba definida más abajo, junto a sus primeros
// usuarios, y al necesitarla la Posición —que se ejecuta antes en el
// archivo— tocaba subirla o declararla a medias; subirla es lo correcto,
// porque ya no pertenece a una herramienta sino al mecanismo de referencias.
// --- Construcciones geométricas (X1) ----------------------------------------
//
// Trigonometría sobre elementos que otras herramientas ya ajustan: cero
// algoritmo nuevo. Lo único delicado son los casos degenerados, y ahí la regla
// es siempre la misma — se falla con motivo y NUNCA se devuelve un NaN, que es
// un número con toda la pinta de ser una medida.

// Dos rectas cuentan como paralelas cuando el seno del ángulo que forman baja
// de esto. Con direcciones unitarias |cross| ES ese seno, así que 1e-3 son
// 0,057°: por debajo el corte existe en matemáticas pero cae a más de cien mil
// píxeles, y eso no dice nada de la pieza.
constexpr double kParallelSin = 1e-3;
// Dos puntos "distintos" tienen que estarlo de verdad: por debajo de una
// milésima de píxel no hay ninguna dirección que sacar de ellos.
constexpr double kCoincidentPx = 1e-3;
// Medio largo del tramo que se dibuja de una recta construida. Es una longitud
// de DIBUJO: la recta es infinita y esto solo decide cuánto se ve.
constexpr float kDrawHalfLengthPx = 150.0F;

std::optional<cv::Point2f> intersectLines(const DerivedElement& a, const DerivedElement& b) {
    const double cross = static_cast<double>(a.direction.x) * b.direction.y -
                         static_cast<double>(a.direction.y) * b.direction.x;
    if (std::abs(cross) < kParallelSin) {
        return std::nullopt;
    }
    const cv::Point2f w = b.point - a.point;
    const double t = (static_cast<double>(w.x) * b.direction.y -
                      static_cast<double>(w.y) * b.direction.x) /
                     cross;
    return a.point + static_cast<float>(t) * a.direction;
}

cv::Point2f projectOnLine(const cv::Point2f& p, const DerivedElement& line) {
    return line.point + (p - line.point).dot(line.direction) * line.direction;
}

// Una recta no tiene sentido, solo dirección, pero su vector SÍ lo tiene, y
// depende de hacia dónde la trazara el operador. Esto lo lleva siempre al
// semiplano superior para que dos trazos opuestos de la misma recta den el
// mismo vector. Sin esto la bisectriz de dos rectas perpendiculares salía a 45°
// o a 135° según el sentido del trazo: las dos son igual de válidas —con 90°
// entre las rectas no hay ángulo agudo que partir— pero que cambie sola no lo
// es, porque el datum giraría 90° sin que nadie tocara nada.
cv::Point2f canonicalDirection(const cv::Point2f& d) {
    if (d.y < 0.0F || (d.y == 0.0F && d.x < 0.0F)) {
        return -d;
    }
    return d;
}

// Resuelve un operando comprobando que sea de la clase que la construcción
// necesita. El motivo se escribe para el operador: qué falta y de qué clase
// tendría que ser, no un "referencia inválida" que no dice dónde mirar.
const DerivedElement* operand(const DerivedElements& refs, const std::string& name,
                              OperandKind kind, const char* which, std::string& why) {
    if (kind == OperandKind::Unused) {
        return nullptr;
    }
    if (name.empty()) {
        why = std::string("falta la ") + which + " referencia: hace falta " +
              operandKindLabel(kind);
        return nullptr;
    }
    const auto found = refs.find(name);
    if (found == refs.end()) {
        why = "no se pudo usar la referencia '" + name +
              "': no existe, está desactivada o falló al medir";
        return nullptr;
    }
    const DerivedElement& element = found->second;
    const bool fits = (kind == OperandKind::Point)  ? element.hasPoint()
                      : (kind == OperandKind::Line) ? element.hasLine()
                                                    : element.kind == DerivedKind::Circle;
    if (!fits) {
        why = "'" + name + "' no sirve como " + which + " referencia: hace falta " +
              operandKindLabel(kind);
        return nullptr;
    }
    // Una recta con dirección degenerada haría aparecer NaN más abajo. Se corta
    // aquí, en el único sitio por el que pasan todos los operandos, en vez de
    // repetir la comprobación en cada construcción.
    if (kind == OperandKind::Line && cv::norm(element.direction) < 0.5) {
        why = "la recta de '" + name + "' no tiene dirección utilizable";
        return nullptr;
    }
    return &element;
}

// Comprueba los dos operandos de una construcción. Devuelve false y deja el
// motivo en el resultado cuando alguno no sirve.
bool resolveOperands(const DerivedElements& refs, const ToolConfig& config,
                     const std::array<OperandKind, 2>& kinds, const DerivedElement*& a,
                     const DerivedElement*& b, ToolRunResult& result) {
    std::string why;
    a = operand(refs, config.reference, kinds[0], "primera", why);
    if (kinds[0] != OperandKind::Unused && a == nullptr) {
        result.detail = why;
        return false;
    }
    b = operand(refs, config.reference2, kinds[1], "segunda", why);
    if (kinds[1] != OperandKind::Unused && b == nullptr) {
        result.detail = why;
        return false;
    }
    return true;
}


ToolRunResult runCaliper(const cv::Mat& gray, const Fixture& fixture,
                         const ToolConfig& config, const CaliperGeometry& g,
                         const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f p0 = toImg(fixture, g.p0);
    const cv::Point2f p1 = toImg(fixture, g.p1);
    result.overlaySegments.push_back({p0, p1});

    const auto edges = detectEdges(gray, p0, p1, g.bandWidth, 6);
    if (edges.size() < 2) {
        result.detail = "Se necesitan 2 bordes y se detectaron " +
                        std::to_string(edges.size());
        return result;
    }

    // Preferir un par de polaridad opuesta (subida + bajada): mide el ancho
    // real de la pieza en vez de dos bordes del mismo lado.
    std::size_t first = 0;
    std::size_t second = 1;
    double bestScore = -1.0;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        for (std::size_t j = i + 1; j < edges.size(); ++j) {
            if (edges[i].strength * edges[j].strength >= 0.0) {
                continue;  // misma polaridad
            }
            const double score =
                std::min(std::abs(edges[i].strength), std::abs(edges[j].strength));
            if (score > bestScore) {
                bestScore = score;
                first = i;
                second = j;
            }
        }
    }
    // Sin par opuesto (p. ej. escalón simple): caer a los dos más fuertes.

    result.measured = std::abs(edges[first].position - edges[second].position);
    result.ok = withinTolerance(config, result.measured);
    // mm por homografía entre los dos bordes medidos (si hay marcador ArUco).
    result.detail = "d=" + fmtLenPts(edges[first].point, edges[second].point, fmt);
    result.overlayPoints.push_back(edges[first].point);
    result.overlayPoints.push_back(edges[second].point);
    return result;
}

ToolRunResult runRuler(const Fixture& fixture, const ToolConfig& config,
                       const RulerGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f p0 = toImg(fixture, g.p0);
    const cv::Point2f p1 = toImg(fixture, g.p1);
    result.overlaySegments.push_back({p0, p1});
    result.overlayPoints.push_back(p0);
    result.overlayPoints.push_back(p1);

    result.measured = cv::norm(p1 - p0);
    result.ok = withinTolerance(config, result.measured);
    result.detail = "L=" + fmtLenPts(p0, p1, fmt);
    // Ofrece su recta como referencia (X0): dos puntos trazados a mano son la
    // forma más directa de declarar un datum.
    const double length = cv::norm(g.p1 - g.p0);
    if (length > 1e-6) {
        result.derived.kind = DerivedKind::Line;
        result.derived.point = g.p0;
        result.derived.direction = (g.p1 - g.p0) / static_cast<float>(length);
    }
    return result;
}

// Posición: dónde cae un rasgo respecto al cero del tablero de referencia.
// Es lo que convierte el tablero en criterio OK/NG y no solo en ayuda visual.
// Posición verdadera con marco de referencia (G4).
//
// El marco lo forman dos datums: el PRIMARIO orienta —su dirección es el eje X
// del marco— y el SECUNDARIO fija el origen. Con dos rectas, el origen es su
// intersección; con una recta y un punto, el origen es el punto proyectado
// sobre la primaria, que es como se materializa un marco cuando el secundario
// es un agujero.
//
// Todo se mide DENTRO del marco, y por eso girar la pieza entera no cambia el
// resultado: el marco gira con ella.
struct DatumFrame {
    cv::Point2f origin{0.0F, 0.0F};
    cv::Point2f axisX{1.0F, 0.0F};
    bool valid = false;
    std::string problem;
};

DatumFrame buildDatumFrame(const DerivedElements& refs, const ToolConfig& config) {
    DatumFrame frame;
    std::string why;
    const DerivedElement* primary =
        operand(refs, config.reference, OperandKind::Line, "primera", why);
    if (primary == nullptr) {
        frame.problem = config.reference.empty()
                            ? "falta el DATUM PRIMARIO: una recta que oriente el marco"
                            : "el datum primario no vale — " + why;
        return frame;
    }
    if (config.reference2.empty()) {
        frame.problem = "falta el DATUM SECUNDARIO: sin él el marco no tiene origen";
        return frame;
    }
    const auto found = refs.find(config.reference2);
    if (found == refs.end()) {
        frame.problem = "no se pudo usar '" + config.reference2 +
                        "' como datum secundario: no existe, está desactivada o falló";
        return frame;
    }

    frame.axisX = primary->direction;
    const DerivedElement& secondary = found->second;
    if (secondary.hasLine()) {
        const auto crossing = intersectLines(*primary, secondary);
        if (!crossing.has_value()) {
            frame.problem =
                "los dos datums son paralelos: no se cortan, así que no fijan un origen";
            return frame;
        }
        frame.origin = *crossing;
    } else if (secondary.hasPoint()) {
        // El origen es el punto llevado sobre la recta primaria: así el marco
        // queda anclado al datum primario, que es quien manda.
        frame.origin = projectOnLine(secondary.point, *primary);
    } else {
        frame.problem = "'" + config.reference2 +
                        "' no sirve como datum secundario: hace falta una recta o un punto";
        return frame;
    }
    frame.valid = true;
    return frame;
}

ToolRunResult runPosition(const Fixture& fixture, const ToolConfig& config,
                          const PositionGeometry& g, const Fmt& fmt,
                          const vision::BoardFrame& board, const DerivedElements& refs) {
    // Con datums declarados, la posición verdadera de la norma. Sin ellos, el
    // comportamiento de siempre contra el cero del tablero: ampliar y no
    // duplicar significa que quien no toque nada no nota nada.
    if (!config.reference.empty() || !config.reference2.empty()) {
        ToolRunResult framed = baseResult(config);
        const DatumFrame frame = buildDatumFrame(refs, config);
        if (!frame.valid) {
            framed.detail = frame.problem;
            return framed;
        }
        // El rasgo, en coordenadas del marco.
        const cv::Point2f axisY(-frame.axisX.y, frame.axisX.x);
        const cv::Point2f offset = g.point - frame.origin;
        const double x = offset.dot(frame.axisX);
        const double y = offset.dot(axisY);
        const double dx = x - static_cast<double>(g.nominal.x);
        const double dy = y - static_cast<double>(g.nominal.y);
        // El valor de la norma es un DIÁMETRO de zona, no un radio: la zona es
        // un círculo alrededor del punto teórico y la cota da su diámetro.
        framed.measured = 2.0 * std::hypot(dx, dy);
        framed.ok = withinTolerance(config, framed.measured);
        framed.derived.kind = DerivedKind::Point;
        framed.derived.point = g.point;
        framed.detail = "posición verdadera Ø" + fmtLen(framed.measured, fmt) +
                        " (dx=" + fmtLen(dx, fmt) + ", dy=" + fmtLen(dy, fmt) +
                        " en el marco " + config.reference + "|" + config.reference2 + ")";

        const cv::Point2f nominalPiece =
            frame.origin + frame.axisX * g.nominal.x + axisY * g.nominal.y;
        framed.overlayPoints.push_back(toImg(fixture, nominalPiece));
        framed.overlayPoints.push_back(toImg(fixture, g.point));
        framed.overlaySegments.push_back(
            {toImg(fixture, nominalPiece), toImg(fixture, g.point)});
        return framed;
    }

    ToolRunResult result = baseResult(config);
    const cv::Point2f p = toImg(fixture, g.point);
    result.overlayPoints.push_back(board.origin);
    result.overlayPoints.push_back(p);
    result.overlaySegments.push_back({board.origin, p});

    const vision::BoardReading reading = vision::readPoint(board, p);
    switch (g.axis) {
        case PositionAxis::Radial:
            result.measured = reading.radius;
            break;
        case PositionAxis::X:
            result.measured = std::abs(reading.dx);
            break;
        case PositionAxis::Y:
            result.measured = std::abs(reading.dy);
            break;
    }
    result.ok = withinTolerance(config, result.measured);
    // El rasgo marcado, como referencia puntual (X0).
    result.derived.kind = DerivedKind::Point;
    result.derived.point = g.point;
    result.detail = "dx=" + fmtLen(reading.dx, fmt) + " dy=" + fmtLen(reading.dy, fmt) +
                    " r=" + fmtLen(reading.radius, fmt) + " " + fmt2(reading.angleDeg) + "deg";
    return result;
}

ToolRunResult runLineToLine(const Fixture& fixture, const ToolConfig& config,
                            const LineToLineGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    result.measuredIsAngle = true;
    const cv::Point2f a0 = toImg(fixture, g.a0);
    const cv::Point2f a1 = toImg(fixture, g.a1);
    const cv::Point2f b0 = toImg(fixture, g.b0);
    const cv::Point2f b1 = toImg(fixture, g.b1);
    result.overlaySegments.push_back({a0, a1});
    result.overlaySegments.push_back({b0, b1});

    const cv::Point2f dirA = a1 - a0;
    const cv::Point2f dirB = b1 - b0;
    if (cv::norm(dirA) < 1.0 || cv::norm(dirB) < 1.0) {
        result.detail = "Líneas demasiado cortas";
        return result;
    }

    // Ángulo entre líneas (no dirigido): 0°..90°. atan2 del producto cruz y
    // el punto, tomando el valor absoluto y plegando a [0, 90].
    const double cross = static_cast<double>(dirA.x) * dirB.y -
                         static_cast<double>(dirA.y) * dirB.x;
    const double dot = static_cast<double>(dirA.x) * dirB.x +
                       static_cast<double>(dirA.y) * dirB.y;
    double angleDeg = std::abs(std::atan2(cross, dot) * 180.0 / kPi);
    if (angleDeg > 90.0) {
        angleDeg = 180.0 - angleDeg;
    }

    // Separación: distancia perpendicular del punto medio de B a la línea A.
    const cv::Point2f midB = (b0 + b1) * 0.5F;
    const double lenA = cv::norm(dirA);
    const double sep = std::abs(static_cast<double>(dirA.x) * (midB.y - a0.y) -
                                static_cast<double>(dirA.y) * (midB.x - a0.x)) /
                       lenA;

    result.measured = angleDeg;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "ángulo=" + fmt2(angleDeg) + "°, separación=" + fmtLen(sep, fmt);
    return result;
}

ToolRunResult runAngle(const Fixture& fixture, const ToolConfig& config,
                       const AngleGeometry& g, const Fmt& /*fmt*/) {
    ToolRunResult result = baseResult(config);
    result.measuredIsAngle = true;
    const cv::Point2f vertex = toImg(fixture, g.vertex);
    const cv::Point2f end0 = toImg(fixture, g.end0);
    const cv::Point2f end1 = toImg(fixture, g.end1);
    result.overlaySegments.push_back({vertex, end0});
    result.overlaySegments.push_back({vertex, end1});
    result.overlayPoints.push_back(vertex);

    const cv::Point2f r0 = end0 - vertex;
    const cv::Point2f r1 = end1 - vertex;
    if (cv::norm(r0) < 1.0 || cv::norm(r1) < 1.0) {
        result.detail = "Lados demasiado cortos";
        return result;
    }

    // Ángulo interior de la esquina (0°..180°) entre los dos lados.
    const double cross = static_cast<double>(r0.x) * r1.y - static_cast<double>(r0.y) * r1.x;
    const double dot = static_cast<double>(r0.x) * r1.x + static_cast<double>(r0.y) * r1.y;
    const double angleDeg = std::abs(std::atan2(cross, dot) * 180.0 / kPi);

    result.measured = angleDeg;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "ángulo=" + fmt2(angleDeg) + "°";
    return result;
}

ToolRunResult runCircle(const cv::Mat& gray, const Fixture& fixture,
                        const ToolConfig& config, const CircleGeometry& g,
                        const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f center = toImg(fixture, g.center);

    const int rays = std::clamp(g.rayCount, 8, 360);
    std::vector<cv::Point2f> points;
    double circleEdgeStrength = 0.0;
    int circleEdgeCount = 0;
    for (int k = 0; k < rays; ++k) {
        const double theta = 2.0 * kPi * k / rays;
        const cv::Point2f dir(static_cast<float>(std::cos(theta)),
                              static_cast<float>(std::sin(theta)));
        const cv::Point2f from = center + dir * (g.radius - g.searchBand);
        const cv::Point2f to = center + dir * (g.radius + g.searchBand);
        const auto edges = detectEdges(gray, from, to, 3.0F, 1);
        if (!edges.empty()) {
            points.push_back(edges[0].point);
            circleEdgeStrength += std::abs(edges[0].strength);
            ++circleEdgeCount;
        }
    }
    if (circleEdgeCount > 0) {
        circleEdgeStrength /= circleEdgeCount;
    }

    if (static_cast<int>(points.size()) < rays * 6 / 10) {
        result.detail = "Borde circular insuficiente (" + std::to_string(points.size()) +
                        "/" + std::to_string(rays) + " rayos)";
        return result;
    }

    // Ajuste robusto (Taubin + reponderación). Antes se usaba Kasa, que sesga
    // el radio hacia abajo cuando el borde solo aparece en parte del contorno
    // —un círculo tapado a medias, un taladro con el borde roto— y sin ningún
    // rechazo de atípicos, así que una rebaba o un reflejo movían el resultado.
    const vision::CircleFit fit = vision::fitCircleRobust(points);
    if (!fit.valid) {
        result.detail = "No se pudo ajustar el círculo";
        return result;
    }
    const double cx = fit.center.x;
    const double cy = fit.center.y;
    const double r = fit.radius;

    // La redondez se mide sobre los puntos que el ajuste consideró buenos: si
    // se incluyeran los descartados, un solo punto malo la dispararía y
    // ocultaría la forma real de la pieza.
    const double outlierBand = 3.0 * std::max(fit.rmsResidual, 0.1);
    double roundness = 0.0;
    int discarded = 0;
    for (const auto& p : points) {
        const double deviation = std::abs(std::hypot(p.x - cx, p.y - cy) - r);
        if (deviation > outlierBand) {
            ++discarded;
            continue;
        }
        roundness = std::max(roundness, deviation);
    }

    result.measured = 2.0 * r;
    result.ok = withinTolerance(config, result.measured);
    // El círculo AJUSTADO como referencia, no el trazado: el centro que vale
    // para un datum es el que sale del borde real, no el que puso el operador.
    result.derived.kind = DerivedKind::Circle;
    result.derived.point = vision::toPieceCoords(fixture, cv::Point2f(
        static_cast<float>(cx), static_cast<float>(cy)));
    result.derived.radius = r;
    result.detail = "D=" + fmtLen(result.measured, fmt) +
                    ", R=" + fmtLen(r, fmt) +
                    // NO se llama "redondez": es la desviación radial máxima
                    // respecto al círculo de mínimos cuadrados, que es media
                    // banda y otro número distinto del de la norma. Llamarlo
                    // redondez invitaba a apuntarlo en un informe como si fuera
                    // la cota del plano. Para esa está la herramienta Redondez.
                    ", desv. radial máx.=" + fmtLen(roundness, fmt);
    if (discarded > 0) {
        // Decirlo importa: un borde con muchos puntos descartados puede seguir
        // dando un diámetro perfecto y estar midiendo solo media pieza.
        result.detail += " (" + std::to_string(discarded) + "/" +
                         std::to_string(points.size()) + " puntos descartados)";
    }
    appendConditionWarnings(result.detail, fmt, circleEdgeStrength);
    result.overlayPoints = std::move(points);
    result.overlayPoints.push_back(
        {static_cast<float>(cx), static_cast<float>(cy)});
    return result;
}

ToolRunResult runArc(const cv::Mat& gray, const Fixture& fixture, const ToolConfig& config,
                     const ArcGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f start = toImg(fixture, g.start);
    const cv::Point2f mid = toImg(fixture, g.mid);
    const cv::Point2f end = toImg(fixture, g.end);

    const vision::ArcSpan guess = vision::circleThroughThreePoints(start, mid, end);
    if (!guess.valid) {
        result.detail = "Los tres puntos están alineados: no definen un arco";
        return result;
    }

    // Los tres puntos solo sitúan el arco; el radio se mide sobre el BORDE
    // real, barriendo el sector y ajustando. Quedarse con la circunferencia de
    // los tres puntos sería medir dónde hizo clic el operador, no la pieza.
    const int rays = std::clamp(g.rayCount, 5, 180);
    const double band = std::max(2.0, static_cast<double>(g.searchBand));
    const auto profile =
        radialProfileSector(gray, guess.center, std::max(0.0, guess.radius - band),
                            guess.radius + band, rays, guess.startAngleDeg, guess.sweepDeg);

    std::vector<cv::Point2f> points;
    points.reserve(profile.size());
    for (const auto& sample : profile) {
        if (sample.found) {
            points.push_back(sample.point);
        }
    }
    if (static_cast<int>(points.size()) < std::max(3, rays * 6 / 10)) {
        result.detail = "Borde del arco insuficiente (" + std::to_string(points.size()) + "/" +
                        std::to_string(rays) + " rayos)";
        return result;
    }

    const vision::CircleFit fit = vision::fitCircleRobust(points);
    if (!fit.valid) {
        result.detail = "No se pudo ajustar el arco";
        return result;
    }

    // Error de forma sobre los puntos que contaron, igual que la redondez del
    // Círculo.
    const double outlierBand = 3.0 * std::max(fit.rmsResidual, 0.1);
    double formError = 0.0;
    int discarded = 0;
    for (const auto& p : points) {
        const double deviation = std::abs(cv::norm(p - fit.center) - fit.radius);
        if (deviation > outlierBand) {
            ++discarded;
            continue;
        }
        formError = std::max(formError, deviation);
    }

    // El radio es la medida principal: es lo que lleva el plano y lo que se
    // compara con una plantilla de radios.
    result.measured = fit.radius;
    result.ok = withinTolerance(config, result.measured);
    const double span = std::abs(guess.sweepDeg);
    result.detail = "R=" + fmtLen(fit.radius, fmt) + ", D=" + fmtLen(2.0 * fit.radius, fmt) +
                    ", forma=" + fmtLen(formError, fmt) + ", arco=" + fmt2(span) + "°";
    // Sobre un arco corto, el radio y el centro son casi indistinguibles: un
    // error pequeño y sistemático del borde se traduce en un error grande de
    // radio. Medido sobre un cuadrante perfecto de radio 20, el resultado se
    // desvía más de un píxel solo por cómo está dibujado el borde. Conviene
    // decirlo en vez de dar el número a secas.
    if (span < 30.0) {
        result.detail += " — arco corto: el radio es poco fiable, alarga el tramo";
    }
    if (discarded > 0) {
        result.detail += " (" + std::to_string(discarded) + "/" +
                         std::to_string(points.size()) + " puntos descartados)";
    }
    appendConditionWarnings(result.detail, fmt, meanStrength(profile));
    result.overlayPoints = std::move(points);
    return result;
}

ToolRunResult runShaft(const cv::Mat& gray, const Fixture& fixture, const ToolConfig& config,
                       const ShaftGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f from = toImg(fixture, g.axisFrom);
    const cv::Point2f to = toImg(fixture, g.axisTo);
    if (cv::norm(to - from) < 5.0) {
        result.detail = "El eje trazado es demasiado corto";
        return result;
    }

    const int stations = std::clamp(g.stations, 5, 200);
    const double reach = std::max(5.0, static_cast<double>(g.searchBand));
    const auto sideA = axialProfile(gray, from, to, ProfileSide::Positive, stations, reach);
    const auto sideB = axialProfile(gray, from, to, ProfileSide::Negative, stations, reach);

    std::vector<cv::Point2f> pointsA;
    std::vector<cv::Point2f> pointsB;
    for (const auto& s : sideA) {
        if (s.found) {
            pointsA.push_back(s.point);
        }
    }
    for (const auto& s : sideB) {
        if (s.found) {
            pointsB.push_back(s.point);
        }
    }
    const int needed = std::max(3, stations / 2);
    if (static_cast<int>(pointsA.size()) < needed ||
        static_cast<int>(pointsB.size()) < needed) {
        result.detail = "Bordes del eje insuficientes (" + std::to_string(pointsA.size()) +
                        " y " + std::to_string(pointsB.size()) + " de " +
                        std::to_string(stations) + " cortes)";
        // La causa más habitual con diferencia: la banda no llega al borde
        // porque la pieza es gruesa o el eje quedó descentrado. Sin decirlo, el
        // operador no tiene forma de adivinar qué parámetro tocar.
        if (pointsA.empty() || pointsB.empty()) {
            result.detail += ". Un lado no aparece: sube el alcance de búsqueda (ahora " +
                             fmt2(reach) + " px) o centra mejor el eje";
        }
        return result;
    }

    const vision::LineFit lineA = vision::fitLineRobust(pointsA);
    const vision::LineFit lineB = vision::fitLineRobust(pointsB);
    if (!lineA.valid || !lineB.valid) {
        result.detail = "No se pudo ajustar alguno de los dos bordes";
        return result;
    }

    // El diámetro en un punto del eje es la suma de las distancias
    // perpendiculares a los dos bordes ajustados. Sale bien aunque el eje se
    // haya trazado descentrado: lo que se mide es la separación entre los
    // bordes, no la distancia a la línea que dibujó el operador.
    const cv::Point2f axis = to - from;
    std::vector<double> diameters;
    diameters.reserve(static_cast<std::size_t>(stations));
    for (int i = 0; i < stations; ++i) {
        const cv::Point2f p = from + axis * (static_cast<float>(i) / (stations - 1));
        diameters.push_back(std::abs(lineA.signedDistance(p)) +
                            std::abs(lineB.signedDistance(p)));
    }
    const double meanDiameter =
        std::accumulate(diameters.begin(), diameters.end(), 0.0) / diameters.size();
    // Conicidad: cuánto cambia el diámetro de un extremo al otro. Es la medida
    // que un calíper en un punto no puede dar.
    const double taper = diameters.back() - diameters.front();
    // Rectitud: lo que se aparta cada borde de su propia recta.
    const double straightness = std::max(lineA.rmsResidual, lineB.rmsResidual);

    result.measured = meanDiameter;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "Ø=" + fmtLen(meanDiameter, fmt) +
                    ", conicidad=" + fmtLen(std::abs(taper), fmt) +
                    (std::abs(taper) < 1e-9 ? ""
                                            : (taper > 0.0 ? " (abre)" : " (cierra)")) +
                    ", rectitud=" + fmtLen(straightness, fmt);
    // Los dos bordes deberían ser paralelos; si no lo son, el ángulo entre
    // ellos dice cuánto cónica es la pieza en grados, que es como suele venir
    // en el plano.
    const double angleBetween = std::abs(lineA.angleDeg() - lineB.angleDeg());
    if (angleBetween > 0.3) {
        result.detail += ", ángulo entre caras=" + fmt2(angleBetween) + "°";
    }
    appendConditionWarnings(result.detail, fmt,
                            (meanStrength(sideA) + meanStrength(sideB)) / 2.0);

    result.overlaySegments.push_back({pointsA.front(), pointsA.back()});
    result.overlaySegments.push_back({pointsB.front(), pointsB.back()});
    result.overlayPoints.push_back(from + axis * 0.5F);
    return result;
}

// --- Ranura o garganta (M4) -------------------------------------------------

// La cota de un anillo de retención: ancho, profundidad y diámetro de FONDO de
// una entalla en una pieza de torno.
//
// Reutiliza el mismo perfil radio-contra-posición-axial que el Eje torneado, con
// una diferencia que es todo el asunto: el Eje AJUSTA una recta a cada borde
// para describir el conjunto, y aquí eso destruiría la medida — la ranura es
// justo el sitio donde el borde se sale de esa recta. Así que se usa el perfil
// crudo, estación a estación, y se busca su mínimo local.
//
// La consecuencia incómoda, y por eso está escrita en la herramienta: el ancho
// se mide CONTANDO cortes, así que el paso axial es su resolución. Una ranura
// que solo abarca uno o dos cortes no tiene los flancos resueltos, y devolver
// ahí un número redondeado al paso sería inventarlo.
ToolRunResult runGroove(const cv::Mat& gray, const Fixture& fixture, const ToolConfig& config,
                        const GrooveGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f from = toImg(fixture, g.axisFrom);
    const cv::Point2f to = toImg(fixture, g.axisTo);
    const double axisLength = cv::norm(to - from);
    if (axisLength < 20.0) {
        result.detail = "El eje trazado es demasiado corto para buscar una ranura";
        return result;
    }

    const int stations = std::clamp(g.stations, 12, 400);
    const double step = axisLength / (stations - 1);
    const double reach = std::max(5.0, static_cast<double>(g.searchBand));
    const auto sideA = axialProfile(gray, from, to, ProfileSide::Positive, stations, reach);
    const auto sideB = axialProfile(gray, from, to, ProfileSide::Negative, stations, reach);
    if (static_cast<int>(sideA.size()) != stations ||
        static_cast<int>(sideB.size()) != stations) {
        result.detail = "No se pudo recorrer el eje trazado";
        return result;
    }

    // Diámetro crudo en cada corte: la suma de los dos alcances al borde.
    std::vector<double> diameter(static_cast<std::size_t>(stations), 0.0);
    std::vector<bool> known(static_cast<std::size_t>(stations), false);
    int found = 0;
    for (int i = 0; i < stations; ++i) {
        const auto k = static_cast<std::size_t>(i);
        if (sideA[k].found && sideB[k].found) {
            diameter[k] = sideA[k].offset + sideB[k].offset;
            known[k] = true;
            ++found;
        }
    }
    if (found < stations * 3 / 4) {
        result.detail = "Bordes insuficientes (" + std::to_string(found) + " de " +
                        std::to_string(stations) +
                        " cortes ven los dos lados). Sube el alcance de búsqueda (ahora " +
                        fmt2(reach) + " px) o centra mejor el eje";
        return result;
    }
    // Huecos cortos se rellenan interpolando; uno largo se dice, porque tapar un
    // tramo ciego de tres cortes con una recta puede borrar la ranura entera.
    for (int i = 0; i < stations; ++i) {
        if (known[static_cast<std::size_t>(i)]) {
            continue;
        }
        int left = i - 1;
        while (left >= 0 && !known[static_cast<std::size_t>(left)]) {
            --left;
        }
        int right = i + 1;
        while (right < stations && !known[static_cast<std::size_t>(right)]) {
            ++right;
        }
        if (left < 0 || right >= stations || right - left > 3) {
            result.detail = "Hay un tramo del eje sin borde a la altura del corte " +
                            std::to_string(i) + ": no se puede seguir el perfil";
            return result;
        }
        const double f = static_cast<double>(i - left) / (right - left);
        diameter[static_cast<std::size_t>(i)] =
            diameter[static_cast<std::size_t>(left)] * (1.0 - f) +
            diameter[static_cast<std::size_t>(right)] * f;
    }

    const auto medianOf = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };

    // La ranura más estrecha que este muestreo puede medir. Sale de la misma
    // regla que el corte de abajo —hacen falta tres cortes dentro para tener
    // los dos flancos resueltos— y se dice en los dos sitios, porque las dos
    // veces la respuesta del operador es la misma: subir el número de cortes.
    const double finestGroove = 3.0 * step;

    const double minimum = *std::min_element(diameter.begin(), diameter.end());
    double outer = medianOf(diameter);  // la ranura es minoría del eje trazado
    if (outer - minimum < std::max(3.0, outer * 0.03)) {
        // Ojo con lo que se afirma aquí. Si la ranura es más fina que el paso,
        // puede que NINGÚN corte caiga dentro y el perfil salga plano: desde el
        // perfil, "no hay ranura" y "la ranura se me ha colado entre dos cortes"
        // son indistinguibles. Decir solo lo primero haría que el operador diese
        // por buena una pieza sin mirarla.
        result.detail = "No se ve ninguna ranura en el eje trazado: el diámetro apenas "
                        "varía (Ø=" + fmtLen(outer, fmt) + ", variación=" +
                        fmtLen(outer - minimum, fmt) + "). Con este muestreo (paso " +
                        fmt2(step) + " px) una ranura de menos de " +
                        fmtLen(finestGroove, fmt) +
                        " se colaría entre dos cortes sin verse: si esperabas una más "
                        "fina, sube el número de cortes (ahora " +
                        std::to_string(stations) + ")";
        return result;
    }

    // El tramo de la ranura es la racha CONTIGUA que contiene el mínimo y baja
    // del nivel de media profundidad. Contigua a propósito: si el eje trazado
    // pilla dos ranuras, se mide una, no la envolvente de las dos.
    const int lowest = static_cast<int>(
        std::min_element(diameter.begin(), diameter.end()) - diameter.begin());
    double root = minimum;
    int first = lowest;
    int last = lowest;
    for (int pass = 0; pass < 2; ++pass) {
        const double level = (outer + root) / 2.0;
        first = lowest;
        while (first > 0 && diameter[static_cast<std::size_t>(first - 1)] < level) {
            --first;
        }
        last = lowest;
        while (last + 1 < stations && diameter[static_cast<std::size_t>(last + 1)] < level) {
            ++last;
        }
        // Fondo: la mediana de los cortes que están en el quinto más hondo, no
        // el mínimo suelto — un solo corte con ruido no debe fijar la cota.
        std::vector<double> floorSamples;
        std::vector<double> outside;
        const double floorLevel = minimum + (outer - minimum) * 0.2;
        for (int i = 0; i < stations; ++i) {
            const double d = diameter[static_cast<std::size_t>(i)];
            if (i >= first && i <= last) {
                if (d <= floorLevel) {
                    floorSamples.push_back(d);
                }
            } else {
                outside.push_back(d);
            }
        }
        if (!floorSamples.empty()) {
            root = medianOf(floorSamples);
        }
        if (outside.size() >= 4) {
            outer = medianOf(outside);
        }
    }

    if (first == 0 || last == stations - 1) {
        result.detail = "La ranura llega al extremo del eje trazado: no se ven sus dos "
                        "flancos. Alarga el trazo por fuera de la ranura";
        return result;
    }

    const int inside = last - first + 1;
    if (inside < 3) {
        // Aquí está la línea que separa medir de inventar. Con uno o dos cortes
        // dentro, los flancos no están resueltos y cualquier ancho que se diera
        // sería el paso de muestreo disfrazado de medida.
        result.detail = "La ranura solo abarca " + std::to_string(inside) +
                        (inside == 1 ? " corte" : " cortes") + " de " + fmt2(step) +
                        " px: es más estrecha que el muestreo y su ancho NO SE PUEDE "
                        "medir con esta configuración (harían falta al menos " +
                        fmtLen(finestGroove, fmt) +
                        " de ranura). Sube el número de cortes (ahora " +
                        std::to_string(stations) + ") o acorta el trazo del eje";
        return result;
    }

    // Flancos por interpolación del cruce con el nivel de media profundidad.
    // Afina dentro del paso, pero no crea resolución: por eso el corte de arriba
    // se hace CONTANDO cortes y no con este número.
    const double level = (outer + root) / 2.0;
    const auto crossing = [&](int above, int below) {
        const double da = diameter[static_cast<std::size_t>(above)];
        const double db = diameter[static_cast<std::size_t>(below)];
        const double f = std::abs(da - db) < 1e-9 ? 0.5 : (da - level) / (da - db);
        return (above + std::clamp(f, 0.0, 1.0) * (below - above)) * step;
    };
    const double tLeft = crossing(first - 1, first);
    const double tRight = crossing(last + 1, last);
    const double width = tRight - tLeft;
    const double depth = (outer - root) / 2.0;

    switch (g.measure) {
        case GrooveMeasure::Width: result.measured = width; break;
        case GrooveMeasure::Depth: result.measured = depth; break;
        case GrooveMeasure::RootDiameter: result.measured = root; break;
    }
    result.ok = withinTolerance(config, result.measured);
    result.detail = std::string(grooveMeasureLabel(g.measure)) + " · ancho=" +
                    fmtLen(width, fmt) + " · profundidad=" + fmtLen(depth, fmt) +
                    " · Ø fondo=" + fmtLen(root, fmt) + " · Ø fuera=" + fmtLen(outer, fmt) +
                    " (" + std::to_string(inside) + " cortes dentro, paso " + fmt2(step) +
                    " px)";
    appendConditionWarnings(result.detail, fmt,
                            (meanStrength(sideA) + meanStrength(sideB)) / 2.0);

    const cv::Point2f dir = (to - from) / static_cast<float>(axisLength);
    const cv::Point2f normal = profileNormal(from, to);
    const cv::Point2f leftAt = from + dir * static_cast<float>(tLeft);
    const cv::Point2f rightAt = from + dir * static_cast<float>(tRight);
    const auto half = static_cast<float>(root / 2.0);
    // Los dos flancos, y el fondo entre ellos: lo que se ha medido, dibujado.
    result.overlaySegments.push_back({leftAt - normal * half, leftAt + normal * half});
    result.overlaySegments.push_back({rightAt - normal * half, rightAt + normal * half});
    result.overlaySegments.push_back({leftAt + normal * half, rightAt + normal * half});
    result.overlaySegments.push_back({leftAt - normal * half, rightAt - normal * half});
    result.overlayPoints.push_back(leftAt);
    result.overlayPoints.push_back(rightAt);
    return result;
}

// --- Rosca -----------------------------------------------------------------

// Perfil de un lado, convertido en señal uniforme. Los cortes sin borde se
// rellenan interpolando en vez de omitirse: el paso sale del PERIODO de esta
// señal, y quitar muestras desplazaría todas las siguientes.
struct ThreadSide {
    std::vector<double> offsets;
    int missing = 0;
    bool usable = false;
};

ThreadSide buildThreadSide(const std::vector<AxialSample>& profile) {
    ThreadSide side;
    if (profile.empty()) {
        return side;
    }
    side.offsets.assign(profile.size(), 0.0);
    std::vector<bool> known(profile.size(), false);
    for (std::size_t i = 0; i < profile.size(); ++i) {
        known[i] = profile[i].found;
        side.offsets[i] = profile[i].offset;
        if (!profile[i].found) {
            ++side.missing;
        }
    }
    if (side.missing * 3 > static_cast<int>(profile.size())) {
        return side;  // más de un tercio sin borde: no hay señal que analizar
    }
    // Relleno lineal de los huecos, extendiendo el valor de los extremos.
    std::size_t lastKnown = profile.size();
    for (std::size_t i = 0; i < profile.size(); ++i) {
        if (!known[i]) {
            continue;
        }
        if (lastKnown == profile.size()) {
            for (std::size_t j = 0; j < i; ++j) {
                side.offsets[j] = side.offsets[i];
            }
        } else if (i > lastKnown + 1) {
            const double a = side.offsets[lastKnown];
            const double b = side.offsets[i];
            for (std::size_t j = lastKnown + 1; j < i; ++j) {
                const double t = static_cast<double>(j - lastKnown) / (i - lastKnown);
                side.offsets[j] = a + (b - a) * t;
            }
        }
        lastKnown = i;
    }
    if (lastKnown == profile.size()) {
        return side;  // ni un solo borde
    }
    for (std::size_t j = lastKnown + 1; j < profile.size(); ++j) {
        side.offsets[j] = side.offsets[lastKnown];
    }
    side.usable = true;
    return side;
}

// Media del decil superior (crestas) o inferior (valles). Se usa la media de un
// grupo y no el máximo suelto para que una rebaba no defina el diámetro.
double decileMean(std::vector<double> values, bool top) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t count = std::max<std::size_t>(1, values.size() / 10);
    double sum = 0.0;
    for (std::size_t k = 0; k < count; ++k) {
        sum += top ? values[values.size() - 1 - k] : values[k];
    }
    return sum / count;
}

// Perfil medio de UNA vuelta, obtenido plegando la señal por su periodo y
// promediando todas las vueltas alineadas por fase. Es lo mismo que hace un
// perfilómetro: al promediar decenas de vueltas, el ruido de borde baja y queda
// la forma del filete. Sin este paso, cualquier medida del flanco depende de
// dónde caigan las muestras dentro de cada vuelta.
// El número de casillas lo manda el PERIODO, no un valor fijo: con un periodo
// de 46 muestras solo existen ~46 fases distintas, así que pedir 72 casillas
// dejaba huecos vacíos por construcción y el plegado se abortaba entero.
std::vector<double> foldByPeriod(const std::vector<double>& offsets, double period) {
    if (!(period > 8.0) || offsets.size() < 16) {
        return {};
    }
    const int bins = std::clamp(static_cast<int>(std::floor(period)), 12, 72);
    std::vector<double> sum(static_cast<std::size_t>(bins), 0.0);
    std::vector<int> count(static_cast<std::size_t>(bins), 0);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const double phase = std::fmod(static_cast<double>(i), period) / period;
        auto bin = static_cast<std::size_t>(phase * bins);
        if (bin >= static_cast<std::size_t>(bins)) {
            bin = static_cast<std::size_t>(bins) - 1;
        }
        sum[bin] += offsets[i];
        ++count[bin];
    }

    std::vector<double> folded(static_cast<std::size_t>(bins), 0.0);
    int empty = 0;
    for (std::size_t b = 0; b < folded.size(); ++b) {
        if (count[b] > 0) {
            folded[b] = sum[b] / count[b];
        } else {
            ++empty;
        }
    }
    if (empty * 5 > bins) {
        return {};  // demasiados huecos: el periodo no cuadra
    }
    // Los pocos huecos que queden se rellenan con el vecino ocupado más
    // cercano, dando la vuelta: el perfil de una rosca es cíclico.
    for (std::size_t b = 0; b < folded.size(); ++b) {
        if (count[b] > 0) {
            continue;
        }
        for (int step = 1; step < bins; ++step) {
            const auto before = static_cast<std::size_t>((static_cast<int>(b) - step + bins) %
                                                         bins);
            const auto after = static_cast<std::size_t>((static_cast<int>(b) + step) % bins);
            if (count[before] > 0 && count[after] > 0) {
                folded[b] = (folded[before] + folded[after]) / 2.0;
                break;
            }
            if (count[before] > 0) {
                folded[b] = folded[before];
                break;
            }
            if (count[after] > 0) {
                folded[b] = folded[after];
                break;
            }
        }
    }
    return folded;
}

// Ángulo incluido entre los dos flancos, en grados.
//
// El flanco es el tramo INCLINADO entre un valle y una cresta. Se ajusta una
// recta a la parte central de ese tramo —del 20 % al 80 % de la altura del
// filete— para no contaminarla con el redondeo de la punta ni con el del fondo.
// Se hizo primero por estadística de pendientes (mediana de las más inclinadas)
// y estaba mal: la proporción de muestras que caen en el flanco depende de
// cuánto llano tenga la cresta, así que el resultado cambiaba con el PASO en
// vez de con el ángulo.
double flankAngleDeg(const std::vector<double>& offsets, double period,
                     double stationSpacing) {
    const std::vector<double> folded = foldByPeriod(offsets, period);
    if (folded.empty() || stationSpacing <= 0.0) {
        return 0.0;
    }
    const auto kBins = static_cast<int>(folded.size());
    const auto minIt = std::min_element(folded.begin(), folded.end());
    const auto maxIt = std::max_element(folded.begin(), folded.end());
    const double low = *minIt;
    const double high = *maxIt;
    const double height = high - low;
    if (height < 1.0) {
        return 0.0;  // sin filete apreciable no hay flanco que medir
    }
    const auto rootBin = static_cast<int>(std::distance(folded.begin(), minIt));
    const auto crestBin = static_cast<int>(std::distance(folded.begin(), maxIt));

    // Ancho de un bin, en píxeles a lo largo del eje.
    const double binWidthPx = period * stationSpacing / kBins;

    // Recorre de un extremo al otro (circularmente) y ajusta la parte central.
    const auto measureFlank = [&](int fromBin, int toBin) -> double {
        std::vector<cv::Point2f> points;
        int steps = (toBin - fromBin + kBins) % kBins;
        if (steps == 0) {
            steps = kBins;
        }
        for (int k = 0; k <= steps; ++k) {
            const auto bin = static_cast<std::size_t>((fromBin + k) % kBins);
            const double value = folded[bin];
            const double fraction = (value - low) / height;
            if (fraction < 0.2 || fraction > 0.8) {
                continue;  // punta y fondo redondeados: fuera del ajuste
            }
            points.emplace_back(static_cast<float>(k * binWidthPx),
                                static_cast<float>(value));
        }
        if (points.size() < 3) {
            return 0.0;
        }
        const vision::LineFit fit = vision::fitLineTotal(points);
        if (!fit.valid || std::abs(fit.direction.x) < 1e-6) {
            return 0.0;
        }
        const double slope = std::abs(fit.direction.y / fit.direction.x);
        if (slope < 1e-6) {
            return 0.0;
        }
        // El flanco forma con la PERPENDICULAR al eje un ángulo atan(1/pendiente).
        return std::atan(1.0 / slope) * 180.0 / kPi;
    };

    const double rising = measureFlank(rootBin, crestBin);
    const double falling = measureFlank(crestBin, rootBin);
    if (rising <= 0.0 || falling <= 0.0) {
        return 0.0;
    }
    return rising + falling;  // ángulo incluido: los dos flancos
}

// Roscas métricas de paso grueso. Sirve para proponer la designación cuando hay
// calibración: con el diámetro exterior y el paso en mm, la rosca queda
// identificada.
struct MetricThread {
    double diameterMm;
    double pitchMm;
    const char* name;
};

const char* closestMetricThread(double diameterMm, double pitchMm, double& errorOut) {
    static constexpr MetricThread kTable[] = {
        {1.6, 0.35, "M1.6"}, {2.0, 0.40, "M2"},   {2.5, 0.45, "M2.5"}, {3.0, 0.50, "M3"},
        {4.0, 0.70, "M4"},   {5.0, 0.80, "M5"},   {6.0, 1.00, "M6"},   {8.0, 1.25, "M8"},
        {10.0, 1.50, "M10"}, {12.0, 1.75, "M12"}, {14.0, 2.00, "M14"}, {16.0, 2.00, "M16"},
        {20.0, 2.50, "M20"}, {24.0, 3.00, "M24"}, {30.0, 3.50, "M30"}, {36.0, 4.00, "M36"}};
    const char* best = nullptr;
    double bestError = 1e9;
    for (const auto& entry : kTable) {
        // Error relativo en los dos ejes: un tornillo se identifica por la
        // pareja, no por el diámetro suelto.
        const double e = std::abs(diameterMm - entry.diameterMm) / entry.diameterMm +
                         std::abs(pitchMm - entry.pitchMm) / entry.pitchMm;
        if (e < bestError) {
            bestError = e;
            best = entry.name;
        }
    }
    errorOut = bestError;
    return best;
}

ToolRunResult runThread(const cv::Mat& gray, const Fixture& fixture, const ToolConfig& config,
                        const ThreadGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f from = toImg(fixture, g.axisFrom);
    const cv::Point2f to = toImg(fixture, g.axisTo);
    const double axisLength = cv::norm(to - from);
    if (axisLength < 20.0) {
        result.detail = "El eje trazado es demasiado corto para ver varias vueltas";
        return result;
    }

    const int stations = std::clamp(g.stations, 40, 1000);
    const double reach = std::max(5.0, static_cast<double>(g.searchBand));
    // Grosor de promediado 1 y no el 3 habitual. El promediado va PERPENDICULAR
    // al escaneo, y como el escaneo sale perpendicular al eje, ese promedio cae
    // a lo LARGO del eje: justo sobre el filete que se quiere medir. Con 3 px
    // los flancos salían emborronados y el ángulo se leía siempre ~62° fuera
    // cual fuera el real. Aquí conviene el perfil crudo.
    constexpr float kThreadThickness = 1.0F;
    const auto rawA = axialProfile(gray, from, to, ProfileSide::Positive, stations, reach,
                                   kThreadThickness);
    const auto rawB = axialProfile(gray, from, to, ProfileSide::Negative, stations, reach,
                                   kThreadThickness);
    const ThreadSide sideA = buildThreadSide(rawA);
    const ThreadSide sideB = buildThreadSide(rawB);
    if (!sideA.usable || !sideB.usable) {
        result.detail = "No se ve el perfil de la rosca a los dos lados del eje";
        if (sideA.missing > 0 || sideB.missing > 0) {
            result.detail += ". Sube el alcance de búsqueda (ahora " + fmt2(reach) +
                             " px) o mejora el contraste del borde";
        }
        return result;
    }

    const double spacing = axisLength / (stations - 1);

    // El paso sale del periodo del perfil. Se mide en los dos lados por
    // separado: si no coinciden, la lectura no es de fiar y se dice.
    const double minPeriod = 4.0;
    const double maxPeriod = stations / 2.5;
    const auto periodA = vision::dominantPeriod(sideA.offsets, minPeriod, maxPeriod);
    const auto periodB = vision::dominantPeriod(sideB.offsets, minPeriod, maxPeriod);
    if (!periodA.valid || !periodB.valid) {
        result.detail = "El perfil no se repite: ¿es una rosca vista de perfil?";
        return result;
    }
    const double pitchPx = (periodA.period + periodB.period) / 2.0 * spacing;
    const double confidence = std::min(periodA.confidence, periodB.confidence);
    const double sidesDisagreement =
        std::abs(periodA.period - periodB.period) / std::max(periodA.period, periodB.period);

    // Diámetros: crestas y valles de los dos lados sumados, que no depende de
    // que el eje esté centrado (mismo criterio que el Eje torneado).
    const double majorDiameter =
        decileMean(sideA.offsets, true) + decileMean(sideB.offsets, true);
    const double minorDiameter =
        decileMean(sideA.offsets, false) + decileMean(sideB.offsets, false);
    const double flank = (flankAngleDeg(sideA.offsets, periodA.period, spacing) +
                          flankAngleDeg(sideB.offsets, periodB.period, spacing)) /
                         2.0;

    result.measured = pitchPx;  // el paso es lo que identifica una rosca
    result.ok = withinTolerance(config, result.measured);
    result.detail = "paso=" + fmtLen(pitchPx, fmt) + ", Ø ext=" + fmtLen(majorDiameter, fmt) +
                    ", Ø fondo=" + fmtLen(minorDiameter, fmt) +
                    ", flanco=" + fmt2(flank) + "°";

    if (fmt.mmPerPixel > 0.0) {
        double designationError = 0.0;
        const char* name = closestMetricThread(majorDiameter * fmt.mmPerPixel,
                                               pitchPx * fmt.mmPerPixel, designationError);
        // Solo se propone si encaja de verdad. Dar una designación a una rosca
        // que no está en la tabla sería peor que no dar ninguna.
        if (name != nullptr && designationError < 0.12) {
            result.detail += std::string(", ≈ ") + name + "×" + fmt2(pitchPx * fmt.mmPerPixel);
        }
    } else {
        // Sin escala, el paso en píxeles no identifica nada.
        result.detail += " — sin calibración px→mm no se puede designar la rosca";
    }
    // El ángulo de flanco es el más exigente de los cuatro números: se mide
    // sobre la pendiente del filete, y si el filete ocupa pocos píxeles esa
    // pendiente no se resuelve. Comprobado con roscas sintéticas: con 50 px de
    // altura de filete el ángulo sale a ±1°, con 25 px a ±2°, y con 12 px deja
    // de distinguir 60° de 55°. Los diámetros y el paso aguantan mucho mejor,
    // así que se avisa solo del ángulo en vez de rechazar la medida entera.
    const double crestHeight = (majorDiameter - minorDiameter) / 2.0;
    if (crestHeight < 20.0) {
        result.detail += " (filete de solo " + fmt2(crestHeight) +
                         " px: el ángulo de flanco no es fiable, acerca la cámara)";
    }
    // Sesgo del ángulo de flanco por el ángulo de hélice (M5). Una rosca no es
    // un perfil plano repetido: es una hélice. Mirándola de lado, el flanco
    // cercano y el lejano se proyectan con inclinaciones distintas y la
    // silueta del filete sale engrosada. Los comparadores ópticos lo corrigen
    // INCLINANDO el eje óptico el ángulo de hélice; una cámara fija sobre la
    // mesa no puede, así que el sesgo está ahí se hable de él o no — y hasta
    // ahora no se hablaba.
    //
    // Lo que lo hace decible en vez de un "puede haber error" genérico: el
    // ángulo de hélice sale del paso y del diámetro, que ya están medidos. Y es
    // un COCIENTE entre dos longitudes, así que no hace falta calibración
    // px→mm: el aviso vale igual en una rosca sin calibrar.
    const double pitchDiameter = (majorDiameter + minorDiameter) / 2.0;
    if (pitchDiameter > 1.0) {
        const double helixDeg =
            std::atan(pitchPx / (kPi * pitchDiameter)) * 180.0 / kPi;
        // Solo se avisa por encima de lo que la propia herramienta resuelve
        // (±1° en el mejor caso, y peor con el filete pequeño). En una rosca
        // fina el sesgo se pierde bajo el ruido de la medida, y un aviso que
        // salta en toda rosca es un aviso que se aprende a ignorar.
        if (helixDeg > 1.0) {
            result.detail += " (hélice de " + fmt2(helixDeg) +
                             "°: el flanco cercano y el lejano se proyectan con "
                             "inclinaciones que difieren en ese orden, así que el ángulo "
                             "de flanco lleva un sesgo SISTEMÁTICO de unos " +
                             fmt2(helixDeg) +
                             "° que no se va repitiendo la medida — se quitaría "
                             "inclinando la cámara ese mismo ángulo)";
        }
    }
    if (confidence < 0.5) {
        result.detail += " (repetición débil: confianza " + fmt2(confidence) + ")";
    }
    if (sidesDisagreement > 0.15) {
        result.detail += " (los dos lados no concuerdan: revisa el eje)";
    }

    appendConditionWarnings(result.detail, fmt,
                            (meanStrength(rawA) + meanStrength(rawB)) / 2.0);
    result.overlaySegments.push_back({from, to});
    result.overlayPoints.push_back(from + (to - from) * 0.5F);
    return result;
}

// --- Engranaje --------------------------------------------------------------

ToolRunResult runGear(const cv::Mat& gray, const Fixture& fixture, const ToolConfig& config,
                      const GearGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f center = toImg(fixture, g.center);
    if (!(g.outerRadius > g.innerRadius + 1.0F) || g.innerRadius < 1.0F) {
        result.detail = "Los radios de búsqueda no delimitan la corona de dientes";
        return result;
    }

    const int rays = std::clamp(g.rayCount, 180, 3600);
    const auto profile = radialProfile(gray, center, g.innerRadius, g.outerRadius, rays, 1.0F);
    if (foundCount(profile) * 5 < rays * 4) {
        result.detail = "Borde de los dientes insuficiente (" +
                        std::to_string(foundCount(profile)) + "/" + std::to_string(rays) +
                        " rayos). Ajusta los radios o el contraste";
        return result;
    }

    // Señal r(θ) uniforme: los rayos sin borde se rellenan con el vecino, por
    // la misma razón que en la rosca — el número de dientes sale del PERIODO.
    std::vector<double> radii(profile.size(), 0.0);
    double lastGood = 0.0;
    for (std::size_t i = 0; i < profile.size(); ++i) {
        if (profile[i].found) {
            lastGood = profile[i].radius;
        }
        radii[i] = lastGood;
    }
    for (std::size_t i = 0; i < radii.size() && radii[i] == 0.0; ++i) {
        radii[i] = lastGood;  // los huecos del principio, con el último válido
    }

    // Una vuelta CIERRA sobre sí misma: correlación circular.
    const auto period = vision::dominantPeriod(radii, 6.0, rays / 4.0, /*circular=*/true);
    if (!period.valid) {
        result.detail = "El contorno no se repite: ¿es un engranaje visto de cara?";
        return result;
    }
    const int teeth = static_cast<int>(std::lround(rays / period.period));

    const double tipRadius = decileMean(radii, true);
    const double rootRadius = decileMean(radii, false);
    const double toothHeight = tipRadius - rootRadius;
    if (toothHeight < 2.0) {
        result.detail = "No se aprecian dientes entre los dos radios marcados";
        return result;
    }

    // Recuento independiente: tramos contiguos por encima de un umbral alto.
    // Si no coincide con el periodo, hay que decirlo en vez de elegir en
    // silencio — un diente de más o de menos cambia la pieza entera.
    const double threshold = rootRadius + 0.75 * toothHeight;
    std::size_t start = 0;
    while (start < radii.size() && radii[start] > threshold) {
        ++start;  // empezar fuera de un diente para no partirlo por el cierre
    }
    int runs = 0;
    bool inside = false;
    std::vector<cv::Point2f> tipPoints;
    double bestInRun = 0.0;
    cv::Point2f bestPoint;
    for (std::size_t k = 0; k < radii.size(); ++k) {
        const std::size_t i = (start + k) % radii.size();
        if (radii[i] > threshold) {
            if (!inside) {
                inside = true;
                ++runs;
                bestInRun = 0.0;
            }
            if (radii[i] > bestInRun && profile[i].found) {
                bestInRun = radii[i];
                bestPoint = profile[i].point;
            }
        } else if (inside) {
            inside = false;
            if (bestInRun > 0.0) {
                tipPoints.push_back(bestPoint);
            }
        }
    }
    if (inside && bestInRun > 0.0) {
        tipPoints.push_back(bestPoint);
    }

    // Con las puntas de los dientes se ajusta el círculo de cabeza: da un
    // centro mejor que el marcado a ojo, y su dispersión ES la excentricidad.
    double runout = 0.0;
    double tipCircleRadius = tipRadius;
    if (tipPoints.size() >= 3) {
        const vision::CircleFit tipCircle = vision::fitCircleRobust(tipPoints);
        if (tipCircle.valid) {
            tipCircleRadius = tipCircle.radius;
            for (const auto& p : tipPoints) {
                runout = std::max(runout, std::abs(cv::norm(p - tipCircle.center) -
                                                   tipCircle.radius));
            }
        }
    }

    const double tipDiameter = 2.0 * tipCircleRadius;
    const double rootDiameter = 2.0 * rootRadius;

    result.measured = teeth;  // los dientes son la identidad de la rueda
    result.ok = withinTolerance(config, result.measured);
    result.detail = "z=" + std::to_string(teeth) + " dientes, Ø cabeza=" +
                    fmtLen(tipDiameter, fmt) + ", Ø raíz=" + fmtLen(rootDiameter, fmt) +
                    ", excentricidad=" + fmtLen(runout, fmt);

    if (runs != teeth) {
        result.detail += " (¡ojo! contando picos salen " + std::to_string(runs) +
                         ": revisa los radios marcados)";
    }
    if (period.confidence < 0.5) {
        result.detail += " (repetición débil: confianza " + fmt2(period.confidence) + ")";
    }

    if (fmt.mmPerPixel > 0.0 && teeth > 0) {
        // Módulo de un engranaje recto normalizado: la cabeza sobresale un
        // módulo por encima del primitivo, así que Da = m·(z+2).
        const double moduleFromTip = tipDiameter * fmt.mmPerPixel / (teeth + 2);
        // Comprobación cruzada por la altura del diente, que en la norma es
        // 2,25·m. Ojo: la diferencia de DIÁMETROS es el DOBLE de esa altura,
        // así que el divisor es 4,5 y no 2,25 — con 2,25 el módulo cruzado
        // salía justo el doble y el aviso de discrepancia saltaba en ruedas
        // perfectamente normalizadas.
        const double moduleFromHeight = (tipDiameter - rootDiameter) * fmt.mmPerPixel / 4.5;
        result.detail += ", módulo≈" + fmt2(moduleFromTip) + " mm, Ø primitivo=" +
                         fmt2(moduleFromTip * teeth) + " mm";
        const double disagreement =
            std::abs(moduleFromTip - moduleFromHeight) / std::max(moduleFromTip, 1e-9);
        if (disagreement > 0.15) {
            // No es un recto normalizado sin corrección: dar un módulo a secas
            // sería inventárselo.
            result.detail += " (las dos estimaciones del módulo discrepan: " +
                             fmt2(moduleFromHeight) +
                             " mm por altura de diente; ¿perfil corregido o no normalizado?)";
        }
    } else {
        result.detail += " — el módulo necesita calibración px→mm";
    }

    appendConditionWarnings(result.detail, fmt, meanStrength(profile));
    result.overlayPoints = std::move(tipPoints);
    result.overlayPoints.push_back(center);
    return result;
}

ToolRunResult runPointToLine(const cv::Mat& gray, const Fixture& fixture,
                             const ToolConfig& config, const PointToLineGeometry& g,
                             const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f lineA = toImg(fixture, g.lineA);
    const cv::Point2f lineB = toImg(fixture, g.lineB);
    const cv::Point2f scanA = toImg(fixture, g.scanA);
    const cv::Point2f scanB = toImg(fixture, g.scanB);
    result.overlaySegments.push_back({lineA, lineB});
    result.overlaySegments.push_back({scanA, scanB});

    const cv::Point2f lineDelta = lineB - lineA;
    const double lineLength = cv::norm(lineDelta);
    if (lineLength < 1.0) {
        result.detail = "Línea de referencia degenerada";
        return result;
    }

    const auto edges = detectEdges(gray, scanA, scanB, 5.0F, 1);
    if (edges.empty()) {
        result.detail = "No se detectó ningún borde en el escaneo";
        return result;
    }

    const cv::Point2f p = edges[0].point;
    const double cross = static_cast<double>(lineDelta.x) * (p.y - lineA.y) -
                         static_cast<double>(lineDelta.y) * (p.x - lineA.x);
    result.measured = std::abs(cross) / lineLength;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "d=" + fmtPerpDist(p, lineA, lineB, result.measured, fmt);
    result.overlayPoints.push_back(p);
    return result;
}

ToolRunResult runEdgeFlaw(const cv::Mat& gray, const Fixture& fixture,
                          const ToolConfig& config, const EdgeFlawGeometry& g,
                          const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f p0 = toImg(fixture, g.p0);
    const cv::Point2f p1 = toImg(fixture, g.p1);
    result.overlaySegments.push_back({p0, p1});

    const cv::Point2f delta = p1 - p0;
    const float length = static_cast<float>(cv::norm(delta));
    const int scans = std::clamp(g.scanCount, 3, 200);
    if (length < static_cast<float>(scans)) {
        result.detail = "Tramo demasiado corto para " + std::to_string(scans) + " escaneos";
        return result;
    }
    const cv::Point2f u = delta / length;
    const cv::Point2f n(-u.y, u.x);

    // Un escaneo perpendicular por posición; el borde debería quedar a offset
    // constante. La desviación máxima respecto a la recta ajustada es el flaw.
    std::vector<double> ts;
    std::vector<double> offsets;
    int gap = 0;
    int longestGap = 0;
    for (int k = 0; k < scans; ++k) {
        const float t = length * static_cast<float>(k) / static_cast<float>(scans - 1);
        const cv::Point2f base = p0 + u * t;
        const cv::Point2f from = base - n * (g.scanLength / 2.0F);
        const cv::Point2f to = base + n * (g.scanLength / 2.0F);
        const auto edges = detectEdges(gray, from, to, 1.0F, 1);
        if (edges.empty()) {
            ++gap;
            longestGap = std::max(longestGap, gap);
            continue;
        }
        gap = 0;
        ts.push_back(static_cast<double>(t));
        offsets.push_back(edges[0].position - static_cast<double>(g.scanLength) / 2.0);
        result.overlayPoints.push_back(edges[0].point);
    }

    if (ts.size() < static_cast<std::size_t>(scans) * 6 / 10) {
        result.detail = "Borde no detectado en suficientes escaneos (" +
                        std::to_string(ts.size()) + "/" + std::to_string(scans) + ")";
        return result;
    }
    // Un hueco SEGUIDO no se puede dar por limpio, y esta herramienta lo hacía.
    //
    // Medido: sobre una mella de 26 px con el largo de escaneo en 16, devolvía
    // 0,000 y veredicto OK. Los escaneos que caen sobre la mella no encuentran
    // borde —está más allá de media ventana— y se descartaban en silencio, así
    // que la recta se ajustaba solo con el tramo bueno. O sea que la herramienta
    // daba por perfecto justo el sitio donde estaba el defecto.
    //
    // Es el mismo fallo que «Rebabas y mellas» ya tenía cubierto, con la misma
    // salida: subir el largo de escaneo. Aquí faltaba.
    const double step = static_cast<double>(length) / std::max(1, scans - 1);
    if (blindStretchMatters(longestGap, step, static_cast<double>(length))) {
        result.detail = "No se pudo ver el borde en un tramo de " +
                        fmtLen(longestGap * step, fmt) +
                        ": sube el largo de escaneo (ahora " +
                        fmtLen(static_cast<double>(g.scanLength), fmt) +
                        "). Un defecto más hondo que media ventana se sale de ella, y dar el "
                        "borde por liso ahí sería el peor error posible";
        return result;
    }

    // Recta offset = a + b*t por mínimos cuadrados; residuos = irregularidad.
    const double count = static_cast<double>(ts.size());
    double sumT = 0.0;
    double sumO = 0.0;
    double sumTT = 0.0;
    double sumTO = 0.0;
    for (std::size_t i = 0; i < ts.size(); ++i) {
        sumT += ts[i];
        sumO += offsets[i];
        sumTT += ts[i] * ts[i];
        sumTO += ts[i] * offsets[i];
    }
    const double denom = count * sumTT - sumT * sumT;
    const double slope = std::abs(denom) > 1e-9 ? (count * sumTO - sumT * sumO) / denom : 0.0;
    const double intercept = (sumO - slope * sumT) / count;

    double maxDeviation = 0.0;
    for (std::size_t i = 0; i < ts.size(); ++i) {
        maxDeviation = std::max(maxDeviation,
                                std::abs(offsets[i] - (intercept + slope * ts[i])));
    }

    result.measured = maxDeviation;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "desv. máx=" + fmtLen(result.measured, fmt) + " (" +
                    std::to_string(ts.size()) + " escaneos)";
    return result;
}

// Cuenta manchas dentro de un polígono dado en coords de IMAGEN (convexo o no).
// Rellena las aristas del polígono y los centroides en `result`; devuelve el
// conteo, o -1 si la región cae fuera de la imagen (con el detalle ya puesto).
int countBlobsInPolygon(const cv::Mat& gray, const std::vector<cv::Point2f>& poly,
                        float minArea, bool darkBlobs, double& totalAreaOut,
                        ToolRunResult& result) {
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        result.overlaySegments.push_back({poly[i], poly[(i + 1) % n]});
    }

    std::vector<cv::Point> polyInt;
    polyInt.reserve(n);
    for (const auto& p : poly) {
        polyInt.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    const cv::Rect bounds = cv::boundingRect(polyInt) & cv::Rect(0, 0, gray.cols, gray.rows);
    if (bounds.area() < 9) {
        result.detail = "La región cae fuera de la imagen";
        return -1;
    }

    cv::Mat regionMask = cv::Mat::zeros(bounds.size(), CV_8UC1);
    std::vector<cv::Point> polyLocal;
    polyLocal.reserve(n);
    for (const auto& p : polyInt) {
        polyLocal.emplace_back(p.x - bounds.x, p.y - bounds.y);
    }
    // fillPoly admite polígonos no convexos (a diferencia de fillConvexPoly).
    cv::fillPoly(regionMask, std::vector<std::vector<cv::Point>>{polyLocal}, cv::Scalar(255));

    const cv::Mat roi = gray(bounds);
    cv::Mat binary;
    cv::threshold(roi, binary, 0.0, 255.0,
                  (darkBlobs ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);
    cv::bitwise_and(binary, regionMask, binary);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int count = 0;
    totalAreaOut = 0.0;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < static_cast<double>(minArea)) {
            continue;
        }
        ++count;
        totalAreaOut += area;
        const cv::Moments m = cv::moments(contour);
        if (m.m00 > 0.0) {
            result.overlayPoints.emplace_back(
                static_cast<float>(m.m10 / m.m00 + bounds.x),
                static_cast<float>(m.m01 / m.m00 + bounds.y));
        }
    }
    return count;
}

ToolRunResult runBlob(const cv::Mat& gray, const Fixture& fixture, const ToolConfig& config,
                      const BlobGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);

    // Rectángulo alineado a los ejes de la pieza -> cuadrilátero en imagen.
    const float hw = g.width / 2.0F;
    const float hh = g.height / 2.0F;
    const std::vector<cv::Point2f> quad = {
        toImg(fixture, g.center + cv::Point2f(-hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, hh)),
        toImg(fixture, g.center + cv::Point2f(-hw, hh)),
    };

    double totalArea = 0.0;
    const int count = countBlobsInPolygon(gray, quad, g.minArea, g.darkBlobs, totalArea, result);
    if (count < 0) {
        return result;
    }
    result.measured = count;
    result.ok = withinTolerance(config, result.measured);
    result.detail = std::to_string(count) + " blob(s), área=" + fmtArea(totalArea, fmt);
    return result;
}

ToolRunResult runPolyBlob(const cv::Mat& gray, const Fixture& fixture, const ToolConfig& config,
                          const PolyBlobGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    if (g.vertices.size() < 3) {
        result.detail = "El polígono necesita al menos 3 vértices";
        return result;
    }

    std::vector<cv::Point2f> poly;
    poly.reserve(g.vertices.size());
    for (const auto& v : g.vertices) {
        poly.push_back(toImg(fixture, v));
    }

    double totalArea = 0.0;
    const int count = countBlobsInPolygon(gray, poly, g.minArea, g.darkBlobs, totalArea, result);
    if (count < 0) {
        return result;
    }
    result.measured = count;
    result.ok = withinTolerance(config, result.measured);
    result.detail = std::to_string(count) + " blob(s), área=" + fmtArea(totalArea, fmt);
    return result;
}

std::string fmtPieceCoords(const cv::Point2f& p) {
    // En coordenadas de PIEZA, que son las que no cambian cuando la pieza se
    // mueve. Se dice, porque un par de números sin sistema no significa nada.
    return "(" + fmt2(p.x) + "; " + fmt2(p.y) + ") px en la pieza";
}

ToolRunResult runConstructedPoint(const Fixture& fixture, const ToolConfig& config,
                                  const ConstructedPointGeometry& g,
                                  const DerivedElements& refs) {
    ToolRunResult result = baseResult(config);
    result.informative = true;

    const DerivedElement* a = nullptr;
    const DerivedElement* b = nullptr;
    if (!resolveOperands(refs, config, operandsOf(g.mode), a, b, result)) {
        return result;
    }

    cv::Point2f point;
    switch (g.mode) {
        case PointConstruction::Midpoint:
            // Dos puntos coincidentes NO son un error aquí: su punto medio es
            // ese mismo punto y está perfectamente definido. Solo la recta por
            // dos puntos necesita que sean distintos.
            point = (a->point + b->point) * 0.5F;
            break;
        case PointConstruction::Intersection: {
            const auto hit = intersectLines(*a, *b);
            if (!hit.has_value()) {
                result.detail =
                    "las dos rectas son paralelas (menos de 0,06° entre ellas): no se cortan";
                return result;
            }
            point = *hit;
            break;
        }
        case PointConstruction::Projection:
            point = projectOnLine(a->point, *b);
            break;
        case PointConstruction::CircleCenter:
            point = a->point;
            break;
    }

    result.derived.kind = DerivedKind::Point;
    result.derived.point = point;
    result.ok = true;
    result.detail = std::string(constructionLabel(g.mode)) + ": " + fmtPieceCoords(point);

    const cv::Point2f img = toImg(fixture, point);
    const cv::Point2f anchor = toImg(fixture, g.anchor);
    result.overlayPoints.push_back(img);
    // Un rabito de la etiqueta al punto: el punto calculado casi nunca cae
    // donde el operador dejó el texto, y sin la línea no se sabe cuál es cuál.
    result.overlaySegments.push_back({anchor, img});
    return result;
}

ToolRunResult runConstructedLine(const Fixture& fixture, const ToolConfig& config,
                                 const ConstructedLineGeometry& g,
                                 const DerivedElements& refs) {
    ToolRunResult result = baseResult(config);
    result.informative = true;

    const DerivedElement* a = nullptr;
    const DerivedElement* b = nullptr;
    if (!resolveOperands(refs, config, operandsOf(g.mode), a, b, result)) {
        return result;
    }

    cv::Point2f point;
    cv::Point2f direction;
    switch (g.mode) {
        case LineConstruction::ThroughTwoPoints: {
            const cv::Point2f delta = b->point - a->point;
            const double length = cv::norm(delta);
            if (length < kCoincidentPx) {
                result.detail = "los dos puntos coinciden: no definen ninguna recta";
                return result;
            }
            point = a->point;
            direction = delta / static_cast<float>(length);
            break;
        }
        case LineConstruction::Bisector: {
            // Primero se lleva cada dirección a su forma canónica, para que el
            // resultado no dependa de hacia dónde trazó el operador cada recta.
            // Después se orienta la segunda con la primera, que es lo que deja
            // siempre la bisectriz del ángulo agudo y no la perpendicular.
            const cv::Point2f first = canonicalDirection(a->direction);
            cv::Point2f second = canonicalDirection(b->direction);
            if (first.dot(second) < 0.0F) {
                second = -second;
            }
            // Tras orientarlas el ángulo entre ellas es <= 90°, así que la suma
            // mide entre 1,41 y 2: no puede anularse. Por eso aquí no hay
            // guarda; la que hace falta —dirección degenerada— ya la hizo
            // `operand`.
            const cv::Point2f sum = first + second;
            direction = sum / static_cast<float>(cv::norm(sum));
            const auto hit = intersectLines(*a, *b);
            // Si no se cortan, la bisectriz ES la recta media entre las dos. No
            // es un caso especial que se esquiva: es el mismo resultado por
            // continuidad, y por eso «bisectriz» y «recta media» son una sola
            // construcción y no dos.
            point = hit.has_value() ? *hit
                                    : (a->point + projectOnLine(a->point, *b)) * 0.5F;
            break;
        }
        case LineConstruction::ParallelThrough:
            point = b->point;
            direction = a->direction;
            break;
        case LineConstruction::PerpendicularThrough:
            point = b->point;
            direction = {-a->direction.y, a->direction.x};
            break;
    }

    result.derived.kind = DerivedKind::Line;
    result.derived.point = point;
    result.derived.direction = direction;
    result.ok = true;

    // Una recta no tiene sentido, solo dirección: su ángulo vive en [0°,180°).
    double angle = std::atan2(direction.y, direction.x) * 180.0 / CV_PI;
    if (angle < 0.0) {
        angle += 180.0;
    }
    result.measured = angle;
    result.measuredIsAngle = true;
    result.detail = std::string(constructionLabel(g.mode)) + ": " + fmt2(angle) + "° por " +
                    fmtPieceCoords(point);

    const cv::Point2f base = projectOnLine(g.anchor, result.derived);
    result.overlaySegments.push_back({toImg(fixture, base - kDrawHalfLengthPx * direction),
                                      toImg(fixture, base + kDrawHalfLengthPx * direction)});
    return result;
}

// Radio de acuerdo con comprobación de tangencia (M3).
//
// El radio sale casi gratis: `decomposeContour` ya separa rectas de arcos y ya
// ajusta el círculo. Lo que aporta esta herramienta es lo OTRO: el ángulo con
// el que el arco entra en cada recta vecina. Un acuerdo que no empalma tangente
// es un defecto de mecanizado que el radio por sí solo no delata — dos piezas
// con el mismo radio y distinta tangencia no son la misma pieza.
ToolRunResult runFillet(const cv::Mat& gray, const Fixture& fixture,
                        const ToolConfig& config, const FilletGeometry& g,
                        const Fmt& fmt) {
    ToolRunResult result = baseResult(config);

    const float hw = g.width / 2.0F;
    const float hh = g.height / 2.0F;
    const std::vector<cv::Point2f> quad{
        toImg(fixture, g.center + cv::Point2f(-hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, hh)),
        toImg(fixture, g.center + cv::Point2f(-hw, hh))};
    for (std::size_t i = 0; i < quad.size(); ++i) {
        result.overlaySegments.push_back({quad[i], quad[(i + 1) % quad.size()]});
    }
    std::vector<cv::Point> quadInt;
    for (const auto& p : quad) {
        quadInt.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    const cv::Rect selection = cv::boundingRect(quadInt);
    // Igual que el Chaflán: el recuadro SELECCIONA, no recorta. Recortar
    // convertiría sus propios cortes en tramos rectos del contorno.
    cv::Rect bounds = selection;
    bounds -= cv::Point(selection.width / 2, selection.height / 2);
    bounds += cv::Size(selection.width, selection.height);
    bounds &= cv::Rect(0, 0, gray.cols, gray.rows);
    if (bounds.area() < 100) {
        result.detail = "La región cae fuera de la imagen";
        return result;
    }

    cv::Mat binary;
    cv::threshold(gray(bounds), binary, 0.0, 255.0,
                  (g.darkPiece ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        result.detail = "No se ve ningún borde dentro del recuadro";
        return result;
    }
    const auto& outer = *std::max_element(
        contours.begin(), contours.end(), [](const auto& a, const auto& b) {
            return cv::contourArea(a) < cv::contourArea(b);
        });

    const auto primitives = vision::decomposeContour(outer);
    const cv::Rect2f selectionLocal(
        static_cast<float>(selection.x - bounds.x), static_cast<float>(selection.y - bounds.y),
        static_cast<float>(selection.width), static_cast<float>(selection.height));

    // Se busca un ARCO con una recta a cada lado, todos dentro del recuadro.
    int arcIndex = -1;
    for (int i = 1; i + 1 < static_cast<int>(primitives.size()); ++i) {
        const auto& previous = primitives[static_cast<std::size_t>(i - 1)];
        const auto& arc = primitives[static_cast<std::size_t>(i)];
        const auto& next = primitives[static_cast<std::size_t>(i + 1)];
        if (arc.kind != vision::PrimitiveKind::Arc || arc.radius <= 1.0) {
            continue;
        }
        if (previous.kind != vision::PrimitiveKind::Line ||
            next.kind != vision::PrimitiveKind::Line) {
            continue;
        }
        if (!selectionLocal.contains(arc.mid) || previous.length < 8.0 ||
            next.length < 8.0) {
            continue;
        }
        if (arcIndex < 0 ||
            arc.length > primitives[static_cast<std::size_t>(arcIndex)].length) {
            arcIndex = i;
        }
    }
    if (arcIndex < 0) {
        result.detail = "No se ve un arco con una recta a cada lado dentro del recuadro: "
                        "¿es un chaflán en vez de un acuerdo, o falta encuadrar las caras?";
        return result;
    }

    const auto& before = primitives[static_cast<std::size_t>(arcIndex - 1)];
    const auto& arc = primitives[static_cast<std::size_t>(arcIndex)];
    const auto& after = primitives[static_cast<std::size_t>(arcIndex + 1)];

    // La TANGENTE del arco en un extremo es perpendicular al radio en ese punto.
    // Comparada con la dirección de la recta vecina, su diferencia es lo que
    // separa un acuerdo bien hecho de uno con un salto.
    const auto unit = [](const cv::Point2f& v) {
        const double length = cv::norm(v);
        return length > 1e-9 ? v / static_cast<float>(length) : cv::Point2f(1.0F, 0.0F);
    };
    const auto tangentAt = [&arc, &unit](const cv::Point2f& p) {
        const cv::Point2f radial = unit(p - arc.center);
        return cv::Point2f(-radial.y, radial.x);
    };
    const auto angleOf = [](const cv::Point2f& a, const cv::Point2f& b) {
        const double dot =
            std::abs(static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y);
        const double cross =
            std::abs(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x);
        return std::atan2(cross, dot) * 180.0 / kPi;
    };

    const double deviationStart =
        angleOf(tangentAt(arc.start), unit(before.end - before.start));
    const double deviationEnd = angleOf(tangentAt(arc.end), unit(after.end - after.start));
    const double worst = std::max(deviationStart, deviationEnd);

    switch (g.measure) {
        case FilletMeasure::Radius: result.measured = arc.radius; break;
        case FilletMeasure::Tangency:
            result.measured = worst;
            result.measuredIsAngle = true;
            break;
    }
    result.ok = withinTolerance(config, result.measured);
    result.detail = std::string(filletMeasureLabel(g.measure)) + " · R=" +
                    fmtLen(arc.radius, fmt) + " · barrido " + fmt2(arc.sweepDeg) +
                    "° · tangencia " + fmt2(deviationStart) + "° y " + fmt2(deviationEnd) +
                    "° (0 = empalme perfecto)";

    const cv::Point2f offset(static_cast<float>(bounds.x), static_cast<float>(bounds.y));
    result.overlayPoints.push_back(arc.center + offset);
    result.overlayPoints.push_back(arc.start + offset);
    result.overlayPoints.push_back(arc.end + offset);
    result.overlaySegments.push_back({arc.center + offset, arc.start + offset});
    result.overlaySegments.push_back({arc.center + offset, arc.end + offset});
    return result;
}

// Chaflán (M2): el ángulo del bisel y sus dos catetos.
//
// Cero algoritmo nuevo: `decomposeContour` ya separa el contorno en rectas y
// arcos, `fitLineTotal` ya ajusta una recta y `intersectLines` ya las corta.
// Lo único que hay aquí es elegir bien las TRES rectas y saber desde dónde se
// miden los catetos.
ToolRunResult runChamfer(const cv::Mat& gray, const Fixture& fixture,
                         const ToolConfig& config, const ChamferGeometry& g,
                         const Fmt& fmt) {
    ToolRunResult result = baseResult(config);

    const float hw = g.width / 2.0F;
    const float hh = g.height / 2.0F;
    const std::vector<cv::Point2f> quad{
        toImg(fixture, g.center + cv::Point2f(-hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, hh)),
        toImg(fixture, g.center + cv::Point2f(-hw, hh))};
    for (std::size_t i = 0; i < quad.size(); ++i) {
        result.overlaySegments.push_back({quad[i], quad[(i + 1) % quad.size()]});
    }

    std::vector<cv::Point> quadInt;
    for (const auto& p : quad) {
        quadInt.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    const cv::Rect selection = cv::boundingRect(quadInt);
    // El recuadro SELECCIONA qué tramos del borde se miran; NO recorta la pieza.
    //
    // Recortarla era el primer intento y estaba mal: los cortes del propio
    // recuadro se convertían en tramos rectos del contorno, se colaban como
    // "caras" y el chaflán salía medido contra un borde que no existe en la
    // pieza. Por eso se binariza una zona HOLGADA alrededor y luego se filtran
    // los tramos por si caen dentro del recuadro que dibujó el operador.
    cv::Rect bounds = selection;
    bounds -= cv::Point(selection.width / 2, selection.height / 2);
    bounds += cv::Size(selection.width, selection.height);
    bounds &= cv::Rect(0, 0, gray.cols, gray.rows);
    if (bounds.area() < 100) {
        result.detail = "La región cae fuera de la imagen";
        return result;
    }

    cv::Mat binary;
    cv::threshold(gray(bounds), binary, 0.0, 255.0,
                  (g.darkPiece ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        result.detail = "No se ve ningún borde dentro del recuadro";
        return result;
    }
    const auto& outer = *std::max_element(
        contours.begin(), contours.end(), [](const auto& a, const auto& b) {
            return cv::contourArea(a) < cv::contourArea(b);
        });

    // Descomposición con el listón del ARCO más alto de lo normal, y por un
    // motivo concreto: el bisel de un chaflán es un tramo RECTO y corto, y en
    // esa longitud una circunferencia de radio grande lo explica un pelo mejor
    // por el dentado de la rasterización. Con el umbral por defecto (15° de
    // barrido) un bisel de 68 px salía clasificado como arco y la herramienta
    // decía no ver tres tramos rectos. Un acuerdo de verdad barre mucho más que
    // 45°, así que subirlo aquí no confunde un chaflán con un redondeo — al
    // contrario, es lo que los separa.
    vision::DecomposeOptions options;
    options.minArcSweepDeg = 45.0;
    const auto primitives = vision::decomposeContour(outer, options);
    // Solo los tramos RECTOS, con longitud apreciable y que caigan DENTRO del
    // recuadro: en las esquinas salen trocitos de dos o tres puntos que no
    // representan ningún lado, y fuera del recuadro está el resto de la pieza.
    struct Straight {
        cv::Point2f start;
        cv::Point2f end;
        double length = 0.0;
    };
    const cv::Rect2f selectionLocal(
        static_cast<float>(selection.x - bounds.x), static_cast<float>(selection.y - bounds.y),
        static_cast<float>(selection.width), static_cast<float>(selection.height));
    std::vector<Straight> sides;
    int straightOutside = 0;
    int curved = 0;
    for (const auto& p : primitives) {
        if (p.kind != vision::PrimitiveKind::Line || p.length < 8.0) {
            if (p.length >= 8.0) {
                ++curved;
            }
            continue;
        }
        if (!selectionLocal.contains(p.mid)) {
            ++straightOutside;
            continue;
        }
        sides.push_back({p.start, p.end, p.length});
    }
    if (sides.size() < 3) {
        // Se dice CUÁNTOS quedaron fuera del recuadro y cuántos salieron
        // curvos: son las dos razones por las que un encuadre falla, y sin
        // distinguirlas el operador no sabe si agrandar el recuadro o si lo que
        // tiene delante es un redondeo y no un chaflán.
        result.detail = "Hacen falta tres tramos rectos (cara, bisel, cara) y se ven " +
                        std::to_string(sides.size()) + " dentro del recuadro (" +
                        std::to_string(straightOutside) + " rectos fuera, " +
                        std::to_string(curved) + " curvos): agranda el recuadro para " +
                        "coger un trozo de las dos caras";
        return result;
    }

    // El bisel es el tramo corto que tiene una cara larga a cada lado. Pero no
    // basta con eso: `decomposeContour` puede partir una cara larga en dos
    // trozos casi alineados, y esa terna «cara, trocito, cara» puntúa igual de
    // bien. Como las dos caras entonces son casi paralelas, su esquina virtual
    // cae a decenas de miles de píxeles — así salió un cateto de 75 000 px
    // antes de exigir esto.
    //
    // Se pide, además de que el del medio sea el corto, que las DOS CARAS
    // FORMEN ESQUINA DE VERDAD: al menos 15° entre ellas. Un chaflán en una
    // esquina más abierta que 165° no es un chaflán.
    constexpr double kMinCornerDeg = 15.0;
    const auto directionOf = [](const Straight& side) {
        const cv::Point2f delta = side.end - side.start;
        const double length = cv::norm(delta);
        return length > 1e-9 ? delta / static_cast<float>(length) : cv::Point2f(1.0F, 0.0F);
    };
    const auto angleOf = [](const cv::Point2f& a, const cv::Point2f& b) {
        const double dot =
            std::abs(static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y);
        const double cross =
            std::abs(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x);
        return std::atan2(cross, dot) * 180.0 / kPi;
    };

    std::size_t best = sides.size();
    double bestScore = -1.0;
    for (std::size_t i = 1; i + 1 < sides.size(); ++i) {
        const double corner =
            angleOf(directionOf(sides[i - 1]), directionOf(sides[i + 1]));
        if (corner < kMinCornerDeg) {
            continue;  // las dos "caras" son la misma partida en dos
        }
        const double score = std::min(sides[i - 1].length, sides[i + 1].length) /
                             std::max(sides[i].length, 1e-6);
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (best >= sides.size()) {
        result.detail = "No se encontró un bisel entre dos caras que formen esquina: "
                        "encuadra la esquina achaflanada con un trozo de las dos caras";
        return result;
    }
    const Straight& faceA = sides[best - 1];
    const Straight& bevel = sides[best];
    const Straight& faceB = sides[best + 1];

    const auto asLine = [](const Straight& side) {
        DerivedElement line;
        line.kind = DerivedKind::Line;
        line.point = side.start;
        const cv::Point2f delta = side.end - side.start;
        const double length = cv::norm(delta);
        line.direction = length > 1e-9 ? delta / static_cast<float>(length)
                                       : cv::Point2f(1.0F, 0.0F);
        return line;
    };
    const DerivedElement lineA = asLine(faceA);
    const DerivedElement lineBevel = asLine(bevel);
    const DerivedElement lineB = asLine(faceB);

    const auto cornerVirtual = intersectLines(lineA, lineB);
    const auto startBevel = intersectLines(lineA, lineBevel);
    const auto endBevel = intersectLines(lineBevel, lineB);
    if (!cornerVirtual.has_value() || !startBevel.has_value() || !endBevel.has_value()) {
        result.detail = "Las tres rectas no se cortan: alguna sale paralela a otra";
        return result;
    }
    // La esquina virtual tiene que caer CERCA del bisel. Si sale a diez veces su
    // largo, lo que se han cortado no son las dos caras de un chaflán, y los
    // catetos que saldrían de ahí serían números enormes con pinta de medida.
    const cv::Point2f bevelMid = (bevel.start + bevel.end) * 0.5F;
    if (cv::norm(*cornerVirtual - bevelMid) > 10.0 * std::max(bevel.length, 1.0)) {
        result.detail = "La esquina virtual sale demasiado lejos del bisel: las dos caras "
                        "encuadradas son casi paralelas y no forman un chaflán";
        return result;
    }

    const auto angleBetween = [](const cv::Point2f& a, const cv::Point2f& b) {
        const double dot = std::abs(static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y);
        const double cross =
            std::abs(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x);
        return std::atan2(cross, dot) * 180.0 / kPi;
    };
    // Los catetos se miden desde la ESQUINA VIRTUAL, que es de donde los mide el
    // plano. No hay ningún punto de la pieza ahí: hay que construirla cortando
    // las dos caras, y por eso el recuadro tiene que abarcar un trozo de ambas.
    const double legFirst = cv::norm(*startBevel - *cornerVirtual);
    const double legSecond = cv::norm(*endBevel - *cornerVirtual);
    const double angleFirst = angleBetween(lineA.direction, lineBevel.direction);
    const double angleSecond = angleBetween(lineB.direction, lineBevel.direction);

    // Se ordenan por TAMAÑO y no por el orden en que aparecieron. Cuál cara
    // recorre antes `findContours` es un detalle interno que el operador no
    // puede predecir, y con un chaflán asimétrico hacía que los dos catetos
    // salieran intercambiados de una pieza a otra.
    const bool firstIsLonger = legFirst >= legSecond;
    const double legLong = firstIsLonger ? legFirst : legSecond;
    const double legShort = firstIsLonger ? legSecond : legFirst;
    // El ángulo va emparejado con SU cara: el del cateto mayor es el que forma
    // el bisel con la cara sobre la que se mide ese cateto.
    const double angleLong = firstIsLonger ? angleFirst : angleSecond;
    const double angleShort = firstIsLonger ? angleSecond : angleFirst;

    switch (g.measure) {
        case ChamferMeasure::Angle:
            result.measured = angleLong;
            result.measuredIsAngle = true;
            break;
        case ChamferMeasure::LegLong: result.measured = legLong; break;
        case ChamferMeasure::LegShort: result.measured = legShort; break;
    }
    result.ok = withinTolerance(config, result.measured);
    result.detail = std::string(chamferMeasureLabel(g.measure)) + " · cateto mayor " +
                    fmtLen(legLong, fmt) + " a " + fmt2(angleLong) + "° · cateto menor " +
                    fmtLen(legShort, fmt) + " a " + fmt2(angleShort) + "°";

    const cv::Point2f offset(static_cast<float>(bounds.x), static_cast<float>(bounds.y));
    result.overlayPoints.push_back(*cornerVirtual + offset);
    result.overlayPoints.push_back(*startBevel + offset);
    result.overlayPoints.push_back(*endBevel + offset);
    result.overlaySegments.push_back({*startBevel + offset, *endBevel + offset});
    result.overlaySegments.push_back({*cornerVirtual + offset, *startBevel + offset});
    result.overlaySegments.push_back({*cornerVirtual + offset, *endBevel + offset});
    return result;
}

// Anchura mínima y diámetro máximo de la silueta (M1).
//
// «Lo de máx y mín»: la medida más grande y la más pequeña de la pieza EN
// CUALQUIER DIRECCIÓN, no en la que el operador acertó a trazar. Las dos salen
// del casco convexo y ninguna sale de `minAreaRect`, que minimiza el ÁREA: ni
// su lado corto es la anchura mínima ni su diagonal es el diámetro.
ToolRunResult runExtremes(const cv::Mat& gray, const Fixture& fixture,
                          const ToolConfig& config, const ExtremesGeometry& g,
                          const Fmt& fmt) {
    ToolRunResult result = baseResult(config);

    const float hw = g.width / 2.0F;
    const float hh = g.height / 2.0F;
    const std::vector<cv::Point2f> quad{
        toImg(fixture, g.center + cv::Point2f(-hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, hh)),
        toImg(fixture, g.center + cv::Point2f(-hw, hh))};
    for (std::size_t i = 0; i < quad.size(); ++i) {
        result.overlaySegments.push_back({quad[i], quad[(i + 1) % quad.size()]});
    }

    std::vector<cv::Point> quadInt;
    for (const auto& p : quad) {
        quadInt.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    const cv::Rect bounds = cv::boundingRect(quadInt) & cv::Rect(0, 0, gray.cols, gray.rows);
    if (bounds.area() < 25) {
        result.detail = "La región cae fuera de la imagen";
        return result;
    }
    cv::Mat regionMask = cv::Mat::zeros(bounds.size(), CV_8UC1);
    std::vector<cv::Point> quadLocal;
    for (const auto& p : quadInt) {
        quadLocal.emplace_back(p.x - bounds.x, p.y - bounds.y);
    }
    cv::fillPoly(regionMask, std::vector<std::vector<cv::Point>>{quadLocal}, cv::Scalar(255));

    cv::Mat binary;
    cv::threshold(gray(bounds), binary, 0.0, 255.0,
                  (g.darkPiece ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);
    cv::bitwise_and(binary, regionMask, binary);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        result.detail = "No se ve ninguna figura dentro del recuadro";
        return result;
    }
    const auto& outer = *std::max_element(
        contours.begin(), contours.end(), [](const auto& a, const auto& b) {
            return cv::contourArea(a) < cv::contourArea(b);
        });
    if (cv::contourArea(outer) < 25.0) {
        result.detail = "No se ve ninguna figura dentro del recuadro";
        return result;
    }

    const cv::Point2f offset(static_cast<float>(bounds.x), static_cast<float>(bounds.y));
    std::vector<cv::Point2f> points;
    points.reserve(outer.size());
    for (const auto& p : outer) {
        points.emplace_back(cv::Point2f(p) + offset);
    }

    const vision::MinimumZone narrow = vision::minimumZoneBand(points);
    const vision::MaximumSpan wide = vision::maximumSpan(points);
    if (!narrow.valid || !wide.valid) {
        result.detail = "No se pudieron calcular los extremos de la silueta";
        return result;
    }

    // Las direcciones, para que el operador sepa POR DÓNDE hay que meterla.
    const double narrowAngle =
        std::fmod(std::atan2(narrow.direction.y, narrow.direction.x) * 180.0 / kPi + 180.0,
                  180.0);
    const cv::Point2f spanDir = wide.to - wide.from;
    const double wideAngle =
        std::fmod(std::atan2(spanDir.y, spanDir.x) * 180.0 / kPi + 180.0, 180.0);

    switch (g.measure) {
        case ExtremeMeasure::MinWidth: result.measured = narrow.width; break;
        case ExtremeMeasure::MaxSpan: result.measured = wide.length; break;
    }
    result.ok = withinTolerance(config, result.measured);
    result.detail = std::string(extremeMeasureLabel(g.measure)) + " · anchura mín " +
                    fmtLen(narrow.width, fmt) + " (banda a " + fmt2(narrowAngle) +
                    "°) · diámetro máx " + fmtLen(wide.length, fmt) + " (a " +
                    fmt2(wideAngle) + "°)";

    // El par más separado y las dos rectas de la banda: sin verlos, los dos
    // números no se pueden comprobar a ojo.
    result.overlayPoints.push_back(wide.from);
    result.overlayPoints.push_back(wide.to);
    result.overlaySegments.push_back({wide.from, wide.to});
    const cv::Point2f bandNormal(-narrow.direction.y, narrow.direction.x);
    const float half = static_cast<float>(narrow.width / 2.0);
    const float reach = static_cast<float>(wide.length / 2.0);
    for (const float side : {-1.0F, 1.0F}) {
        const cv::Point2f centre = narrow.point + bandNormal * (half * side);
        result.overlaySegments.push_back(
            {centre - narrow.direction * reach, centre + narrow.direction * reach});
    }
    return result;
}

// Perfil de línea contra un nominal (G7).
//
// El nominal es el contorno de la pieza buena, capturado al crear la
// herramienta y guardado dentro de ella. El plan pedía además cargarlo de un
// DXF; se entrega la mitad que el propio plan preveía como alternativa, que es
// la que aporta el valor sin un parser de por medio.
//
// No hay ICP. Los dos contornos están en coordenadas de PIEZA y el Position
// Fixture ya los alineó: meter un ajuste encima sería alinear dos veces, y
// dejaría que el ajuste se comiera una desviación real girando el nominal para
// que encajara.
ToolRunResult runProfile(const cv::Mat& gray, const Fixture& fixture,
                         const ToolConfig& config, const ProfileGeometry& g,
                         const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    if (g.nominal.size() < 3) {
        result.detail = "esta herramienta no tiene nominal guardado";
        return result;
    }

    // La silueta actual, buscada donde está el nominal y con margen para que un
    // exceso de material no se salga del recorte.
    cv::Rect box = cv::boundingRect(g.nominal);
    std::vector<cv::Point2f> corners{
        toImg(fixture, cv::Point2f(box.x, box.y)),
        toImg(fixture, cv::Point2f(box.x + box.width, box.y)),
        toImg(fixture, cv::Point2f(box.x + box.width, box.y + box.height)),
        toImg(fixture, cv::Point2f(box.x, box.y + box.height))};
    std::vector<cv::Point> cornersInt;
    for (const auto& p : corners) {
        cornersInt.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    constexpr int kPad = 20;
    cv::Rect bounds = cv::boundingRect(cornersInt);
    bounds -= cv::Point(kPad, kPad);
    bounds += cv::Size(2 * kPad, 2 * kPad);
    bounds &= cv::Rect(0, 0, gray.cols, gray.rows);
    if (bounds.area() < 100) {
        result.detail = "el nominal cae fuera de la imagen";
        return result;
    }

    cv::Mat binary;
    // Con la polaridad de la herramienta, como el resto de las de silueta. Antes
    // era `THRESH_BINARY_INV` a secas, o sea «la pieza es siempre lo oscuro»: con
    // el montaje contrario —contraluz, pieza clara sobre fondo negro— comparaba
    // el nominal contra el FONDO y devolvía 125,7 px de perfil con veredicto
    // bueno. No un aviso: un número con toda la pinta de ser una medida.
    cv::threshold(gray(bounds), binary, 0.0, 255.0,
                  (g.darkPiece ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        result.detail = "no se ve ninguna silueta que comparar";
        return result;
    }
    const auto& biggest = *std::max_element(
        contours.begin(), contours.end(), [](const auto& a, const auto& b) {
            return cv::contourArea(a) < cv::contourArea(b);
        });

    // A coordenadas de pieza, que es donde vive el nominal.
    const cv::Point2f offset(static_cast<float>(bounds.x), static_cast<float>(bounds.y));
    std::vector<cv::Point2f> measured;
    measured.reserve(biggest.size());
    for (const auto& p : biggest) {
        measured.push_back(vision::toPieceCoords(fixture, cv::Point2f(p) + offset));
    }

    const vision::ProfileDeviation deviation = vision::profileDeviation(measured, g.nominal);
    if (!deviation.valid) {
        result.detail = "no se pudo comparar con el nominal";
        return result;
    }

    result.measured = deviation.zoneWidth;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "perfil: zona " + fmtLen(deviation.zoneWidth, fmt) + " · sobra " +
                    fmtLen(deviation.worstOutside, fmt) + ", falta " +
                    fmtLen(deviation.worstInside, fmt) + " · " +
                    std::to_string(deviation.comparedPoints) + " puntos";

    // El nominal dibujado y el punto donde peor va: sin verlos, un número de
    // perfil no dice dónde mirar.
    for (std::size_t i = 0; i < g.nominal.size(); i += 4) {
        const std::size_t next = (i + 4) % g.nominal.size();
        result.overlaySegments.push_back({toImg(fixture, g.nominal[i]),
                                          toImg(fixture, g.nominal[next])});
    }
    result.overlayPoints.push_back(toImg(fixture, deviation.worstAt));
    return result;
}

// Patrón de agujeros (G6): la cota de una brida.
//
// Cero algoritmo nuevo: los agujeros salen de la jerarquía de contornos, el
// centro de cada uno del ajuste de círculo que ya existe, y el círculo
// primitivo de ajustar otro círculo a esos centros.
//
// La referencia es EL PROPIO PATRÓN —su primitivo ajustado y su reparto
// angular—, no un datum de fuera. Es lo que se quiere aquí: girar la brida
// entera no puede sacar de tolerancia unos agujeros que están donde deben. Para
// medir contra un datum externo está la Posición verdadera.
ToolRunResult runBoltPattern(const cv::Mat& gray, const Fixture& fixture,
                             const ToolConfig& config, const BoltPatternGeometry& g,
                             const Fmt& fmt) {
    ToolRunResult result = baseResult(config);

    const float hw = g.width / 2.0F;
    const float hh = g.height / 2.0F;
    const std::vector<cv::Point2f> quad{
        toImg(fixture, g.center + cv::Point2f(-hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, hh)),
        toImg(fixture, g.center + cv::Point2f(-hw, hh))};
    for (std::size_t i = 0; i < quad.size(); ++i) {
        result.overlaySegments.push_back({quad[i], quad[(i + 1) % quad.size()]});
    }

    std::vector<cv::Point> quadInt;
    for (const auto& p : quad) {
        quadInt.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    const cv::Rect bounds = cv::boundingRect(quadInt) & cv::Rect(0, 0, gray.cols, gray.rows);
    if (bounds.area() < 100) {
        result.detail = "La región cae fuera de la imagen";
        return result;
    }
    cv::Mat regionMask = cv::Mat::zeros(bounds.size(), CV_8UC1);
    std::vector<cv::Point> quadLocal;
    for (const auto& p : quadInt) {
        quadLocal.emplace_back(p.x - bounds.x, p.y - bounds.y);
    }
    cv::fillPoly(regionMask, std::vector<std::vector<cv::Point>>{quadLocal}, cv::Scalar(255));

    cv::Mat binary;
    cv::threshold(gray(bounds), binary, 0.0, 255.0,
                  (g.darkPiece ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);
    cv::bitwise_and(binary, regionMask, binary);

    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binary, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE);
    int flange = -1;
    double flangeArea = 0.0;
    for (int i = 0; i < static_cast<int>(contours.size()); ++i) {
        if (hierarchy[static_cast<std::size_t>(i)][3] >= 0) {
            continue;
        }
        const double area = cv::contourArea(contours[static_cast<std::size_t>(i)]);
        if (area > flangeArea) {
            flangeArea = area;
            flange = i;
        }
    }
    if (flange < 0) {
        result.detail = "No se ve ninguna pieza dentro del recuadro";
        return result;
    }

    // Los agujeros son los hijos de la brida. Se descartan las motas: el ruido
    // de la binarización dejaría "agujeros" de tres píxeles que arruinarían
    // tanto el primitivo como el reparto angular.
    constexpr double kMinHoleArea = 40.0;
    const cv::Point2f offset(static_cast<float>(bounds.x), static_cast<float>(bounds.y));
    std::vector<cv::Point2f> centres;
    for (int i = hierarchy[static_cast<std::size_t>(flange)][2]; i >= 0;
         i = hierarchy[static_cast<std::size_t>(i)][0]) {
        const auto& hole = contours[static_cast<std::size_t>(i)];
        if (cv::contourArea(hole) < kMinHoleArea) {
            continue;
        }
        std::vector<cv::Point2f> points;
        points.reserve(hole.size());
        for (const auto& p : hole) {
            points.emplace_back(cv::Point2f(p) + offset);
        }
        const vision::CircleFit fit = vision::fitCircleRobust(points);
        centres.push_back(fit.valid ? fit.center : [&points] {
            cv::Point2f sum(0.0F, 0.0F);
            for (const auto& p : points) {
                sum += p;
            }
            return sum / static_cast<float>(points.size());
        }());
    }

    const int found = static_cast<int>(centres.size());
    if (g.expectedHoles > 0 && found != g.expectedHoles) {
        // Que falte (o sobre) un agujero ES el defecto: no se sigue midiendo un
        // reparto angular que ya no tiene sentido.
        result.measured = found;
        result.detail = "se esperaban " + std::to_string(g.expectedHoles) +
                        " agujeros y se ven " + std::to_string(found);
        return result;
    }
    if (found < 3) {
        result.detail = "hacen falta al menos 3 agujeros para ajustar el círculo "
                        "primitivo y se ven " + std::to_string(found);
        return result;
    }

    const vision::CircleFit pitch = vision::fitCircleRobust(centres);
    if (!pitch.valid) {
        result.detail = "no se pudo ajustar el círculo primitivo a los centros";
        return result;
    }

    // Reparto ideal: los agujeros ordenados por ángulo, con el paso nominal
    // 360/n, y la FASE que mejor encaja con lo medido. Sin ajustar la fase, una
    // brida perfecta pero girada saldría toda fuera de tolerancia.
    struct Hole {
        double angle = 0.0;
        cv::Point2f centre;
    };
    std::vector<Hole> holes;
    holes.reserve(centres.size());
    for (const auto& c : centres) {
        holes.push_back({std::atan2(static_cast<double>(c.y) - pitch.center.y,
                                    static_cast<double>(c.x) - pitch.center.x),
                         c});
    }
    std::sort(holes.begin(), holes.end(),
              [](const Hole& a, const Hole& b) { return a.angle < b.angle; });

    const double step = 2.0 * kPi / found;
    // Media circular de los restos: es la forma correcta de promediar ángulos.
    // Con la media aritmética, un resto de 359° y otro de 1° darían 180°.
    double sumSin = 0.0;
    double sumCos = 0.0;
    for (int k = 0; k < found; ++k) {
        const double residual = holes[static_cast<std::size_t>(k)].angle - k * step;
        sumSin += std::sin(residual);
        sumCos += std::cos(residual);
    }
    const double phase = std::atan2(sumSin, sumCos);

    double worst = 0.0;
    double worstAngleDeg = 0.0;
    double worstStep = 0.0;
    for (int k = 0; k < found; ++k) {
        const double ideal = phase + k * step;
        const cv::Point2f target(
            static_cast<float>(pitch.center.x + pitch.radius * std::cos(ideal)),
            static_cast<float>(pitch.center.y + pitch.radius * std::sin(ideal)));
        const double deviation = cv::norm(holes[static_cast<std::size_t>(k)].centre - target);
        if (deviation > worst) {
            worst = deviation;
            // Se guarda su POSICIÓN ANGULAR y no su índice. Un "agujero nº 5"
            // no le sirve de nada al operador: el orden es el que sale del
            // barrido angular, y en la pieza no hay ningún número escrito. Un
            // ángulo sí se localiza mirando la brida.
            worstAngleDeg = holes[static_cast<std::size_t>(k)].angle * 180.0 / kPi;
            if (worstAngleDeg < 0.0) {
                worstAngleDeg += 360.0;
            }
        }
        // Paso angular real entre este agujero y el siguiente.
        const int next = (k + 1) % found;
        double gap = holes[static_cast<std::size_t>(next)].angle -
                     holes[static_cast<std::size_t>(k)].angle;
        while (gap <= 0.0) {
            gap += 2.0 * kPi;
        }
        worstStep = std::max(worstStep, std::abs(gap - step) * 180.0 / kPi);
        result.overlayPoints.push_back(holes[static_cast<std::size_t>(k)].centre);
        result.overlaySegments.push_back({target, holes[static_cast<std::size_t>(k)].centre});
    }

    // Como en la posición verdadera, la cota es un DIÁMETRO de zona.
    result.measured = 2.0 * worst;
    result.ok = withinTolerance(config, result.measured);
    result.detail = std::to_string(found) + " agujeros · Ø primitivo=" +
                    fmtLen(2.0 * pitch.radius, fmt) + " · paso " +
                    fmt2(360.0 / found) + "° (desvío máx " + fmt2(worstStep) + "°) · peor " +
                    "agujero a " + fmt2(worstAngleDeg) + "°, Ø" +
                    fmtLen(result.measured, fmt) + " fuera de su sitio";

    // El primitivo como referencia: es el datum natural de una brida.
    result.derived.kind = DerivedKind::Circle;
    result.derived.point = vision::toPieceCoords(fixture, pitch.center);
    result.derived.radius = pitch.radius;
    return result;
}

// Desviación de centros (G5): lo que NO es concentricidad.
//
// La concentricidad normativa se retiró de ASME Y14.5-2018 por inverificable de
// forma repetible. La pregunta que la gente hacía con ella —«¿están estos dos
// agujeros centrados uno con otro?»— sigue siendo legítima, y tiene una
// respuesta que sí se puede medir y repetir: la distancia entre los centros.
//
// Se le da ese nombre y no el del símbolo retirado. Dar un número correcto bajo
// el nombre de una cota que ya no existe sería peor que no darlo, porque
// acabaría copiado en un informe como si fuera la cota.
ToolRunResult runCentreOffset(const Fixture& fixture, const ToolConfig& config,
                              const CentreOffsetGeometry& g, const Fmt& fmt,
                              const DerivedElements& refs) {
    ToolRunResult result = baseResult(config);

    std::string why;
    const DerivedElement* first =
        operand(refs, config.reference, OperandKind::Point, "primera", why);
    if (first == nullptr) {
        result.detail = why;
        return result;
    }
    const DerivedElement* second =
        operand(refs, config.reference2, OperandKind::Point, "segunda", why);
    if (second == nullptr) {
        result.detail = why;
        return result;
    }

    const cv::Point2f offset = second->point - first->point;
    result.measured = cv::norm(offset);
    result.ok = withinTolerance(config, result.measured);

    const cv::Point2f a = toImg(fixture, first->point);
    const cv::Point2f b = toImg(fixture, second->point);
    result.detail = "desviación de centros=" + fmtLenPts(a, b, fmt) + " (dx=" +
                    fmt2(offset.x) + ", dy=" + fmt2(offset.y) +
                    " px) — no es concentricidad";
    result.overlayPoints.push_back(a);
    result.overlayPoints.push_back(b);
    result.overlaySegments.push_back({a, b});
    // El punto medio de los dos centros, por si alguien quiere referenciarlo:
    // es el eje de simetría del par cuando el descentrado es el defecto.
    result.derived.kind = DerivedKind::Point;
    result.derived.point = (first->point + second->point) * 0.5F;
    (void)g;  // el ancla solo sirve para agarrar la herramienta con el ratón
    return result;
}

// Orientación respecto a un datum (G3): paralelismo, perpendicularidad y
// angularidad, que son la misma medida con distinto ángulo nominal.
//
// Lo que devuelve es una DISTANCIA, no un ángulo, y esa es la diferencia entre
// medir la cota del plano y medir otra cosa parecida. La zona de tolerancia de
// la norma son dos rectas paralelas ORIENTADAS SEGÚN EL DATUM que tienen que
// contener el elemento; el valor es su separación.
//
// Ojo a la diferencia con la rectitud (G1), que se parece pero no es: allí la
// banda se orienta como quiera, buscando la más estrecha. Aquí la orientación
// la manda el datum y no se puede elegir — por eso una orientación siempre es
// mayor o igual que la rectitud del mismo borde.
ToolRunResult runOrientation(const cv::Mat& gray, const Fixture& fixture,
                             const ToolConfig& config, const OrientationGeometry& g,
                             const Fmt& fmt, const DerivedElements& refs) {
    ToolRunResult result = baseResult(config);

    std::string why;
    const DerivedElement* datum =
        operand(refs, config.reference, OperandKind::Line, "primera", why);
    if (datum == nullptr) {
        // Sin datum no se mide. Una orientación sin decir respecto a qué es el
        // número con nombre de norma que este programa existe para no dar.
        //
        // El motivo se reescribe en el idioma de esta herramienta: `operand`
        // habla de "referencia", que es correcto en general, pero quien pone un
        // paralelismo busca la palabra DATUM y es la que tiene que leer.
        result.detail = config.reference.empty()
                            ? "falta el DATUM: elige en Referencia la herramienta que "
                              "da la recta contra la que se mide"
                            : "no se puede usar '" + config.reference +
                                  "' como DATUM — " + why;
        return result;
    }

    const cv::Point2f p0 = toImg(fixture, g.p0);
    const cv::Point2f p1 = toImg(fixture, g.p1);
    result.overlaySegments.push_back({p0, p1});

    const cv::Point2f delta = p1 - p0;
    const float length = static_cast<float>(cv::norm(delta));
    const int scans = std::clamp(g.scanCount, 5, 400);
    if (length < static_cast<float>(scans)) {
        result.detail = "Tramo demasiado corto para " + std::to_string(scans) + " escaneos";
        return result;
    }
    const cv::Point2f u = delta / length;
    const cv::Point2f n(-u.y, u.x);

    std::vector<cv::Point2f> edgePoints;
    for (int k = 0; k < scans; ++k) {
        const float t = length * static_cast<float>(k) / static_cast<float>(scans - 1);
        const cv::Point2f base = p0 + u * t;
        const auto edges = detectEdges(gray, base - n * (g.scanLength / 2.0F),
                                       base + n * (g.scanLength / 2.0F), 1.0F, 1);
        if (!edges.empty()) {
            edgePoints.push_back(edges[0].point);
            result.overlayPoints.push_back(edges[0].point);
        }
    }
    if (edgePoints.size() < static_cast<std::size_t>(scans) * 6 / 10) {
        result.detail = "Borde no detectado en suficientes escaneos (" +
                        std::to_string(edgePoints.size()) + "/" + std::to_string(scans) + ")";
        return result;
    }

    // El datum viene en coordenadas de PIEZA; se lleva a imagen para medir
    // contra los puntos del borde, que están en imagen.
    const cv::Point2f datumFrom = toImg(fixture, datum->point);
    const cv::Point2f datumTo = toImg(fixture, datum->point + datum->direction);
    cv::Point2f datumDir = datumTo - datumFrom;
    const double datumLength = cv::norm(datumDir);
    if (datumLength < 1e-6) {
        result.detail = "la recta del datum no tiene dirección utilizable";
        return result;
    }
    datumDir /= static_cast<float>(datumLength);

    // La dirección IDEAL del elemento: el datum girado el ángulo nominal. Con 0°
    // es paralelismo, con 90° perpendicularidad, con cualquier otro angularidad.
    const double nominal = static_cast<double>(g.nominalAngleDeg) * kPi / 180.0;
    const cv::Point2f ideal(
        static_cast<float>(datumDir.x * std::cos(nominal) - datumDir.y * std::sin(nominal)),
        static_cast<float>(datumDir.x * std::sin(nominal) + datumDir.y * std::cos(nominal)));
    const cv::Point2f bandNormal(-ideal.y, ideal.x);

    // La anchura de la banda: proyección de los puntos sobre la normal de la
    // dirección ideal. NO se busca la orientación óptima — la manda el datum.
    double lowest = 1e18;
    double highest = -1e18;
    for (const auto& p : edgePoints) {
        const double d = p.dot(bandNormal);
        lowest = std::min(lowest, d);
        highest = std::max(highest, d);
    }
    result.measured = highest - lowest;
    result.ok = withinTolerance(config, result.measured);

    // El ángulo real, INFORMATIVO. Se da porque ayuda a entender de dónde sale
    // la banda, pero no es la medida: un borde puede ir a 0,0° del datum y no
    // caber en la banda por estar ondulado.
    const vision::LineFit actual = vision::fitLineTotal(edgePoints);
    std::string angleNote;
    if (actual.valid) {
        const double dot = std::abs(static_cast<double>(actual.direction.x) * ideal.x +
                                    static_cast<double>(actual.direction.y) * ideal.y);
        const double cross = std::abs(static_cast<double>(actual.direction.x) * ideal.y -
                                      static_cast<double>(actual.direction.y) * ideal.x);
        angleNote = ", desvío angular " + fmt2(std::atan2(cross, dot) * 180.0 / kPi) +
                    "° (informativo, no es la medida)";
    }

    const char* kind = std::abs(g.nominalAngleDeg) < 0.01F          ? "paralelismo"
                       : std::abs(g.nominalAngleDeg - 90.0F) < 0.01F ? "perpendicularidad"
                                                                     : "angularidad";
    result.detail = std::string(kind) + " respecto a '" + config.reference +
                    "' = " + fmtLen(result.measured, fmt) + " de anchura de banda" +
                    angleNote;

    // Las dos rectas de la banda, en la orientación que manda el datum.
    for (const double edge : {lowest, highest}) {
        const cv::Point2f base = bandNormal * static_cast<float>(edge);
        const cv::Point2f centre = base + ideal * ((p0 + p1) * 0.5F).dot(ideal);
        result.overlaySegments.push_back(
            {centre - ideal * (length / 2.0F), centre + ideal * (length / 2.0F)});
    }
    return result;
}

// Redondez por ZONA MÍNIMA (G2): la separación radial entre los dos círculos
// concéntricos más juntos que contienen el perfil, que es como la define la
// norma. Se dan también los números de mínimos cuadrados, porque son los que
// dan casi todas las máquinas de medir y el operador va a comparar.
ToolRunResult runRoundness(const cv::Mat& gray, const Fixture& fixture,
                           const ToolConfig& config, const RoundnessGeometry& g,
                           const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f center = toImg(fixture, g.center);

    const int rays = std::clamp(g.rayCount, 12, 720);
    std::vector<cv::Point2f> points;
    double edgeStrength = 0.0;
    int edgeCount = 0;
    for (int k = 0; k < rays; ++k) {
        const double theta = 2.0 * kPi * k / rays;
        const cv::Point2f dir(static_cast<float>(std::cos(theta)),
                              static_cast<float>(std::sin(theta)));
        const auto edges = detectEdges(gray, center + dir * (g.radius - g.searchBand),
                                       center + dir * (g.radius + g.searchBand), 3.0F, 1);
        if (!edges.empty()) {
            points.push_back(edges[0].point);
            edgeStrength += std::abs(edges[0].strength);
            ++edgeCount;
        }
    }
    if (edgeCount > 0) {
        edgeStrength /= edgeCount;
    }
    if (static_cast<int>(points.size()) < rays * 8 / 10) {
        // Más exigente que el Círculo (que se conforma con el 60 %) y a
        // propósito: un diámetro se puede sacar de medio contorno, pero la
        // redondez es la FORMA. Con un trozo del borde sin ver, el círculo
        // interior se apoya donde le da la gana y el número sale bonito.
        result.detail = "Borde circular insuficiente para juzgar la forma (" +
                        std::to_string(points.size()) + "/" + std::to_string(rays) +
                        " rayos): la redondez necesita ver el contorno entero";
        return result;
    }

    const vision::CircleFit lsq = vision::fitCircleRobust(points);
    if (!lsq.valid) {
        result.detail = "No se pudo ajustar el círculo";
        return result;
    }
    const vision::MinimumZoneCircle zone = vision::minimumZoneCircle(points);
    if (!zone.valid) {
        result.detail = "No se pudo calcular la zona mínima";
        return result;
    }

    // La banda de mínimos cuadrados, para comparar peras con peras: la misma
    // separación radial pero con el centro del ajuste en vez del óptimo.
    double lsqInner = 1e18;
    double lsqOuter = 0.0;
    for (const auto& p : points) {
        const double distance = cv::norm(p - lsq.center);
        lsqInner = std::min(lsqInner, distance);
        lsqOuter = std::max(lsqOuter, distance);
    }

    result.measured = zone.width();
    result.ok = withinTolerance(config, result.measured);
    result.detail = "redondez (zona mínima)=" + fmtLen(zone.width(), fmt) +
                    ", por mínimos cuadrados " + fmtLen(lsqOuter - lsqInner, fmt) +
                    " · Ø=" + fmtLen(2.0 * lsq.radius, fmt) + " · solo vale de frente";

    // El círculo de mínimos cuadrados como referencia: es el centro
    // convencional de un agujero, y el que esperan las demás herramientas.
    result.derived.kind = DerivedKind::Circle;
    result.derived.point = vision::toPieceCoords(fixture, lsq.center);
    result.derived.radius = lsq.radius;

    appendConditionWarnings(result.detail, fmt, edgeStrength);
    result.overlayPoints = points;
    result.overlayPoints.push_back(zone.center);
    return result;
}

// Rectitud por ZONA MÍNIMA (G1): el valor con el que la norma define la
// rectitud, y que no es el que da el Borde liso.
ToolRunResult runStraightness(const cv::Mat& gray, const Fixture& fixture,
                              const ToolConfig& config, const StraightnessGeometry& g,
                              const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f p0 = toImg(fixture, g.p0);
    const cv::Point2f p1 = toImg(fixture, g.p1);
    result.overlaySegments.push_back({p0, p1});

    const cv::Point2f delta = p1 - p0;
    const float length = static_cast<float>(cv::norm(delta));
    const int scans = std::clamp(g.scanCount, 5, 400);
    if (length < static_cast<float>(scans)) {
        result.detail = "Tramo demasiado corto para " + std::to_string(scans) + " escaneos";
        return result;
    }
    const cv::Point2f u = delta / length;
    const cv::Point2f n(-u.y, u.x);

    std::vector<cv::Point2f> edgePoints;
    edgePoints.reserve(static_cast<std::size_t>(scans));
    int gap = 0;
    int longestGap = 0;
    for (int k = 0; k < scans; ++k) {
        const float t = length * static_cast<float>(k) / static_cast<float>(scans - 1);
        const cv::Point2f base = p0 + u * t;
        const auto edges = detectEdges(gray, base - n * (g.scanLength / 2.0F),
                                       base + n * (g.scanLength / 2.0F), 1.0F, 1);
        if (edges.empty()) {
            ++gap;
            longestGap = std::max(longestGap, gap);
            continue;
        }
        gap = 0;
        edgePoints.push_back(edges[0].point);
        result.overlayPoints.push_back(edges[0].point);
    }
    if (edgePoints.size() < static_cast<std::size_t>(scans) * 6 / 10) {
        result.detail = "Borde no detectado en suficientes escaneos (" +
                        std::to_string(edgePoints.size()) + "/" + std::to_string(scans) + ")";
        return result;
    }
    // Y el mismo hueco seguido que se le escapaba al Borde liso. Aquí duele
    // igual o más: la rectitud por zona mínima es un valor de plano, y darlo por
    // bueno sobre un borde que no se ha visto entero es firmar una cota que
    // nadie ha medido. Medido: sobre una mella de 26 px con el largo en 16
    // devolvía 0,000.
    const double step = static_cast<double>(length) / std::max(1, scans - 1);
    if (blindStretchMatters(longestGap, step, static_cast<double>(length))) {
        result.detail = "No se pudo ver el borde en un tramo de " +
                        fmtLen(longestGap * step, fmt) +
                        ": sube el largo de escaneo (ahora " +
                        fmtLen(static_cast<double>(g.scanLength), fmt) +
                        "). Con un tramo sin ver, la banda mínima se calcula sobre el borde "
                        "que sí se vio y sale más recta de lo que la pieza es";
        return result;
    }

    const vision::MinimumZone zone = vision::minimumZoneBand(edgePoints);
    if (!zone.valid) {
        result.detail = "No se pudo calcular la banda mínima del borde";
        return result;
    }
    result.measured = zone.width;
    result.ok = withinTolerance(config, result.measured);

    // La misma banda, pero orientada según la recta de mínimos cuadrados. Se da
    // para que el número de la norma se pueda comparar con el de siempre: la
    // banda mínima NUNCA puede ser mayor, porque la de mínimos cuadrados es una
    // candidata más entre todas las orientaciones.
    const vision::LineFit lsq = vision::fitLineTotal(edgePoints);
    std::string comparison;
    if (lsq.valid) {
        double lowest = 1e18;
        double highest = -1e18;
        for (const auto& p : edgePoints) {
            const double d = lsq.signedDistance(p);
            lowest = std::min(lowest, d);
            highest = std::max(highest, d);
        }
        comparison = ", banda por mínimos cuadrados " + fmtLen(highest - lowest, fmt);
    }

    result.detail = "rectitud (zona mínima)=" + fmtLen(zone.width, fmt) + comparison + " · " +
                    std::to_string(edgePoints.size()) + " puntos · proyectada en el plano " +
                    "de la imagen";

    // Las dos rectas de la banda, dibujadas: sin verlas, un número de rectitud
    // no se puede comprobar a ojo.
    const cv::Point2f bandNormal(-zone.direction.y, zone.direction.x);
    const float half = static_cast<float>(zone.width / 2.0);
    for (const float side : {-1.0F, 1.0F}) {
        const cv::Point2f centre = zone.point + bandNormal * (half * side);
        result.overlaySegments.push_back(
            {centre - zone.direction * (length / 2.0F),
             centre + zone.direction * (length / 2.0F)});
    }
    return result;
}

// Holgura: la separación más corta entre dos figuras (L1).
//
// `pointPolygonTest` con `measureDist` da la distancia con signo de un punto a
// un contorno: positiva dentro, negativa fuera. Recorriendo los puntos de una
// figura contra la otra sale el mínimo y, con él, DÓNDE está — que es la mitad
// del valor de esta herramienta: un mínimo que no se puede señalar en el lienzo
// no se puede verificar a ojo.
ToolRunResult runClearance(const cv::Mat& gray, const Fixture& fixture,
                           const ToolConfig& config, const ClearanceGeometry& g,
                           const Fmt& fmt) {
    ToolRunResult result = baseResult(config);

    const float hw = g.width / 2.0F;
    const float hh = g.height / 2.0F;
    const std::vector<cv::Point2f> quad{
        toImg(fixture, g.center + cv::Point2f(-hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, hh)),
        toImg(fixture, g.center + cv::Point2f(-hw, hh))};
    for (std::size_t i = 0; i < quad.size(); ++i) {
        result.overlaySegments.push_back({quad[i], quad[(i + 1) % quad.size()]});
    }

    std::vector<cv::Point> quadInt;
    quadInt.reserve(quad.size());
    for (const auto& p : quad) {
        quadInt.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    const cv::Rect bounds = cv::boundingRect(quadInt) & cv::Rect(0, 0, gray.cols, gray.rows);
    if (bounds.area() < 25) {
        result.detail = "La región cae fuera de la imagen";
        return result;
    }

    cv::Mat regionMask = cv::Mat::zeros(bounds.size(), CV_8UC1);
    std::vector<cv::Point> quadLocal;
    quadLocal.reserve(quadInt.size());
    for (const auto& p : quadInt) {
        quadLocal.emplace_back(p.x - bounds.x, p.y - bounds.y);
    }
    cv::fillPoly(regionMask, std::vector<std::vector<cv::Point>>{quadLocal}, cv::Scalar(255));

    cv::Mat binary;
    cv::threshold(gray(bounds), binary, 0.0, 255.0,
                  (g.darkPiece ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);
    cv::bitwise_and(binary, regionMask, binary);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    // Se descartan las motas: dos píxeles sueltos harían de "segunda figura" y
    // la holgura medida sería contra el ruido.
    constexpr double kMinShapeArea = 20.0;
    std::vector<std::vector<cv::Point>> shapes;
    for (auto& contour : contours) {
        if (cv::contourArea(contour) >= kMinShapeArea) {
            shapes.push_back(std::move(contour));
        }
    }
    if (shapes.size() < 2) {
        if (shapes.size() == 1) {
            // No es solo "falta una figura": si las dos se tocan, la
            // binarización las une y aquí llega UNA. Decirlo convierte la
            // limitación en el dato que el operador necesita.
            result.detail =
                "Solo se ve una figura: o el recuadro no abarca las dos, o se están "
                "TOCANDO — dos piezas en contacto se leen como una sola silueta";
        } else {
            result.detail = "No se ve ninguna figura dentro del recuadro";
        }
        return result;
    }
    std::sort(shapes.begin(), shapes.end(), [](const auto& a, const auto& b) {
        return cv::contourArea(a) > cv::contourArea(b);
    });

    // Se mide entre las DOS MAYORES. Con más figuras dentro, las pequeñas se
    // ignoran y el detalle dice cuántas había, para que el operador sepa que su
    // recuadro abarca más de lo que cree.
    const auto& first = shapes[0];
    const auto& second = shapes[1];

    // Distancia con signo de cada punto de la primera a la segunda: negativa
    // fuera, y el máximo (el menos negativo) es lo más cerca que están.
    //
    // Nota de honestidad: el plan pedía además que dos figuras SOLAPADAS dieran
    // una medida negativa de interferencia. No se puede, y no por falta de
    // ganas: dos contornos externos de una misma binarización jamás se solapan
    // — en cuanto dos piezas se tocan, la silueta las une en una sola y aquí
    // llega UNA figura, no dos. Se escribió la rama de interferencia y resultó
    // ser inalcanzable, así que se quitó en vez de dejarla de adorno. Lo que
    // queda es el caso de una sola figura, que se explica arriba diciendo que
    // pueden estar tocándose. Medir cuánto se solapan dos piezas es una medida
    // que una silueta 2D no contiene.
    double best = -1e18;
    cv::Point bestPoint = first.front();
    for (const auto& point : first) {
        const double signed_ = cv::pointPolygonTest(second, cv::Point2f(point), true);
        if (signed_ > best) {
            best = signed_;
            bestPoint = point;
        }
    }

    // El punto de la otra figura donde ocurre, para poder dibujar el segmento.
    // Sin los dos extremos la medida no se puede comprobar a ojo.
    cv::Point partner = second.front();
    double closest = 1e18;
    for (const auto& point : second) {
        const double distance = cv::norm(cv::Point2f(point) - cv::Point2f(bestPoint));
        if (distance < closest) {
            closest = distance;
            partner = point;
        }
    }

    const cv::Point2f offset(static_cast<float>(bounds.x), static_cast<float>(bounds.y));
    const cv::Point2f a = cv::Point2f(bestPoint) + offset;
    const cv::Point2f b = cv::Point2f(partner) + offset;
    result.overlayPoints.push_back(a);
    result.overlayPoints.push_back(b);
    result.overlaySegments.push_back({a, b});

    const std::string extra =
        shapes.size() > 2 ? " (" + std::to_string(shapes.size()) +
                                " figuras en el recuadro; se miden las dos mayores)"
                          : "";
    result.measured = -best;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "holgura mínima=" + fmtLenPts(a, b, fmt) + extra;
    return result;
}

// Rebabas y mellas (F4): los defectos de un borde, contados y medidos UNO A UNO.
//
// El Borde liso da la desviación máxima, y con eso un borde con una mella de
// 0,5 mm y otro con veinte de 0,1 mm se leen igual. No son la misma pieza: la
// primera se rectifica, la segunda se tira.
ToolRunResult runEdgeDefects(const cv::Mat& gray, const Fixture& fixture,
                             const ToolConfig& config, const EdgeDefectsGeometry& g,
                             const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f p0 = toImg(fixture, g.p0);
    const cv::Point2f p1 = toImg(fixture, g.p1);
    result.overlaySegments.push_back({p0, p1});

    const cv::Point2f delta = p1 - p0;
    const float length = static_cast<float>(cv::norm(delta));
    const int scans = std::clamp(g.scanCount, 8, 400);
    if (length < static_cast<float>(scans)) {
        result.detail = "Tramo demasiado corto para " + std::to_string(scans) + " escaneos";
        return result;
    }
    const cv::Point2f u = delta / length;
    const cv::Point2f n(-u.y, u.x);
    const double step = static_cast<double>(length) / (scans - 1);

    std::vector<double> ts;
    std::vector<double> offsets;
    std::vector<cv::Point2f> points;
    // Escaneos SEGUIDOS sin borde. No es lo mismo que el total de fallos: unos
    // cuantos sueltos son ruido, pero un tramo entero sin ver el borde es un
    // punto ciego, y casi siempre significa que el defecto se sale de la
    // ventana de escaneo.
    int longestGap = 0;
    int currentGap = 0;
    for (int k = 0; k < scans; ++k) {
        const float t = static_cast<float>(step * k);
        const cv::Point2f base = p0 + u * t;
        const cv::Point2f from = base - n * (g.scanLength / 2.0F);
        const cv::Point2f to = base + n * (g.scanLength / 2.0F);
        const auto edges = detectEdges(gray, from, to, 1.0F, 1);
        if (edges.empty()) {
            longestGap = std::max(longestGap, ++currentGap);
            continue;
        }
        currentGap = 0;
        ts.push_back(static_cast<double>(t));
        offsets.push_back(edges[0].position - static_cast<double>(g.scanLength) / 2.0);
        points.push_back(edges[0].point);
    }
    if (ts.size() < static_cast<std::size_t>(scans) * 6 / 10) {
        result.detail = "Borde no detectado en suficientes escaneos (" +
                        std::to_string(ts.size()) + "/" + std::to_string(scans) + ")";
        return result;
    }
    // Un hueco seguido NO se puede reportar como "sin defectos". Es el fallo más
    // peligroso que puede tener esta herramienta: una rebaba más alta que media
    // ventana de escaneo hace que esos escaneos no encuentren borde, y con los
    // huecos ignorados la herramienta daría por limpio justo el tramo donde está
    // el defecto GORDO.
    //
    // El corte iba en «tres escaneos seguidos», y eso significaba cosas
    // distintas según lo fino que se muestreara: subir la resolución hacía
    // saltar el aviso y bajarla lo silenciaba, al revés de lo que debe pasar.
    // Ahora lo decide cuánto BORDE quedó sin ver, que es lo que de verdad
    // importa. La misma regla en las tres herramientas de borde.
    if (blindStretchMatters(longestGap, step, static_cast<double>(length))) {
        result.detail = "No se pudo ver el borde en un tramo de " +
                        fmtLen(longestGap * step, fmt) +
                        ": sube el largo de escaneo (ahora " +
                        fmtLen(static_cast<double>(g.scanLength), fmt) +
                        "). Un defecto más alto que media ventana se sale de ella, y "
                        "dar el borde por limpio ahí sería el peor error posible";
        return result;
    }

    // Recta base AJUSTADA ROBUSTAMENTE, no por mínimos cuadrados. La diferencia
    // importa justo aquí: una rebaba grande arrastra el ajuste clásico y
    // reparte su altura entre ella y el resto del borde, así que el defecto sale
    // más pequeño de lo que es y el borde sano parece torcido. La reponderación
    // de Tukey deja los defectos fuera del ajuste, que es donde tienen que
    // estar — son justo los atípicos que se buscan.
    std::vector<cv::Point2f> profile;
    profile.reserve(ts.size());
    for (std::size_t i = 0; i < ts.size(); ++i) {
        profile.emplace_back(static_cast<float>(ts[i]), static_cast<float>(offsets[i]));
    }
    const vision::LineFit base = vision::fitLineRobust(profile);
    if (!base.valid) {
        result.detail = "No se pudo ajustar la recta base del borde";
        return result;
    }
    const double slope = std::abs(base.direction.x) > 1e-6
                             ? static_cast<double>(base.direction.y) / base.direction.x
                             : 0.0;

    // De qué lado está el material. Sin esto no se puede distinguir una rebaba
    // de una mella: el signo del residuo depende de hacia dónde trazó el
    // operador la línea, no de la pieza. Se mira el gris a los dos lados del
    // borde y se decide con la imagen, no con una suposición.
    const cv::Point2f middle = p0 + u * (length / 2.0F);
    const auto graySample = [&gray](const cv::Point2f& p) -> double {
        const int x = std::clamp(cvRound(p.x), 0, gray.cols - 1);
        const int y = std::clamp(cvRound(p.y), 0, gray.rows - 1);
        return gray.at<unsigned char>(y, x);
    };
    const double towardPositive = graySample(middle + n * (g.scanLength * 0.45F));
    const double towardNegative = graySample(middle - n * (g.scanLength * 0.45F));
    // Si la pieza es lo oscuro, el material está del lado más oscuro.
    const bool materialTowardPositive =
        g.darkPiece ? towardPositive < towardNegative : towardPositive > towardNegative;
    // Un residuo que se aleja del material es material de MÁS sobresaliendo:
    // rebaba. Uno que entra hacia el material es material de MENOS: mella.
    const double burrSign = materialTowardPositive ? -1.0 : 1.0;

    // Agrupación por conectividad: muestras seguidas por encima del umbral y
    // con el mismo signo son UN defecto.
    struct Defect {
        double peak = 0.0;    // altura con signo (+ rebaba, − mella)
        double from = 0.0;    // extensión a lo largo del borde
        double to = 0.0;
        cv::Point2f at{0.0F, 0.0F};
    };
    std::vector<Defect> defects;
    const double threshold = std::max(0.05, static_cast<double>(g.minHeight));
    bool inDefect = false;
    for (std::size_t i = 0; i < ts.size(); ++i) {
        const double residual =
            offsets[i] - (base.point.y + (ts[i] - base.point.x) * slope);
        const double signedHeight = residual * burrSign;
        const bool over = std::abs(residual) >= threshold;
        const bool sameSign =
            inDefect && !defects.empty() && (signedHeight > 0.0) == (defects.back().peak > 0.0);
        if (over && inDefect && sameSign) {
            Defect& current = defects.back();
            current.to = ts[i];
            if (std::abs(signedHeight) > std::abs(current.peak)) {
                current.peak = signedHeight;
                current.at = points[i];
            }
        } else if (over) {
            Defect fresh;
            fresh.peak = signedHeight;
            fresh.from = ts[i];
            fresh.to = ts[i];
            fresh.at = points[i];
            defects.push_back(fresh);
            inDefect = true;
        } else {
            inDefect = false;
        }
    }

    result.measured = static_cast<double>(defects.size());
    result.ok = withinTolerance(config, result.measured);

    if (defects.empty()) {
        result.detail = "sin defectos por encima de " + fmtLen(threshold, fmt) + " (" +
                        std::to_string(ts.size()) + " escaneos)";
        return result;
    }

    // El mayor primero: es el que decide si la pieza se rectifica o se tira.
    std::sort(defects.begin(), defects.end(), [](const Defect& a, const Defect& b) {
        return std::abs(a.peak) > std::abs(b.peak);
    });
    std::string detail = std::to_string(defects.size()) + " defecto(s):";
    constexpr std::size_t kListed = 4;
    for (std::size_t i = 0; i < std::min(defects.size(), kListed); ++i) {
        const Defect& d = defects[i];
        // La extensión de un defecto de una sola muestra no es cero: ocupa al
        // menos el paso entre escaneos, que es lo que se pudo resolver.
        const double extent = std::max(d.to - d.from, step);
        detail += (i == 0 ? " " : ", ");
        detail += std::string(d.peak > 0.0 ? "rebaba " : "mella ") +
                  fmtLen(std::abs(d.peak), fmt) + "×" + fmtLen(extent, fmt);
        result.overlayPoints.push_back(d.at);
    }
    if (defects.size() > kListed) {
        detail += " y " + std::to_string(defects.size() - kListed) + " más";
    }
    result.detail = detail;
    return result;
}

// Lados de un perfil poligonal (F3).
//
// `approxPolyDP` hace el trabajo; lo que hay que hacer bien es DECIDIR SI EL
// NÚMERO QUE DEVUELVE SIGNIFICA ALGO. Sobre un polígono de verdad el recuento
// aguanta aunque se cambie la tolerancia; sobre una curva, cada tolerancia da
// un número distinto. Esa estabilidad es el criterio: se aproxima con epsilon,
// con la mitad y con el doble, y solo se da el recuento si los tres coinciden.
// Es también la razón por la que el plan pedía "un círculo no se da por
// polígono" — y sale gratis, sin ningún umbral de curvatura inventado.
ToolRunResult runPolygon(const cv::Mat& gray, const Fixture& fixture,
                         const ToolConfig& config, const PolygonGeometry& g,
                         const Fmt& fmt) {
    ToolRunResult result = baseResult(config);

    const float hw = g.width / 2.0F;
    const float hh = g.height / 2.0F;
    const std::vector<cv::Point2f> quad{
        toImg(fixture, g.center + cv::Point2f(-hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, hh)),
        toImg(fixture, g.center + cv::Point2f(-hw, hh))};
    for (std::size_t i = 0; i < quad.size(); ++i) {
        result.overlaySegments.push_back({quad[i], quad[(i + 1) % quad.size()]});
    }

    std::vector<cv::Point> quadInt;
    quadInt.reserve(quad.size());
    for (const auto& p : quad) {
        quadInt.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    const cv::Rect bounds = cv::boundingRect(quadInt) & cv::Rect(0, 0, gray.cols, gray.rows);
    if (bounds.area() < 25) {
        result.detail = "La región cae fuera de la imagen";
        return result;
    }

    cv::Mat regionMask = cv::Mat::zeros(bounds.size(), CV_8UC1);
    std::vector<cv::Point> quadLocal;
    quadLocal.reserve(quadInt.size());
    for (const auto& p : quadInt) {
        quadLocal.emplace_back(p.x - bounds.x, p.y - bounds.y);
    }
    cv::fillPoly(regionMask, std::vector<std::vector<cv::Point>>{quadLocal}, cv::Scalar(255));

    cv::Mat binary;
    cv::threshold(gray(bounds), binary, 0.0, 255.0,
                  (g.darkPiece ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);
    cv::bitwise_and(binary, regionMask, binary);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        result.detail = "No se ve ninguna figura dentro del recuadro";
        return result;
    }
    const auto& outer = *std::max_element(
        contours.begin(), contours.end(), [](const auto& a, const auto& b) {
            return cv::contourArea(a) < cv::contourArea(b);
        });
    if (cv::contourArea(outer) < 25.0) {
        result.detail = "No se ve ninguna figura dentro del recuadro";
        return result;
    }

    const double perimeter = vision::digitalPerimeter(outer);
    const double epsilon = std::max(1.0, static_cast<double>(g.epsilonFraction) * perimeter);
    const auto sidesAt = [&outer](double eps) {
        std::vector<cv::Point> approx;
        cv::approxPolyDP(outer, approx, eps, true);
        return approx;
    };

    const std::vector<cv::Point> approx = sidesAt(epsilon);
    const int sides = static_cast<int>(approx.size());
    if (sides < 3) {
        result.detail = "Con este epsilon la figura se queda en " + std::to_string(sides) +
                        " vértices: bájalo";
        return result;
    }

    // La comprobación de estabilidad: mitad y doble del epsilon elegido.
    const int sidesHalf = static_cast<int>(sidesAt(epsilon * 0.5).size());
    const int sidesDouble = static_cast<int>(sidesAt(epsilon * 2.0).size());
    result.measured = sides;
    if (sidesHalf != sides || sidesDouble != sides) {
        result.detail = "No es un polígono claro: " + std::to_string(sides) + " lados con " +
                        "este epsilon, " + std::to_string(sidesHalf) + " con la mitad y " +
                        std::to_string(sidesDouble) + " con el doble. Un recuento que " +
                        "cambia con la tolerancia no dice nada de la pieza";
        return result;
    }

    result.ok = withinTolerance(config, result.measured);

    // Longitudes de los lados y ángulos interiores.
    double shortest = 1e9;
    double longest = 0.0;
    double minAngle = 360.0;
    double maxAngle = 0.0;
    for (std::size_t i = 0; i < approx.size(); ++i) {
        const cv::Point2f current(approx[i]);
        const cv::Point2f next(approx[(i + 1) % approx.size()]);
        const cv::Point2f previous(approx[(i + approx.size() - 1) % approx.size()]);
        const double side = cv::norm(next - current);
        shortest = std::min(shortest, side);
        longest = std::max(longest, side);

        const cv::Point2f toPrev = previous - current;
        const cv::Point2f toNext = next - current;
        const double lenA = cv::norm(toPrev);
        const double lenB = cv::norm(toNext);
        if (lenA > 1e-6 && lenB > 1e-6) {
            const double cosine =
                std::clamp((toPrev.dot(toNext)) / (lenA * lenB), -1.0, 1.0);
            const double angle = std::acos(cosine) * 180.0 / kPi;
            minAngle = std::min(minAngle, angle);
            maxAngle = std::max(maxAngle, angle);
        }
        result.overlaySegments.push_back(
            {current + cv::Point2f(static_cast<float>(bounds.x), static_cast<float>(bounds.y)),
             next + cv::Point2f(static_cast<float>(bounds.x), static_cast<float>(bounds.y))});
        result.overlayPoints.push_back(
            current + cv::Point2f(static_cast<float>(bounds.x), static_cast<float>(bounds.y)));
    }

    result.detail = std::to_string(sides) + " lados · lado " + fmtLen(shortest, fmt) + " a " +
                    fmtLen(longest, fmt) + " · ángulo interior " + fmt2(minAngle) + "° a " +
                    fmt2(maxAngle) + "°";
    return result;
}

// Simetría de la silueta (F2). Un DESCRIPTOR DE FORMA, no una tolerancia GD&T:
// la simetría de la norma se retiró en ASME Y14.5-2018, y darla con ese nombre
// sería vender como cota algo que ya no lo es.
//
// El método: reflejar la máscara respecto a una recta que pasa por su centroide
// y medir cuánto se solapa con la original (IoU). El mejor ángulo es el eje de
// simetría, y su IoU es el grado.

// Solape de una máscara consigo misma reflejada respecto a la recta que pasa por
// `centre` con el ángulo dado. 1 = se superponen exactamente.
double symmetryOverlap(const cv::Mat& mask, const cv::Point2f& centre, double angleRad) {
    // Reflexión respecto a una recta por el origen con ángulo θ:
    //   [ cos2θ   sin2θ ]
    //   [ sin2θ  -cos2θ ]
    // y luego se traslada para que el centroide quede fijo.
    const double c = std::cos(2.0 * angleRad);
    const double s = std::sin(2.0 * angleRad);
    cv::Mat m = (cv::Mat_<double>(2, 3) << c, s, centre.x - c * centre.x - s * centre.y, s,
                 -c, centre.y - s * centre.x + c * centre.y);

    cv::Mat mirrored;
    // INTER_NEAREST porque la máscara es binaria: interpolar crearía valores
    // intermedios que luego habría que volver a umbralizar, y eso engorda o
    // adelgaza la figura según el ángulo — justo el sesgo que se está midiendo.
    cv::warpAffine(mask, mirrored, m, mask.size(), cv::INTER_NEAREST, cv::BORDER_CONSTANT,
                   cv::Scalar(0));

    cv::Mat inter;
    cv::Mat uni;
    cv::bitwise_and(mask, mirrored, inter);
    cv::bitwise_or(mask, mirrored, uni);
    const double unionArea = cv::countNonZero(uni);
    if (unionArea <= 0.0) {
        return 0.0;
    }
    return cv::countNonZero(inter) / unionArea;
}

ToolRunResult runSymmetry(const cv::Mat& gray, const Fixture& fixture,
                          const ToolConfig& config, const SymmetryGeometry& g,
                          const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    (void)fmt;  // el grado no tiene unidades y el ángulo va en grados

    const float hw = g.width / 2.0F;
    const float hh = g.height / 2.0F;
    const std::vector<cv::Point2f> quad{
        toImg(fixture, g.center + cv::Point2f(-hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, hh)),
        toImg(fixture, g.center + cv::Point2f(-hw, hh))};
    for (std::size_t i = 0; i < quad.size(); ++i) {
        result.overlaySegments.push_back({quad[i], quad[(i + 1) % quad.size()]});
    }

    std::vector<cv::Point> quadInt;
    quadInt.reserve(quad.size());
    for (const auto& p : quad) {
        quadInt.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    // Se deja un margen alrededor del recuadro: al reflejar, parte de la figura
    // cae fuera de su propia caja, y si el lienzo la recortara ese trozo
    // contaría como asimetría cuando solo es falta de sitio.
    constexpr int kPad = 8;
    cv::Rect bounds = cv::boundingRect(quadInt);
    bounds -= cv::Point(kPad, kPad);
    bounds += cv::Size(2 * kPad, 2 * kPad);
    bounds &= cv::Rect(0, 0, gray.cols, gray.rows);
    if (bounds.area() < 100) {
        result.detail = "La región cae fuera de la imagen";
        return result;
    }

    cv::Mat regionMask = cv::Mat::zeros(bounds.size(), CV_8UC1);
    std::vector<cv::Point> quadLocal;
    quadLocal.reserve(quadInt.size());
    for (const auto& p : quadInt) {
        quadLocal.emplace_back(p.x - bounds.x, p.y - bounds.y);
    }
    cv::fillPoly(regionMask, std::vector<std::vector<cv::Point>>{quadLocal}, cv::Scalar(255));

    cv::Mat binary;
    cv::threshold(gray(bounds), binary, 0.0, 255.0,
                  (g.darkPiece ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);
    cv::bitwise_and(binary, regionMask, binary);

    const cv::Moments moments = cv::moments(binary, true);
    if (moments.m00 < 25.0) {
        result.detail = "No se ve ninguna figura dentro del recuadro";
        return result;
    }
    const cv::Point2f centroid(static_cast<float>(moments.m10 / moments.m00),
                               static_cast<float>(moments.m01 / moments.m00));

    // Barrido grueso por toda la media vuelta y afinado alrededor del mejor.
    //
    // Se barre entero en vez de sembrar con el eje principal de inercia. El eje
    // de simetría de una figura simétrica ES un eje principal, así que sembrar
    // sería correcto... salvo cuando la nube es casi redonda, que es cuando el
    // eje principal es ruido — y esa es justo la figura en la que uno querría
    // fiarse del resultado. Barrer entero quita el caso especial.
    //
    // El barrido grueso va sobre una copia REDUCIDA, y no es un atajo gratuito:
    // barrer 60 ángulos a resolución completa costaba ~26 ms por frame, que en
    // la vista en vivo es un tercio del presupuesto. Reducir a 160 px de lado
    // baja eso a unos pocos ms, y no afecta al resultado porque el grueso solo
    // tiene que acertar el TRAMO de 3°: el ángulo final y el grado que se
    // publica salen del afinado, que va a resolución completa.
    constexpr double kDegToRad = kPi / 180.0;
    constexpr int kCoarseSide = 160;
    cv::Mat coarse = binary;
    cv::Point2f coarseCentroid = centroid;
    const int longestSide = std::max(binary.cols, binary.rows);
    if (longestSide > kCoarseSide) {
        const double factor = static_cast<double>(kCoarseSide) / longestSide;
        cv::resize(binary, coarse, cv::Size(), factor, factor, cv::INTER_NEAREST);
        coarseCentroid = centroid * static_cast<float>(factor);
    }

    double bestAngleDeg = 0.0;
    double bestScore = -1.0;
    for (double angle = 0.0; angle < 180.0; angle += 3.0) {
        const double score = symmetryOverlap(coarse, coarseCentroid, angle * kDegToRad);
        if (score > bestScore) {
            bestScore = score;
            bestAngleDeg = angle;
        }
    }
    // El afinado, a resolución completa: el grado que se publica no puede salir
    // de una imagen reducida, porque perder píxeles del borde INFLA la simetría
    // —los detalles pequeños que la rompen son los primeros en desaparecer—.
    bestScore = -1.0;
    const double coarseBest = bestAngleDeg;
    for (double delta = -3.0; delta <= 3.0; delta += 0.25) {
        const double angle = coarseBest + delta;
        const double score = symmetryOverlap(binary, centroid, angle * kDegToRad);
        if (score > bestScore) {
            bestScore = score;
            bestAngleDeg = angle;
        }
    }
    // El ángulo de una recta vive en [0,180).
    bestAngleDeg = std::fmod(bestAngleDeg + 180.0, 180.0);

    // El eje perpendicular al mejor: es lo que distingue un rectángulo —simétrico
    // en dos ejes— de un triángulo isósceles, que solo lo es en uno.
    const double crossScore =
        symmetryOverlap(binary, centroid, (bestAngleDeg + 90.0) * kDegToRad);

    result.measured = bestScore;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "grado=" + fmt2(bestScore) + " en un eje a " + fmt2(bestAngleDeg) +
                    "°, y " + fmt2(crossScore) + " en el perpendicular";

    // El eje encontrado se ofrece como referencia: el eje de simetría de una
    // pieza es un datum tan legítimo como su eje medio.
    const cv::Point2f centreImage(centroid.x + bounds.x, centroid.y + bounds.y);
    const cv::Point2f direction(static_cast<float>(std::cos(bestAngleDeg * kDegToRad)),
                                static_cast<float>(std::sin(bestAngleDeg * kDegToRad)));
    const cv::Point2f piecePoint = vision::toPieceCoords(fixture, centreImage);
    const cv::Point2f pieceAlong = vision::toPieceCoords(fixture, centreImage + direction);
    result.derived.kind = DerivedKind::Line;
    result.derived.point = piecePoint;
    result.derived.direction = pieceAlong - piecePoint;

    const float half = std::max(hw, hh);
    result.overlaySegments.push_back(
        {centreImage - half * direction, centreImage + half * direction});
    result.overlayPoints.push_back(centreImage);
    return result;
}

// Región (F1): los descriptores de forma de la silueta que hay dentro del
// recuadro. Una sola herramienta con selector de medida, porque cada instancia
// lleva su tolerancia y así el operador pone solo las que le importan.
ToolRunResult runRegion(const cv::Mat& gray, const Fixture& fixture, const ToolConfig& config,
                        const RegionGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);

    const float hw = g.width / 2.0F;
    const float hh = g.height / 2.0F;
    const std::vector<cv::Point2f> quad{
        toImg(fixture, g.center + cv::Point2f(-hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, hh)),
        toImg(fixture, g.center + cv::Point2f(-hw, hh))};
    for (std::size_t i = 0; i < quad.size(); ++i) {
        result.overlaySegments.push_back({quad[i], quad[(i + 1) % quad.size()]});
    }

    std::vector<cv::Point> quadInt;
    quadInt.reserve(quad.size());
    for (const auto& p : quad) {
        quadInt.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    const cv::Rect bounds = cv::boundingRect(quadInt) & cv::Rect(0, 0, gray.cols, gray.rows);
    if (bounds.area() < 25) {
        result.detail = "La región cae fuera de la imagen";
        return result;
    }

    cv::Mat regionMask = cv::Mat::zeros(bounds.size(), CV_8UC1);
    std::vector<cv::Point> quadLocal;
    quadLocal.reserve(quadInt.size());
    for (const auto& p : quadInt) {
        quadLocal.emplace_back(p.x - bounds.x, p.y - bounds.y);
    }
    cv::fillPoly(regionMask, std::vector<std::vector<cv::Point>>{quadLocal}, cv::Scalar(255));

    cv::Mat binary;
    cv::threshold(gray(bounds), binary, 0.0, 255.0,
                  (g.darkPiece ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);
    cv::bitwise_and(binary, regionMask, binary);

    // CCOMP da exterior e hijos en dos niveles, que es justo lo que hace falta
    // para contar agujeros. Y NONE —no SIMPLE— porque `digitalPerimeter`
    // necesita la cadena de píxeles completa: con el contorno aproximado el
    // conteo de pasos no significa nada.
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binary, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        result.detail = "No se ve ninguna figura dentro del recuadro";
        return result;
    }

    // La figura es la mayor de las EXTERIORES (las que no tienen padre).
    int best = -1;
    double bestArea = 0.0;
    for (int i = 0; i < static_cast<int>(contours.size()); ++i) {
        if (hierarchy[static_cast<std::size_t>(i)][3] >= 0) {
            continue;  // es un agujero de otra
        }
        const double area = cv::contourArea(contours[static_cast<std::size_t>(i)]);
        if (area > bestArea) {
            bestArea = area;
            best = i;
        }
    }
    if (best < 0 || bestArea < 9.0) {
        result.detail = "No se ve ninguna figura dentro del recuadro";
        return result;
    }
    const auto& outer = contours[static_cast<std::size_t>(best)];

    // Los agujeros son los hijos directos. Se descartan los de pocos píxeles:
    // el ruido de la binarización deja motas de dos o tres, y contarlas como
    // agujeros haría inútil la medida.
    constexpr double kMinHoleArea = 12.0;
    double holeArea = 0.0;
    int holes = 0;
    for (int i = hierarchy[static_cast<std::size_t>(best)][2]; i >= 0;
         i = hierarchy[static_cast<std::size_t>(i)][0]) {
        const double area = cv::contourArea(contours[static_cast<std::size_t>(i)]);
        if (area >= kMinHoleArea) {
            ++holes;
            holeArea += area;
        }
    }

    const double outerArea = bestArea;
    const double netArea = std::max(0.0, outerArea - holeArea);
    const double perimeter = vision::digitalPerimeter(outer);
    std::vector<cv::Point> hull;
    cv::convexHull(outer, hull);
    const double hullArea = cv::contourArea(hull);
    const double solidity = hullArea > 0.0 ? outerArea / hullArea : 0.0;
    const double circularity =
        perimeter > 0.0 ? 4.0 * kPi * outerArea / (perimeter * perimeter) : 0.0;
    const cv::RotatedRect box = cv::minAreaRect(outer);
    const double longSide = std::max(box.size.width, box.size.height);
    const double shortSide = std::min(box.size.width, box.size.height);
    const double aspect = shortSide > 1e-6 ? longSide / shortSide : 0.0;

    switch (g.measure) {
        case RegionMeasure::Area: result.measured = netArea; break;
        case RegionMeasure::Perimeter: result.measured = perimeter; break;
        case RegionMeasure::Solidity: result.measured = solidity; break;
        case RegionMeasure::Circularity: result.measured = circularity; break;
        case RegionMeasure::AspectRatio: result.measured = aspect; break;
        case RegionMeasure::HoleCount: result.measured = holes; break;
    }
    result.ok = withinTolerance(config, result.measured);

    // Se enseñan TODAS aunque solo una lleve tolerancia: calcularlas ya está
    // hecho, y quien está decidiendo qué vigilar necesita verlas juntas.
    result.detail = std::string(regionMeasureLabel(g.measure)) + " · área=" +
                    fmtArea(netArea, fmt) + ", perímetro=" + fmtLen(perimeter, fmt) +
                    ", solidez=" + fmt2(solidity) + ", circularidad=" + fmt2(circularity) +
                    ", aspecto=" + fmt2(aspect) + ", agujeros=" + std::to_string(holes);

    for (const auto& p : outer) {
        result.overlayPoints.emplace_back(static_cast<float>(p.x + bounds.x),
                                          static_cast<float>(p.y + bounds.y));
    }
    return result;
}

// Eje medio de la silueta (X2). Misma exploración que el Eje torneado —dos
// perfiles axiales, uno por lado— y lo que cambia es qué se hace con ellos: en
// vez de sumar los dos offsets para dar el diámetro, se toma el PUNTO MEDIO de
// cada pareja y se les ajusta una recta.
//
// Eso es lo que hace que dé igual cómo de descentrado vaya el trazo del
// operador: el punto medio entre los dos bordes reales no depende de por dónde
// pase la línea que dibujó, solo de dónde estén los flancos.
ToolRunResult runMedianAxis(const cv::Mat& gray, const Fixture& fixture,
                            const ToolConfig& config, const MedianAxisGeometry& g,
                            const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f from = toImg(fixture, g.axisFrom);
    const cv::Point2f to = toImg(fixture, g.axisTo);
    if (cv::norm(to - from) < 5.0) {
        result.detail = "El eje trazado es demasiado corto";
        return result;
    }

    const int stations = std::clamp(g.stations, 5, 200);
    const double reach = std::max(5.0, static_cast<double>(g.searchBand));
    const auto sideA = axialProfile(gray, from, to, ProfileSide::Positive, stations, reach);
    const auto sideB = axialProfile(gray, from, to, ProfileSide::Negative, stations, reach);
    if (sideA.size() != sideB.size() || sideA.empty()) {
        result.detail = "No se pudo recorrer el eje";
        return result;
    }

    // Solo cuentan las estaciones donde se vieron LOS DOS flancos: con uno solo
    // no hay punto medio que calcular, y suponerlo simétrico sería inventarse el
    // centro justo en la herramienta que existe para encontrarlo.
    std::vector<cv::Point2f> midpoints;
    midpoints.reserve(sideA.size());
    for (std::size_t i = 0; i < sideA.size(); ++i) {
        if (sideA[i].found && sideB[i].found) {
            midpoints.push_back((sideA[i].point + sideB[i].point) * 0.5F);
        }
    }
    if (midpoints.size() < 5) {
        result.detail = "Solo " + std::to_string(midpoints.size()) +
                        " de " + std::to_string(stations) +
                        " cortes vieron los dos flancos: sube el alcance de búsqueda (" +
                        fmt2(reach) + " px) o mejora el contraste del borde";
        return result;
    }

    const vision::LineFit fit = vision::fitLineRobust(midpoints);
    if (!fit.valid) {
        result.detail = "No se pudo ajustar el eje medio";
        return result;
    }

    // Rectitud: la desviación máxima de los puntos medios respecto a la recta
    // ajustada. Es el ancho de la banda mínima que los contiene a todos, que es
    // como se define la rectitud, y no la desviación típica — una única
    // curvatura en un extremo tiene que salir, no diluirse en la media.
    double straightness = 0.0;
    for (const auto& p : midpoints) {
        straightness = std::max(straightness, std::abs(fit.signedDistance(p)));
    }
    result.measured = straightness;
    result.ok = withinTolerance(config, result.measured);

    // El eje medio, en coordenadas de PIEZA, para que otras herramientas lo
    // referencien. El fixture es rotación + traslación, así que la dirección se
    // transforma llevando dos puntos y restando.
    const cv::Point2f p0 = vision::toPieceCoords(fixture, fit.point);
    const cv::Point2f p1 = vision::toPieceCoords(fixture, fit.point + fit.direction);
    result.derived.kind = DerivedKind::Line;
    result.derived.point = p0;
    result.derived.direction = p1 - p0;

    std::string detail = "rectitud=" + fmtLen(straightness, fmt);

    // Desalineación entre la primera mitad del eje y la segunda: es lo que
    // delata dos tramos de distinto diámetro que no son coaxiales. Se da solo si
    // cada mitad tiene puntos de sobra para que su recta signifique algo; con
    // tres puntos, el "ángulo" que saldría sería ruido con unidades.
    const std::size_t half = midpoints.size() / 2;
    if (half >= 4) {
        const std::vector<cv::Point2f> firstHalf(midpoints.begin(),
                                                 midpoints.begin() + static_cast<long>(half));
        const std::vector<cv::Point2f> secondHalf(midpoints.begin() + static_cast<long>(half),
                                                  midpoints.end());
        const vision::LineFit fitA = vision::fitLineRobust(firstHalf);
        const vision::LineFit fitB = vision::fitLineRobust(secondHalf);
        if (fitA.valid && fitB.valid) {
            // Ángulo entre dos RECTAS, en [0°, 90°]: los valores absolutos son
            // lo que evita que salga 179° donde hay 1°.
            const double dot = std::abs(static_cast<double>(fitA.direction.x) * fitB.direction.x +
                                        static_cast<double>(fitA.direction.y) * fitB.direction.y);
            const double cross =
                std::abs(static_cast<double>(fitA.direction.x) * fitB.direction.y -
                         static_cast<double>(fitA.direction.y) * fitB.direction.x);
            const double misalignment = std::atan2(cross, dot) * 180.0 / kPi;
            detail += ", desalineación de los dos tramos=" + fmt2(misalignment) + "°";
        }
    }
    detail += " (" + std::to_string(midpoints.size()) + "/" + std::to_string(stations) +
              " cortes)";
    result.detail = detail;

    // Se dibuja el eje ENCONTRADO, no el trazado: el trazado ya se ve mientras
    // se dibuja, y lo que el operador necesita comprobar es si el que ha salido
    // cae por el centro de la pieza.
    const cv::Point2f along = fit.direction;
    double tMin = 0.0;
    double tMax = 0.0;
    for (const auto& p : midpoints) {
        const double t = (p - fit.point).dot(along);
        tMin = std::min(tMin, t);
        tMax = std::max(tMax, t);
    }
    result.overlaySegments.push_back({fit.point + static_cast<float>(tMin) * along,
                                      fit.point + static_cast<float>(tMax) * along});
    for (const auto& p : midpoints) {
        result.overlayPoints.push_back(p);
    }
    return result;
}

}  // namespace

core::Result<ToolRunResult> runTool(const cv::Mat& image, const vision::Fixture& fixture,
                                    const ToolConfig& config, double mmPerPixel,
                                    LengthUnit unit, const cv::Mat& imageToMm,
                                    const vision::BoardFrame* board, double scaleQuality,
                                    const DerivedElements* references) {
    using ResultT = core::Result<ToolRunResult>;
    const Fmt fmt{mmPerPixel, unit, imageToMm, scaleQuality};

    // Una herramienta que declara referencia y no la tiene resuelta NO MIDE.
    // Nunca cae a una referencia implícita: un GD&T medido contra otro datum
    // del que cree el operador es exactamente el fallo que este programa
    // existe para evitar, porque el número sale creíble y es falso.
    for (const std::string* declared : {&config.reference, &config.reference2}) {
        if (declared->empty()) {
            continue;
        }
        if (references == nullptr || references->find(*declared) == references->end()) {
            ToolRunResult missing;
            missing.toolId = config.id;
            missing.name = config.name;
            missing.type = config.type;
            missing.ok = false;
            missing.detail = "no se pudo usar la referencia '" + *declared +
                             "': no existe, está desactivada o falló al medir";
            return ResultT::ok(std::move(missing));
        }
    }

    if (image.empty()) {
        return ResultT::err("Imagen vacía");
    }
    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else if (image.channels() == 1) {
        gray = image;
    } else {
        return ResultT::err("Formato de imagen no soportado");
    }

    auto geometry = geometryFromJson(config.type, config.geometryJson);
    if (!geometry.isOk()) {
        return ResultT::err(geometry.error().message);
    }

    try {
        switch (config.type) {
            case ToolType::Caliper:
                return ResultT::ok(runCaliper(gray, fixture, config,
                                              std::get<CaliperGeometry>(geometry.value()),
                                              fmt));
            case ToolType::Circle:
                return ResultT::ok(runCircle(gray, fixture, config,
                                             std::get<CircleGeometry>(geometry.value()),
                                             fmt));
            case ToolType::PointToLine:
                return ResultT::ok(
                    runPointToLine(gray, fixture, config,
                                   std::get<PointToLineGeometry>(geometry.value()),
                                   fmt));
            case ToolType::EdgeFlaw:
                return ResultT::ok(runEdgeFlaw(gray, fixture, config,
                                               std::get<EdgeFlawGeometry>(geometry.value()),
                                               fmt));
            case ToolType::Blob:
                return ResultT::ok(runBlob(gray, fixture, config,
                                           std::get<BlobGeometry>(geometry.value()),
                                           fmt));
            case ToolType::Ruler:
                return ResultT::ok(runRuler(fixture, config,
                                            std::get<RulerGeometry>(geometry.value()),
                                            fmt));
            case ToolType::LineToLine:
                return ResultT::ok(runLineToLine(
                    fixture, config, std::get<LineToLineGeometry>(geometry.value()), fmt));
            case ToolType::Angle:
                return ResultT::ok(runAngle(
                    fixture, config, std::get<AngleGeometry>(geometry.value()), fmt));
            case ToolType::PolyBlob:
                return ResultT::ok(runPolyBlob(
                    gray, fixture, config, std::get<PolyBlobGeometry>(geometry.value()), fmt));
            case ToolType::Arc:
                return ResultT::ok(runArc(gray, fixture, config,
                                          std::get<ArcGeometry>(geometry.value()), fmt));
            case ToolType::Shaft:
                return ResultT::ok(runShaft(gray, fixture, config,
                                            std::get<ShaftGeometry>(geometry.value()), fmt));
            case ToolType::Groove:
                return ResultT::ok(runGroove(gray, fixture, config,
                                             std::get<GrooveGeometry>(geometry.value()),
                                             fmt));
            case ToolType::Thread:
                return ResultT::ok(runThread(gray, fixture, config,
                                             std::get<ThreadGeometry>(geometry.value()), fmt));
            case ToolType::Gear:
                return ResultT::ok(runGear(gray, fixture, config,
                                           std::get<GearGeometry>(geometry.value()), fmt));
            case ToolType::Position: {
                // Sin tablero explícito: cero en la pieza y ejes de la imagen.
                const vision::BoardFrame fallback{fixture.origin, 0.0};
                static const DerivedElements kNone;
                return ResultT::ok(runPosition(fixture, config,
                                               std::get<PositionGeometry>(geometry.value()),
                                               fmt, board != nullptr ? *board : fallback,
                                               references != nullptr ? *references : kNone));
            }
            case ToolType::Region:
                return ResultT::ok(runRegion(gray, fixture, config,
                                             std::get<RegionGeometry>(geometry.value()), fmt));
            case ToolType::Fillet:
                return ResultT::ok(runFillet(
                    gray, fixture, config, std::get<FilletGeometry>(geometry.value()),
                    fmt));
            case ToolType::Chamfer:
                return ResultT::ok(runChamfer(
                    gray, fixture, config, std::get<ChamferGeometry>(geometry.value()),
                    fmt));
            case ToolType::Extremes:
                return ResultT::ok(runExtremes(
                    gray, fixture, config, std::get<ExtremesGeometry>(geometry.value()),
                    fmt));
            case ToolType::Profile:
                return ResultT::ok(runProfile(
                    gray, fixture, config, std::get<ProfileGeometry>(geometry.value()),
                    fmt));
            case ToolType::BoltPattern:
                return ResultT::ok(runBoltPattern(
                    gray, fixture, config, std::get<BoltPatternGeometry>(geometry.value()),
                    fmt));
            case ToolType::CentreOffset: {
                static const DerivedElements kNone;
                return ResultT::ok(runCentreOffset(
                    fixture, config, std::get<CentreOffsetGeometry>(geometry.value()), fmt,
                    references != nullptr ? *references : kNone));
            }
            case ToolType::Orientation: {
                static const DerivedElements kNone;
                return ResultT::ok(runOrientation(
                    gray, fixture, config, std::get<OrientationGeometry>(geometry.value()),
                    fmt, references != nullptr ? *references : kNone));
            }
            case ToolType::Roundness:
                return ResultT::ok(runRoundness(
                    gray, fixture, config, std::get<RoundnessGeometry>(geometry.value()),
                    fmt));
            case ToolType::Straightness:
                return ResultT::ok(runStraightness(
                    gray, fixture, config, std::get<StraightnessGeometry>(geometry.value()),
                    fmt));
            case ToolType::Clearance:
                return ResultT::ok(runClearance(
                    gray, fixture, config, std::get<ClearanceGeometry>(geometry.value()),
                    fmt));
            case ToolType::EdgeDefects:
                return ResultT::ok(runEdgeDefects(
                    gray, fixture, config, std::get<EdgeDefectsGeometry>(geometry.value()),
                    fmt));
            case ToolType::Polygon:
                return ResultT::ok(runPolygon(
                    gray, fixture, config, std::get<PolygonGeometry>(geometry.value()), fmt));
            case ToolType::Symmetry:
                return ResultT::ok(runSymmetry(
                    gray, fixture, config, std::get<SymmetryGeometry>(geometry.value()), fmt));
            case ToolType::MedianAxis:
                return ResultT::ok(runMedianAxis(
                    gray, fixture, config, std::get<MedianAxisGeometry>(geometry.value()),
                    fmt));
            case ToolType::ConstructedPoint: {
                static const DerivedElements kNone;
                return ResultT::ok(runConstructedPoint(
                    fixture, config, std::get<ConstructedPointGeometry>(geometry.value()),
                    references != nullptr ? *references : kNone));
            }
            case ToolType::ConstructedLine: {
                static const DerivedElements kNone;
                return ResultT::ok(runConstructedLine(
                    fixture, config, std::get<ConstructedLineGeometry>(geometry.value()),
                    references != nullptr ? *references : kNone));
            }
        }
        return ResultT::err("Tipo de herramienta no soportado");
    } catch (const cv::Exception& e) {
        return ResultT::err(std::string("Excepción de OpenCV ejecutando '") + config.name +
                            "': " + e.what());
    }
}

namespace {

// Ejecuta una herramienta y la convierte SIEMPRE en un resultado: un error de
// configuración es un NG con su motivo, no una excepción que tumbe la tanda.
ToolRunResult runOrExplain(const cv::Mat& image, const vision::Fixture& fixture,
                           const ToolConfig& config, double mmPerPixel, LengthUnit unit,
                           const cv::Mat& imageToMm, const vision::BoardFrame* board,
                           double scaleQuality, const DerivedElements& references) {
    auto result = runTool(image, fixture, config, mmPerPixel, unit, imageToMm, board,
                          scaleQuality, &references);
    if (result.isOk()) {
        return std::move(result.value());
    }
    ToolRunResult failed;
    failed.toolId = config.id;
    failed.name = config.name;
    failed.type = config.type;
    failed.ok = false;
    failed.detail = result.error().message;
    return failed;
}

}  // namespace

std::vector<ToolRunResult> runTools(const cv::Mat& image, const vision::Fixture& fixture,
                                    const std::vector<ToolConfig>& tools, double mmPerPixel,
                                    LengthUnit unit, const cv::Mat& imageToMm,
                                    const vision::BoardFrame* board, double scaleQuality,
                                    bool parallel) {
    // Las herramientas se ejecutan en ORDEN DE DEPENDENCIA —primero las que
    // pueden ser referencia, después las que la consumen— pero los resultados
    // se devuelven en el orden en que el operador las tiene en la lista. Si se
    // reordenaran, la lista de resultados bailaría cada vez que alguien añade
    // una referencia.
    std::vector<const ToolConfig*> pending;
    std::set<std::string> known;  // nombres de herramientas habilitadas
    for (const auto& config : tools) {
        if (config.enabled) {
            pending.push_back(&config);
            known.insert(config.name);
        }
    }

    std::vector<ToolRunResult> results;
    results.reserve(pending.size());
    DerivedElements references;
    std::set<std::string> attempted;

    bool progressed = true;
    while (progressed && !pending.empty()) {
        progressed = false;
        std::vector<const ToolConfig*> stillPending;
        std::vector<const ToolConfig*> ready;
        for (const auto* config : pending) {
            // Lista para ejecutarse cuando TODAS las referencias que declara ya
            // se intentaron. Una referencia a un nombre que no existe no espera
            // a nadie: se resuelve —fallando— dentro de `runTool`.
            bool isReady = true;
            for (const std::string* ref : {&config->reference, &config->reference2}) {
                if (ref->empty() || known.find(*ref) == known.end()) {
                    continue;
                }
                if (attempted.find(*ref) == attempted.end()) {
                    isReady = false;
                }
            }
            (isReady ? ready : stillPending).push_back(config);
        }

        // Dentro de una ONDA las herramientas son independientes por
        // construcción: una solo entra aquí cuando todas sus referencias se
        // intentaron en ondas ANTERIORES, así que ninguna lee lo que otra de
        // esta misma onda va a producir. Eso es lo que hace seguro repartirlas,
        // y no una apuesta: la estructura ya lo garantizaba, solo faltaba
        // aprovecharla.
        //
        // Cada una escribe en SU hueco y el mapa de referencias se actualiza
        // después, en serie. Escribir en el mapa dentro del bucle sería la
        // carrera de datos evidente, y no hace falta para nada.
        std::vector<ToolRunResult> wave(ready.size());
        const auto runOne = [&](std::size_t index) {
            wave[index] = runOrExplain(image, fixture, *ready[index], mmPerPixel, unit,
                                       imageToMm, board, scaleQuality, references);
        };
        if (parallel && ready.size() > 1) {
            // `cv::parallel_for_` y no hilos a mano: OpenCV ya está aquí, ya
            // tiene su reparto y respeta el número de hilos que el usuario le
            // haya puesto al proceso.
            cv::parallel_for_(cv::Range(0, static_cast<int>(ready.size())),
                              [&](const cv::Range& range) {
                                  for (int i = range.start; i < range.end; ++i) {
                                      runOne(static_cast<std::size_t>(i));
                                  }
                              });
        } else {
            for (std::size_t i = 0; i < ready.size(); ++i) {
                runOne(i);
            }
        }

        for (std::size_t i = 0; i < ready.size(); ++i) {
            attempted.insert(ready[i]->name);
            if (wave[i].ok && wave[i].derived.valid()) {
                references[ready[i]->name] = wave[i].derived;
            }
            results.push_back(std::move(wave[i]));
            progressed = true;
        }
        pending = std::move(stillPending);
    }

    // Lo que queda no pudo ordenarse, y con referencias entre herramientas solo
    // hay una manera de que eso pase: un ciclo. Con dos referencias por
    // herramienta el ciclo puede ser largo (A→B→C→A), así que no se nombra al
    // culpable —no lo hay— sino a quién está esperando cada una. Fallan
    // diciéndolo, en vez de colgarse.
    for (const auto* config : pending) {
        std::string waiting;
        for (const std::string* ref : {&config->reference, &config->reference2}) {
            if (ref->empty() || attempted.find(*ref) != attempted.end()) {
                continue;
            }
            waiting += (waiting.empty() ? "" : " y ") + ("'" + *ref + "'");
        }
        ToolRunResult cyclic;
        cyclic.toolId = config->id;
        cyclic.name = config->name;
        cyclic.type = config->type;
        cyclic.ok = false;
        cyclic.detail = "referencia circular: '" + config->name + "' espera a " + waiting +
                        ", que a su vez acaban esperándola a ella";
        results.push_back(std::move(cyclic));
    }

    // De vuelta al orden de la lista del operador.
    std::vector<ToolRunResult> ordered;
    ordered.reserve(results.size());
    for (const auto& config : tools) {
        if (!config.enabled) {
            continue;
        }
        for (auto& result : results) {
            if (result.name == config.name && result.toolId == config.id) {
                ordered.push_back(std::move(result));
                break;
            }
        }
    }
    return ordered.size() == results.size() ? ordered : results;
}

std::string formatLength(double px, double mmPerPixel, LengthUnit unit) {
    return fmtLen(px, Fmt{mmPerPixel, unit, cv::Mat()});
}

}  // namespace pci::inspection
