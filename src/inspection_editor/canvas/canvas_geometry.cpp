#include "inspection_editor/canvas/canvas_geometry.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

#include "vision/fitting.h"
#include "vision/position_fixture.h"

namespace pci::inspection {

ViewTransform::ViewTransform(cv::Size imageSize, cv::Size widgetSize, double zoom,
                             cv::Point2d pan)
    : imageSize_(imageSize), widgetSize_(widgetSize), zoom_(zoom), pan_(pan) {}

ViewRect ViewTransform::fitRect() const {
    if (imageSize_.width <= 0 || imageSize_.height <= 0 || widgetSize_.width <= 0 ||
        widgetSize_.height <= 0) {
        return {};
    }
    // Ajuste conservando la proporción: la escala la manda el eje más apretado.
    const double scale =
        std::min(static_cast<double>(widgetSize_.width) / imageSize_.width,
                 static_cast<double>(widgetSize_.height) / imageSize_.height);
    ViewRect rect;
    rect.width = imageSize_.width * scale;
    rect.height = imageSize_.height * scale;
    rect.x = (widgetSize_.width - rect.width) / 2.0;
    rect.y = (widgetSize_.height - rect.height) / 2.0;
    return rect;
}

cv::Point2d ViewTransform::clampedPan(const cv::Point2d& pan) const {
    const ViewRect fit = fitRect();
    if (fit.empty()) {
        return {0.0, 0.0};
    }
    const double maxX = std::max(0.0, (fit.width * zoom_ - widgetSize_.width) / 2.0);
    const double maxY = std::max(0.0, (fit.height * zoom_ - widgetSize_.height) / 2.0);
    return {std::clamp(pan.x, -maxX, maxX), std::clamp(pan.y, -maxY, maxY)};
}

ViewRect ViewTransform::targetRect() const {
    const ViewRect fit = fitRect();
    if (fit.empty()) {
        return fit;
    }
    const cv::Point2d pan = clampedPan(pan_);
    ViewRect scaled;
    scaled.width = fit.width * zoom_;
    scaled.height = fit.height * zoom_;
    scaled.x = fit.centerX() + pan.x - scaled.width / 2.0;
    scaled.y = fit.centerY() + pan.y - scaled.height / 2.0;
    return scaled;
}

cv::Point2d ViewTransform::imageToWidget(const cv::Point2f& imagePoint) const {
    const ViewRect target = targetRect();
    if (target.empty()) {
        return {0.0, 0.0};
    }
    return {target.left() + imagePoint.x * target.width / imageSize_.width,
            target.top() + imagePoint.y * target.height / imageSize_.height};
}

cv::Point2f ViewTransform::widgetToImage(const cv::Point2d& widgetPoint) const {
    const ViewRect target = targetRect();
    if (target.empty()) {
        return {0.0F, 0.0F};
    }
    return {static_cast<float>((widgetPoint.x - target.left()) * imageSize_.width /
                               target.width),
            static_cast<float>((widgetPoint.y - target.top()) * imageSize_.height /
                               target.height)};
}

double ViewTransform::displayScale() const {
    const ViewRect target = targetRect();
    if (target.empty() || imageSize_.width <= 0) {
        return 0.0;
    }
    return target.width / imageSize_.width;
}

double distanceToSegment(const cv::Point2f& p, const cv::Point2f& a, const cv::Point2f& b) {
    const cv::Point2f ab = b - a;
    const double len2 = static_cast<double>(ab.x) * ab.x + static_cast<double>(ab.y) * ab.y;
    if (len2 < 1e-9) {
        return cv::norm(p - a);
    }
    const double t = std::clamp(
        (static_cast<double>(p.x - a.x) * ab.x + static_cast<double>(p.y - a.y) * ab.y) / len2,
        0.0, 1.0);
    const cv::Point2f proj = a + ab * static_cast<float>(t);
    return cv::norm(p - proj);
}

// Puntos representativos de una geometría (coords de pieza) para el marco de
// selección múltiple: basta con que UNO caiga dentro del marco.
//
// Por eso tienen que cubrir la FORMA DIBUJADA, no solo su ancla. Con el círculo
// y el blob solo se devolvía el centro, así que un marco trazado sobre el
// anillo o sobre un lado del rectángulo —encima de lo que el operador ve— no
// seleccionaba nada; y del Punto-Línea faltaba el segmento de escaneo, que sí
// se dibuja y sí tiene manija.
std::vector<cv::Point2f> referencePoints(const ToolGeometry& geometry) {
    return std::visit(
        [](const auto& g) -> std::vector<cv::Point2f> {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry> ||
                          std::is_same_v<T, EdgeFlawGeometry> ||
                          std::is_same_v<T, RulerGeometry>) {
                return {g.p0, g.p1};
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                return {g.center,
                        g.center + cv::Point2f(-g.radius, 0.0F),
                        g.center + cv::Point2f(g.radius, 0.0F),
                        g.center + cv::Point2f(0.0F, -g.radius),
                        g.center + cv::Point2f(0.0F, g.radius)};
            } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                const float hw = g.width / 2.0F;
                const float hh = g.height / 2.0F;
                return {g.center,
                        g.center + cv::Point2f(-hw, -hh),
                        g.center + cv::Point2f(hw, -hh),
                        g.center + cv::Point2f(hw, hh),
                        g.center + cv::Point2f(-hw, hh)};
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                return {g.a0, g.a1, g.b0, g.b1};
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                return {g.vertex, g.end0, g.end1};
            } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                return g.vertices;
            } else if constexpr (std::is_same_v<T, PositionGeometry>) {
                return {g.point};
            } else if constexpr (std::is_same_v<T, ArcGeometry>) {
                return {g.start, g.mid, g.end};
            } else if constexpr (std::is_same_v<T, ShaftGeometry> ||
                                 std::is_same_v<T, ThreadGeometry>) {
                return {g.axisFrom, g.axisTo};
            } else {
                return {g.lineA, g.lineB, g.scanA, g.scanB};
            }
        },
        geometry);
}

// Puntos-manija editables de una geometría (coords de pieza), en orden fijo.
// Cada manija se puede arrastrar por separado. Casos especiales: la 2ª manija
// del círculo es el radio (centro + (r,0)); la 2ª del blob es una esquina que
// redimensiona el rectángulo de forma simétrica respecto al centro.
std::vector<cv::Point2f> handlePoints(const ToolGeometry& geometry) {
    return std::visit(
        [](const auto& g) -> std::vector<cv::Point2f> {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry> ||
                          std::is_same_v<T, EdgeFlawGeometry> ||
                          std::is_same_v<T, RulerGeometry>) {
                return {g.p0, g.p1};
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                return {g.center, g.center + cv::Point2f(g.radius, 0.0F)};
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                return {g.lineA, g.lineB, g.scanA, g.scanB};
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                return {g.a0, g.a1, g.b0, g.b1};
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                return {g.vertex, g.end0, g.end1};
            } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                return {g.center,
                        g.center + cv::Point2f(g.width / 2.0F, g.height / 2.0F)};
            } else if constexpr (std::is_same_v<T, PositionGeometry>) {
                return {g.point};  // una sola manija: el rasgo marcado
            } else if constexpr (std::is_same_v<T, ArcGeometry>) {
                return {g.start, g.mid, g.end};
            } else if constexpr (std::is_same_v<T, ShaftGeometry> ||
                                 std::is_same_v<T, ThreadGeometry>) {
                return {g.axisFrom, g.axisTo};
            } else {  // PolyBlobGeometry
                return g.vertices;
            }
        },
        geometry);
}

// Reposiciona una sola manija (coords de pieza); el índice corresponde al orden
// de handlePoints. Mantiene coherente la geometría (radio y tamaños mínimos).
void setHandlePoint(ToolGeometry& geometry, int handle, const cv::Point2f& q) {
    // Un índice fuera de rango no puede tocar nada. Sin este filtro caía en la
    // rama `else`/`default` de cada tipo y movía la ÚLTIMA manija: un arrastre
    // que sobrevive a un cambio de selección o a un deshacer (el índice era
    // válido para la geometría anterior, no para esta) deformaba la herramienta
    // en silencio, y el operador no tenía forma de relacionar causa y efecto.
    if (handle < 0 || handle >= static_cast<int>(handlePoints(geometry).size())) {
        return;
    }
    std::visit(
        [&](auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, CaliperGeometry> ||
                          std::is_same_v<T, EdgeFlawGeometry> ||
                          std::is_same_v<T, RulerGeometry>) {
                if (handle == 0) {
                    g.p0 = q;
                } else {
                    g.p1 = q;
                }
            } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                if (handle == 0) {
                    g.center = q;
                } else {
                    g.radius = std::max(4.0F, static_cast<float>(cv::norm(q - g.center)));
                }
            } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                switch (handle) {
                    case 0: g.lineA = q; break;
                    case 1: g.lineB = q; break;
                    case 2: g.scanA = q; break;
                    default: g.scanB = q; break;
                }
            } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                switch (handle) {
                    case 0: g.a0 = q; break;
                    case 1: g.a1 = q; break;
                    case 2: g.b0 = q; break;
                    default: g.b1 = q; break;
                }
            } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                switch (handle) {
                    case 0: g.vertex = q; break;
                    case 1: g.end0 = q; break;
                    default: g.end1 = q; break;
                }
            } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                if (handle == 0) {
                    g.center = q;
                } else {
                    g.width = std::max(8.0F, 2.0F * std::abs(q.x - g.center.x));
                    g.height = std::max(8.0F, 2.0F * std::abs(q.y - g.center.y));
                }
            } else if constexpr (std::is_same_v<T, PositionGeometry>) {
                g.point = q;
            } else if constexpr (std::is_same_v<T, ArcGeometry>) {
                switch (handle) {
                    case 0: g.start = q; break;
                    case 1: g.mid = q; break;
                    default: g.end = q; break;
                }
            } else if constexpr (std::is_same_v<T, ShaftGeometry> ||
                                 std::is_same_v<T, ThreadGeometry>) {
                if (handle == 0) {
                    g.axisFrom = q;
                } else {
                    g.axisTo = q;
                }
            } else {  // PolyBlobGeometry
                if (handle >= 0 && handle < static_cast<int>(g.vertices.size())) {
                    g.vertices[static_cast<std::size_t>(handle)] = q;
                }
            }
        },
        geometry);
}

double distanceToGeometry(const ToolGeometry& geometry, const vision::Fixture& fixture,
                          const cv::Point2f& p) {
    double d = 1e9;
    std::visit(
        [&](const auto& g) {
                using T = std::decay_t<decltype(g)>;
                if constexpr (std::is_same_v<T, CaliperGeometry> ||
                              std::is_same_v<T, EdgeFlawGeometry> ||
                              std::is_same_v<T, RulerGeometry>) {
                    d = distanceToSegment(p, vision::toImageCoords(fixture, g.p0), vision::toImageCoords(fixture, g.p1));
                } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                    d = std::abs(cv::norm(p - vision::toImageCoords(fixture, g.center)) - g.radius);
                } else if constexpr (std::is_same_v<T, PointToLineGeometry>) {
                    d = std::min(distanceToSegment(p, vision::toImageCoords(fixture, g.lineA), vision::toImageCoords(fixture, g.lineB)),
                                 distanceToSegment(p, vision::toImageCoords(fixture, g.scanA), vision::toImageCoords(fixture, g.scanB)));
                } else if constexpr (std::is_same_v<T, LineToLineGeometry>) {
                    d = std::min(distanceToSegment(p, vision::toImageCoords(fixture, g.a0), vision::toImageCoords(fixture, g.a1)),
                                 distanceToSegment(p, vision::toImageCoords(fixture, g.b0), vision::toImageCoords(fixture, g.b1)));
                } else if constexpr (std::is_same_v<T, AngleGeometry>) {
                    d = std::min(distanceToSegment(p, vision::toImageCoords(fixture, g.vertex), vision::toImageCoords(fixture, g.end0)),
                                 distanceToSegment(p, vision::toImageCoords(fixture, g.vertex), vision::toImageCoords(fixture, g.end1)));
                } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                    const float hw = g.width / 2.0F;
                    const float hh = g.height / 2.0F;
                    const cv::Point2f c[4] = {
                        vision::toImageCoords(fixture, g.center + cv::Point2f(-hw, -hh)),
                        vision::toImageCoords(fixture, g.center + cv::Point2f(hw, -hh)),
                        vision::toImageCoords(fixture, g.center + cv::Point2f(hw, hh)),
                        vision::toImageCoords(fixture, g.center + cv::Point2f(-hw, hh))};
                    for (int k = 0; k < 4; ++k) {
                        d = std::min(d, distanceToSegment(p, c[k], c[(k + 1) % 4]));
                    }
                } else if constexpr (std::is_same_v<T, ShaftGeometry> ||
                                     std::is_same_v<T, ThreadGeometry>) {
                    d = distanceToSegment(p, vision::toImageCoords(fixture, g.axisFrom),
                                          vision::toImageCoords(fixture, g.axisTo));
                } else if constexpr (std::is_same_v<T, PolyBlobGeometry>) {
                    const std::size_t n = g.vertices.size();
                    for (std::size_t k = 0; k < n; ++k) {
                        d = std::min(d, distanceToSegment(p, vision::toImageCoords(fixture, g.vertices[k]),
                                                          vision::toImageCoords(fixture, g.vertices[(k + 1) % n])));
                    }
                } else if constexpr (std::is_same_v<T, PositionGeometry>) {
                    d = cv::norm(p - vision::toImageCoords(fixture, g.point));  // un punto: distancia directa
                } else if constexpr (std::is_same_v<T, ArcGeometry>) {
                    const cv::Point2f s = vision::toImageCoords(fixture, g.start);
                    const cv::Point2f m = vision::toImageCoords(fixture, g.mid);
                    const cv::Point2f e = vision::toImageCoords(fixture, g.end);
                    const vision::ArcSpan arc = vision::circleThroughThreePoints(s, m, e);
                    if (!arc.valid) {
                        // Tres puntos alineados: todavía no es un arco, pero se
                        // tiene que poder agarrar para corregirlo.
                        d = std::min(distanceToSegment(p, s, m), distanceToSegment(p, m, e));
                    } else {
                        const double angle =
                            std::atan2(static_cast<double>(p.y) - arc.center.y,
                                       static_cast<double>(p.x) - arc.center.x) *
                            57.29577951308232;
                        if (vision::angleWithinSweep(angle, arc.startAngleDeg, arc.sweepDeg)) {
                            d = std::abs(cv::norm(p - arc.center) - arc.radius);
                        } else {
                            // Fuera del sector: lo más cercano son los extremos,
                            // no la circunferencia completa. Medir contra el
                            // círculo entero haría seleccionable la parte del
                            // aro que no se dibuja.
                            d = std::min(cv::norm(p - s), cv::norm(p - e));
                        }
                    }
                }
            },
            geometry);
    return d;
}

double pickTolerance(double screenPixels, double displayScale) {
    if (!(displayScale > 1e-6)) {
        return screenPixels;  // sin imagen todavía: no hay escala que aplicar
    }
    return screenPixels / displayScale;
}

namespace {

ViewRect clampInto(ViewRect box, const ViewRect& bounds) {
    if (box.width <= bounds.width) {
        box.x = std::clamp(box.x, bounds.left(), bounds.right() - box.width);
    } else {
        box.x = bounds.left();  // no cabe: al menos que se vea el principio
    }
    if (box.height <= bounds.height) {
        box.y = std::clamp(box.y, bounds.top(), bounds.bottom() - box.height);
    } else {
        box.y = bounds.top();
    }
    return box;
}

bool freeSpot(const ViewRect& box, const std::vector<ViewRect>& taken) {
    return std::none_of(taken.begin(), taken.end(),
                        [&box](const ViewRect& other) { return box.intersects(other); });
}

}  // namespace

ViewRect placeLabel(const ViewRect& preferred, const std::vector<ViewRect>& taken,
                    const ViewRect& bounds) {
    const ViewRect start = bounds.empty() ? preferred : clampInto(preferred, bounds);
    if (freeSpot(start, taken)) {
        return start;
    }
    // Se busca alejándose de la posición pedida a saltos de una etiqueta,
    // primero abajo y luego arriba: junto al borde inferior la única salida es
    // subir. El alcance está acotado a propósito: una etiqueta que se va al
    // otro extremo del lienzo deja de pertenecer visualmente a su herramienta,
    // y eso confunde más que un solape. Si con muchas medidas en el mismo punto
    // no queda sitio cerca, se acepta el solape (se probó un barrido fino
    // adicional para aprovechar huecos entre rejillas desalineadas y no mejoró
    // el reparto: el límite es el alcance, no el tamaño del salto).
    const double step = start.height + 2.0;
    constexpr int kAttempts = 12;
    for (const double direction : {1.0, -1.0}) {
        for (int k = 1; k <= kAttempts; ++k) {
            ViewRect candidate = start;
            candidate.y += direction * step * k;
            if (!bounds.empty() && !candidate.containedIn(bounds)) {
                break;  // ese lado se agotó
            }
            if (freeSpot(candidate, taken)) {
                return candidate;
            }
        }
    }
    return start;  // sin hueco: visible y solapada, mejor que invisible
}

int pickHandle(const ToolGeometry& geometry, const vision::Fixture& fixture,
               const cv::Point2f& imagePoint, double tolerance) {
    int best = -1;
    double bestDistance = tolerance;
    const auto handles = handlePoints(geometry);
    for (int i = 0; i < static_cast<int>(handles.size()); ++i) {
        const double d =
            cv::norm(imagePoint -
                     vision::toImageCoords(fixture, handles[static_cast<std::size_t>(i)]));
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

}  // namespace pci::inspection
