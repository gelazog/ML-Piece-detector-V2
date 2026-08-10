#include "inspection_editor/auto_measure.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <variant>

#include "inspection_editor/execution/tool_executor.h"
#include "vision/fitting.h"
#include "vision/geometry_features.h"
#include "vision/position_fixture.h"

namespace pci::inspection {

namespace {

constexpr double kRadToDeg = 57.29577951308232;

cv::Point2f toPiece(const vision::Fixture& fixture, const cv::Point2f& imagePoint) {
    return vision::toPieceCoords(fixture, imagePoint);
}

// Dirección de un tramo recto, normalizada y en forma canónica (x > 0), para
// que dos lados opuestos de la misma cara den la misma dirección.
cv::Point2f canonicalDirection(const vision::ContourPrimitive& primitive) {
    cv::Point2f d = primitive.end - primitive.start;
    const double len = cv::norm(d);
    if (len < 1e-6) {
        return {1.0F, 0.0F};
    }
    d /= static_cast<float>(len);
    if (d.x < 0.0F || (std::abs(d.x) < 1e-6F && d.y < 0.0F)) {
        d = -d;
    }
    return d;
}

double angleBetweenDeg(const cv::Point2f& a, const cv::Point2f& b) {
    const double dot = std::clamp(static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y,
                                  -1.0, 1.0);
    return std::acos(std::abs(dot)) * kRadToDeg;
}

// Punto medio de una geometría de longitud, para comparar dos propuestas.
cv::Point2f measurementAnchor(const ToolGeometry& geometry) {
    if (const auto* ruler = std::get_if<RulerGeometry>(&geometry)) {
        return (ruler->p0 + ruler->p1) / 2.0F;
    }
    if (const auto* caliper = std::get_if<CaliperGeometry>(&geometry)) {
        return (caliper->p0 + caliper->p1) / 2.0F;
    }
    return {0.0F, 0.0F};
}

// ¿Esta propuesta mide lo mismo que alguna ya aceptada?
//
// Sin esto, en un rectángulo salían "Ancho total = 279" y "Espesor 1 = 280":
// la misma cota dos veces, con dos nombres. Revisar una lista con duplicados
// cuesta más que revisar una lista corta.
bool alreadyCovered(const std::vector<AutoProposal>& accepted, const AutoProposal& candidate) {
    const bool candidateIsLength = candidate.config.type == ToolType::Ruler ||
                                   candidate.config.type == ToolType::Caliper;
    if (!candidateIsLength) {
        return false;
    }
    const cv::Point2f anchor = measurementAnchor(candidate.geometry);
    for (const auto& other : accepted) {
        if (other.config.type != ToolType::Ruler && other.config.type != ToolType::Caliper) {
            continue;
        }
        const double reference = std::max(std::abs(other.measured), 1.0);
        const bool sameValue =
            std::abs(other.measured - candidate.measured) / reference < 0.02;
        const bool samePlace =
            cv::norm(measurementAnchor(other.geometry) - anchor) < 25.0;
        if (sameValue && samePlace) {
            return true;
        }
    }
    return false;
}

// Mide una propuesta ejecutando de verdad la herramienta. Devuelve false si no
// consigue medir: entonces la propuesta se descarta en vez de ofrecerse.
bool measureProposal(const cv::Mat& gray, const vision::Fixture& fixture, double mmPerPixel,
                     AutoProposal& proposal) {
    proposal.config.geometryJson = toJson(proposal.geometry);
    proposal.config.toleranceMin = 0.0;
    proposal.config.toleranceMax = 1e9;
    const auto result = runTool(gray, fixture, proposal.config, mmPerPixel);
    if (!result.isOk() || !result.value().ok) {
        return false;
    }
    proposal.measured = result.value().measured;
    proposal.detail = result.value().detail;
    suggestTolerances(proposal.config.type, proposal.measured, proposal.config.toleranceMin,
                      proposal.config.toleranceMax);
    return true;
}

}  // namespace

std::vector<AutoProposal> proposeTools(const cv::Mat& gray, const cv::Mat& mask,
                                       const vision::Fixture& fixture,
                                       const ProposeOptions& options, double mmPerPixel) {
    std::vector<AutoProposal> proposals;
    if (gray.empty() || mask.empty()) {
        return proposals;
    }

    std::vector<std::vector<cv::Point>> outer;
    cv::findContours(mask, outer, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (outer.empty()) {
        return proposals;
    }
    const auto& contour = *std::max_element(
        outer.begin(), outer.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });

    // --- Envolvente: largo y ancho de la pieza ---------------------------
    // Son las dos primeras medidas que toma cualquiera con un pie de rey, así
    // que se proponen siempre y las primeras.
    const cv::RotatedRect box = cv::minAreaRect(contour);
    const double angle = box.angle * CV_PI / 180.0;
    const cv::Point2f axisX(static_cast<float>(std::cos(angle)),
                            static_cast<float>(std::sin(angle)));
    const cv::Point2f axisY(-axisX.y, axisX.x);
    // Cuál de los dos lados es el "largo" se decide por su tamaño: minAreaRect
    // no garantiza que width sea el mayor, y fiarse de eso hacía que la pieza
    // saliera con el largo y el ancho intercambiados.
    std::pair<cv::Point2f, float> spans[] = {{axisX, box.size.width},
                                             {axisY, box.size.height}};
    if (spans[1].second > spans[0].second) {
        std::swap(spans[0], spans[1]);
    }
    int spanIndex = 0;
    for (const auto& [dir, extent] : spans) {
        ++spanIndex;
        if (extent < options.minFeatureLength) {
            continue;
        }
        AutoProposal p;
        p.config.type = ToolType::Ruler;
        p.config.name = spanIndex == 1 ? "Largo total" : "Ancho total";
        p.geometry = RulerGeometry{toPiece(fixture, box.center - dir * (extent / 2.0F)),
                                   toPiece(fixture, box.center + dir * (extent / 2.0F))};
        p.reason = "Dimensión general de la pieza (rectángulo mínimo que la contiene).";
        if (measureProposal(gray, fixture, mmPerPixel, p) && !alreadyCovered(proposals, p)) {
            proposals.push_back(std::move(p));
        }
    }

    // --- Agujeros: un Círculo por cada uno --------------------------------
    int holeIndex = 0;
    for (const auto& hole : vision::findHoles(mask)) {
        ++holeIndex;
        cv::Point2f center;
        float radius = 0.0F;
        cv::minEnclosingCircle(hole, center, radius);
        if (radius * 2.0 < options.minFeatureLength) {
            continue;
        }
        AutoProposal p;
        p.config.type = ToolType::Circle;
        p.config.name = "Ø agujero " + std::to_string(holeIndex);
        CircleGeometry g;
        g.center = toPiece(fixture, center);
        g.radius = radius;
        g.searchBand = std::max(4.0F, radius * 0.3F);
        g.rayCount = 36;
        p.geometry = g;
        p.reason = "Agujero interno detectado en la máscara de la pieza.";
        if (measureProposal(gray, fixture, mmPerPixel, p) && !alreadyCovered(proposals, p)) {
            proposals.push_back(std::move(p));
        }
    }

    // --- Descomposición del contorno --------------------------------------
    const auto primitives = vision::decomposeContour(contour);

    // Arcos: el radio de cada redondeo.
    int arcIndex = 0;
    for (const auto& primitive : primitives) {
        if (primitive.kind != vision::PrimitiveKind::Arc ||
            primitive.length < options.minFeatureLength) {
            continue;
        }
        ++arcIndex;
        AutoProposal p;
        p.config.type = ToolType::Arc;
        p.config.name = "Radio " + std::to_string(arcIndex);
        ArcGeometry g;
        g.start = toPiece(fixture, primitive.start);
        g.mid = toPiece(fixture, primitive.mid);
        g.end = toPiece(fixture, primitive.end);
        g.searchBand = std::max(4.0F, static_cast<float>(primitive.radius) * 0.3F);
        g.rayCount = 24;
        p.geometry = g;
        p.reason = "Tramo curvo del contorno: un redondeo de radio ≈ " +
                   std::to_string(static_cast<int>(std::lround(primitive.radius))) + " px.";
        if (measureProposal(gray, fixture, mmPerPixel, p) && !alreadyCovered(proposals, p)) {
            proposals.push_back(std::move(p));
        }
    }

    // Caras enfrentadas: un Calíper que las cruce.
    std::vector<const vision::ContourPrimitive*> lines;
    for (const auto& primitive : primitives) {
        if (primitive.kind == vision::PrimitiveKind::Line &&
            primitive.length >= options.minFeatureLength) {
            lines.push_back(&primitive);
        }
    }
    int caliperIndex = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        for (std::size_t j = i + 1; j < lines.size(); ++j) {
            const cv::Point2f di = canonicalDirection(*lines[i]);
            const cv::Point2f dj = canonicalDirection(*lines[j]);
            if (angleBetweenDeg(di, dj) > options.parallelToleranceDeg) {
                continue;  // no son paralelas
            }
            // Se cruzan por el punto medio de la primera, perpendicularmente.
            const cv::Point2f mid = lines[i]->mid;
            const cv::Point2f normal(-di.y, di.x);
            const cv::Point2f other = lines[j]->mid;
            const double gap = std::abs(static_cast<double>(other.x - mid.x) * normal.x +
                                        static_cast<double>(other.y - mid.y) * normal.y);
            if (gap < options.minFeatureLength) {
                continue;  // demasiado juntas para ser dos caras distintas
            }
            const cv::Point2f towards =
                (static_cast<double>(other.x - mid.x) * normal.x +
                         static_cast<double>(other.y - mid.y) * normal.y >
                     0.0
                     ? normal
                     : cv::Point2f(-normal.x, -normal.y));
            // El trazo sobresale por los dos lados para que los dos bordes
            // caigan dentro del recorrido de búsqueda.
            const cv::Point2f from = mid - towards * static_cast<float>(gap * 0.25);
            const cv::Point2f to = mid + towards * static_cast<float>(gap * 1.25);

            ++caliperIndex;
            AutoProposal p;
            p.config.type = ToolType::Caliper;
            p.config.name = "Espesor " + std::to_string(caliperIndex);
            p.geometry = CaliperGeometry{toPiece(fixture, from), toPiece(fixture, to), 10.0F};
            p.reason = "Dos caras paralelas enfrentadas, a ≈ " +
                       std::to_string(static_cast<int>(std::lround(gap))) + " px.";
            if (measureProposal(gray, fixture, mmPerPixel, p) &&
                !alreadyCovered(proposals, p)) {
                proposals.push_back(std::move(p));
            }
        }
    }

    // Esquinas vivas: el ángulo entre dos caras consecutivas.
    int cornerIndex = 0;
    for (std::size_t i = 0; i + 1 < primitives.size(); ++i) {
        const auto& a = primitives[i];
        const auto& b = primitives[i + 1];
        if (a.kind != vision::PrimitiveKind::Line || b.kind != vision::PrimitiveKind::Line ||
            a.length < options.minFeatureLength || b.length < options.minFeatureLength) {
            continue;
        }
        const double between = angleBetweenDeg(canonicalDirection(a), canonicalDirection(b));
        if (between < options.minCornerAngleDeg) {
            continue;  // prácticamente la misma cara
        }
        ++cornerIndex;
        AutoProposal p;
        p.config.type = ToolType::Angle;
        p.config.name = "Ángulo " + std::to_string(cornerIndex);
        // El vértice es la costura entre los dos tramos; los extremos, hacia
        // fuera por cada cara.
        p.geometry = AngleGeometry{toPiece(fixture, a.end), toPiece(fixture, a.start),
                                   toPiece(fixture, b.end)};
        p.reason = "Esquina entre dos caras rectas del contorno.";
        if (measureProposal(gray, fixture, mmPerPixel, p) && !alreadyCovered(proposals, p)) {
            proposals.push_back(std::move(p));
        }
    }

    // --- Recorte final -----------------------------------------------------
    // Se ordenan por tamaño del rasgo medido: lo grande define la pieza y lo
    // pequeño suele ser detalle. Los ángulos van al final porque su "medida"
    // está en grados y no compara con las longitudes.
    std::stable_sort(proposals.begin(), proposals.end(),
                     [](const AutoProposal& a, const AutoProposal& b) {
                         const bool aAngle = a.config.type == ToolType::Angle;
                         const bool bAngle = b.config.type == ToolType::Angle;
                         if (aAngle != bAngle) {
                             return !aAngle;
                         }
                         return a.measured > b.measured;
                     });
    if (static_cast<int>(proposals.size()) > options.maxProposals) {
        proposals.resize(static_cast<std::size_t>(options.maxProposals));
    }
    return proposals;
}

}  // namespace pci::inspection
