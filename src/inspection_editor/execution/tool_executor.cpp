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

ToolRunResult baseResult(const ToolConfig& config) {
    ToolRunResult result;
    result.toolId = config.id;
    result.name = config.name;
    result.type = config.type;
    return result;
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
ToolRunResult runPosition(const Fixture& fixture, const ToolConfig& config,
                          const PositionGeometry& g, const Fmt& fmt,
                          const vision::BoardFrame& board) {
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
                    ", redondez=" + fmtLen(roundness, fmt);
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
    for (int k = 0; k < scans; ++k) {
        const float t = length * static_cast<float>(k) / static_cast<float>(scans - 1);
        const cv::Point2f base = p0 + u * t;
        const cv::Point2f from = base - n * (g.scanLength / 2.0F);
        const cv::Point2f to = base + n * (g.scanLength / 2.0F);
        const auto edges = detectEdges(gray, from, to, 1.0F, 1);
        if (edges.empty()) {
            continue;
        }
        ts.push_back(static_cast<double>(t));
        offsets.push_back(edges[0].position - static_cast<double>(g.scanLength) / 2.0);
        result.overlayPoints.push_back(edges[0].point);
    }

    if (ts.size() < static_cast<std::size_t>(scans) * 6 / 10) {
        result.detail = "Borde no detectado en suficientes escaneos (" +
                        std::to_string(ts.size()) + "/" + std::to_string(scans) + ")";
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
            case ToolType::Thread:
                return ResultT::ok(runThread(gray, fixture, config,
                                             std::get<ThreadGeometry>(geometry.value()), fmt));
            case ToolType::Gear:
                return ResultT::ok(runGear(gray, fixture, config,
                                           std::get<GearGeometry>(geometry.value()), fmt));
            case ToolType::Position: {
                // Sin tablero explícito: cero en la pieza y ejes de la imagen.
                const vision::BoardFrame fallback{fixture.origin, 0.0};
                return ResultT::ok(runPosition(fixture, config,
                                               std::get<PositionGeometry>(geometry.value()),
                                               fmt, board != nullptr ? *board : fallback));
            }
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
                                    const vision::BoardFrame* board, double scaleQuality) {
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
        for (const auto* config : pending) {
            // Lista para ejecutarse cuando TODAS las referencias que declara ya
            // se intentaron. Una referencia a un nombre que no existe no espera
            // a nadie: se resuelve —fallando— dentro de `runTool`.
            bool ready = true;
            for (const std::string* ref : {&config->reference, &config->reference2}) {
                if (ref->empty() || known.find(*ref) == known.end()) {
                    continue;
                }
                if (attempted.find(*ref) == attempted.end()) {
                    ready = false;
                }
            }
            if (!ready) {
                stillPending.push_back(config);
                continue;
            }
            ToolRunResult result =
                runOrExplain(image, fixture, *config, mmPerPixel, unit, imageToMm, board,
                             scaleQuality, references);
            attempted.insert(config->name);
            if (result.ok && result.derived.valid()) {
                references[config->name] = result.derived;
            }
            results.push_back(std::move(result));
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
