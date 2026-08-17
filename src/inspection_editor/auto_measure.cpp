#include "inspection_editor/auto_measure.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <variant>

#include "inspection_editor/execution/tool_executor.h"
#include "vision/fitting.h"
#include "vision/geometry_features.h"
#include "vision/position_fixture.h"
#include "vision/shape_class.h"

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

// ¿La pieza es más oscura que su fondo? La herramienta de Lados necesita
// saberlo y `proposeTools` no recibe la polaridad de la segmentación.
//
// Se MIDE en vez de suponerse: la media dentro de la máscara contra la media
// fuera. Suponer «pieza oscura», que es el valor por defecto, hacía que la
// propuesta de Lados no midiera nada en un montaje a contraluz y se descartara
// en silencio — el operador no vería la propuesta y no sabría por qué.
bool pieceIsDarkerThanBackground(const cv::Mat& gray, const cv::Mat& mask) {
    cv::Mat background;
    cv::bitwise_not(mask, background);
    const double inside = cv::mean(gray, mask)[0];
    const double outside = cv::mean(gray, background)[0];
    return inside < outside;
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
    proposal.kind = result.value().kind;
    proposal.detail = result.value().detail;
    suggestTolerances(proposal.config.type, proposal.measured, proposal.config.toleranceMin,
                      proposal.config.toleranceMax);
    return true;
}

}  // namespace

std::vector<AutoProposal> proposeTools(const cv::Mat& gray, const cv::Mat& mask,
                                       const vision::Fixture& fixture,
                                       const ProposeOptions& options, double mmPerPixel,
                                       int* dropped) {
    if (dropped != nullptr) {
        *dropped = 0;
    }
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

    // --- Qué figura es ------------------------------------------------------
    // Se pregunta ANTES de proponer nada, porque cambia qué tiene sentido
    // medir. Sin esta pregunta, a un disco se le proponían el largo y el ancho
    // de su rectángulo envolvente —dos números que sobre una pieza redonda no
    // significan nada, y que además son el mismo— y a un hexágono no se le
    // proponía ni un lado.
    const vision::ShapeClass shape = vision::classifyShape(contour, mask);
    const bool isRound =
        shape.kind == vision::ShapeKind::Circle || shape.kind == vision::ShapeKind::Ring;

    // --- Redonda: diámetro y redondez --------------------------------------
    if (isRound && shape.outerDiameter > options.minFeatureLength) {
        AutoProposal diameter;
        diameter.config.type = ToolType::Circle;
        diameter.config.name = shape.kind == vision::ShapeKind::Ring ? "Ø exterior" : "Ø";
        CircleGeometry g;
        g.center = toPiece(fixture, shape.center);
        g.radius = static_cast<float>(shape.outerDiameter / 2.0);
        g.searchBand = std::max(6.0F, g.radius * 0.25F);
        g.rayCount = 72;
        diameter.geometry = g;
        diameter.reason = std::string(shape.reason) + ". El perímetro sale " +
                          std::to_string(static_cast<int>(std::lround(
                              CV_PI * shape.outerDiameter))) +
                          " px.";
        if (measureProposal(gray, fixture, mmPerPixel, diameter)) {
            proposals.push_back(std::move(diameter));
        }

        // La redondez es la otra mitad: un diámetro correcto no dice nada de la
        // FORMA. Una pieza ovalada puede dar el diámetro nominal en el ajuste y
        // estar fuera de plano, y esa es exactamente la avería que un pie de
        // rey no ve.
        AutoProposal round;
        round.config.type = ToolType::Roundness;
        round.config.name = "Redondez";
        RoundnessGeometry r;
        r.center = toPiece(fixture, shape.center);
        r.radius = static_cast<float>(shape.outerDiameter / 2.0);
        r.searchBand = std::max(6.0F, r.radius * 0.25F);
        r.rayCount = 72;
        round.geometry = r;
        round.reason = "La pieza es redonda: el diámetro solo dice el tamaño, la redondez "
                       "dice si la forma está dentro de plano.";
        if (measureProposal(gray, fixture, mmPerPixel, round)) {
            proposals.push_back(std::move(round));
        }
    }

    // --- Con esquinas vivas: cuántos lados hay ------------------------------
    // Ojo con lo que mide esta herramienta: mide el RECUENTO, no las
    // longitudes. Su valor es «6 lados», y su tolerancia vigila que no
    // aparezca ni falte una cara — que es una avería distinta de que un lado
    // se salga de cota. Las longitudes van aparte, una regla por lado.
    //
    // Solo para polígonos de esquina viva. En uno redondeado la propia
    // herramienta se niega, y con razón: exige que el recuento no cambie al
    // mitad y al doble de epsilon, y al afinar epsilon las esquinas
    // redondeadas aparecen como vértices nuevos. Proponerla ahí sería ofrecer
    // una propuesta que nace muerta.
    if (shape.kind == vision::ShapeKind::Polygon && shape.sides >= 3) {
        const cv::Rect bounds = cv::boundingRect(contour);
        AutoProposal sides;
        sides.config.type = ToolType::Polygon;
        sides.config.name = "Lados (" + std::to_string(shape.sides) + ")";
        PolygonGeometry g;
        g.center = toPiece(fixture, cv::Point2f(static_cast<float>(bounds.x + bounds.width / 2.0),
                                                static_cast<float>(bounds.y + bounds.height / 2.0)));
        // Con holgura: el recuadro tiene que abarcar la pieza entera, y ceñirlo
        // al contorno deja fuera los píxeles del borde.
        g.width = static_cast<float>(bounds.width * 1.1);
        g.height = static_cast<float>(bounds.height * 1.1);
        g.epsilonFraction = 0.02F;
        g.darkPiece = pieceIsDarkerThanBackground(gray, mask);
        sides.geometry = g;
        sides.reason = std::string(shape.reason) + ". Mide cada lado y cada ángulo interior.";
        if (measureProposal(gray, fixture, mmPerPixel, sides)) {
            proposals.push_back(std::move(sides));
        }
    }

    // --- Envolvente: largo y ancho de la pieza ---------------------------
    // Son las dos primeras medidas que toma cualquiera con un pie de rey, así
    // que se proponen siempre y las primeras... salvo en una pieza redonda,
    // donde el largo y el ancho de la envolvente SON el diámetro, y proponer
    // tres nombres para el mismo número es exactamente lo que hace que una
    // lista de propuestas no se revise.
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
        if (extent < options.minFeatureLength || isRound) {
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
        // En una arandela el agujero central no es «un agujero más»: es la otra
        // cota de la pieza, y llamarlo «agujero 1» obligaría al operador a
        // adivinar cuál de los dos círculos está mirando.
        const bool isTheRingBore =
            shape.kind == vision::ShapeKind::Ring &&
            std::abs(2.0 * radius - shape.innerDiameter) < 2.0;
        p.config.name = isTheRingBore ? "Ø interior"
                                      : "Ø agujero " + std::to_string(holeIndex);
        CircleGeometry g;
        g.center = toPiece(fixture, center);
        g.radius = radius;
        g.searchBand = std::max(4.0F, radius * 0.3F);
        g.rayCount = 36;
        p.geometry = g;
        p.reason = isTheRingBore ? "Agujero central de la corona: la cota interior de la pieza."
                                 : "Agujero interno detectado en la máscara de la pieza.";
        if (measureProposal(gray, fixture, mmPerPixel, p) && !alreadyCovered(proposals, p)) {
            proposals.push_back(std::move(p));
        }
    }

    // --- Descomposición del contorno --------------------------------------
    // Con las MISMAS opciones que usó el clasificador, ajustadas al tamaño de
    // la pieza. Si cada uno mirara el contorno con un paso distinto, el
    // clasificador podría decir «hexágono» y esta parte proponer cuatro lados.
    const auto primitives =
        vision::decomposeContour(contour, vision::decomposeOptionsFor(contour));

    // Los lados, uno a uno. Esto es lo que le pone tolerancia y veredicto a
    // cada cara: «Lados» dice cuántas hay y estas dicen cuánto mide cada una.
    //
    // Vale igual para el polígono de esquina viva y para el redondeado, porque
    // las dos formas tienen tramos rectos y la descomposición ya los ha
    // separado de los redondeos.
    if (shape.kind == vision::ShapeKind::Polygon || shape.kind == vision::ShapeKind::Rounded) {
        int sideIndex = 0;
        for (const auto& primitive : primitives) {
            if (primitive.kind != vision::PrimitiveKind::Line ||
                primitive.length < options.minFeatureLength) {
                continue;
            }
            ++sideIndex;
            AutoProposal p;
            p.config.type = ToolType::Ruler;
            p.config.name = "Lado " + std::to_string(sideIndex);
            p.geometry = RulerGeometry{toPiece(fixture, primitive.start),
                                       toPiece(fixture, primitive.end)};
            p.reason = "Tramo recto del contorno, de extremo a extremo.";
            if (measureProposal(gray, fixture, mmPerPixel, p) && !alreadyCovered(proposals, p)) {
                proposals.push_back(std::move(p));
            }
        }
    }

    // Arcos: el radio de cada redondeo.
    //
    // En una pieza redonda NO: ahí el «arco» es el contorno entero y su radio
    // es la mitad del diámetro que ya se ha propuesto. Serían dos nombres para
    // la misma cota, que es justo lo que hace que una lista no se revise.
    // El tamaño de la pieza, que es la vara para decidir si un «redondeo» lo es.
    cv::Point2f pieceCentre;
    float pieceRadius = 0.0F;
    cv::minEnclosingCircle(contour, pieceCentre, pieceRadius);

    int arcIndex = 0;
    for (const auto& primitive : primitives) {
        if (isRound || primitive.kind != vision::PrimitiveKind::Arc ||
            primitive.length < options.minFeatureLength) {
            continue;
        }
        // Un redondeo MÁS GRANDE QUE LA PIEZA no es un redondeo. La
        // descomposición devuelve como arco cualquier tramo que no consigue
        // llamar recta, y a un tramo casi recto le sale una circunferencia
        // enorme: en la pieza de muestra salía «un redondeo de radio ≈ 3899 px»
        // sobre una pieza de 199. Ese número no es una cota, es el síntoma de
        // que ahí no hay curva.
        if (primitive.radius > 2.0 * pieceRadius) {
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
        // Y la banda de búsqueda se limita a la pieza. Salía de multiplicar el
        // radio por 0,3, así que con aquel radio absurdo la herramienta buscaba
        // el borde en 1170 px de banda sobre una imagen de 640x480: cualquier
        // cosa que encontrara allí no era esta pieza.
        g.searchBand = std::clamp(static_cast<float>(primitive.radius) * 0.3F, 4.0F,
                                  std::max(8.0F, pieceRadius * 0.5F));
        g.rayCount = 24;
        p.geometry = g;
        p.reason = "Tramo curvo del contorno: un redondeo de radio ≈ " +
                   std::to_string(static_cast<int>(std::lround(primitive.radius))) + " px.";
        if (!measureProposal(gray, fixture, mmPerPixel, p) || alreadyCovered(proposals, p)) {
            continue;
        }
        // Y que haya medido el arco que se le pidió, la misma regla que ya se
        // aplica al calíper: la propuesta prometía un radio y si la herramienta
        // devuelve otro, no está midiendo eso.
        if (std::abs(p.measured - primitive.radius) > std::max(4.0, primitive.radius * 0.25)) {
            continue;
        }
        proposals.push_back(std::move(p));
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
            if (!measureProposal(gray, fixture, mmPerPixel, p) || alreadyCovered(proposals, p)) {
                continue;
            }
            // Y que haya medido LO QUE SE LE PIDIÓ. El calíper recorre su
            // trazo y se queda con el primer par de bordes de polaridad
            // opuesta que encuentra, que no tiene por qué ser el par de caras
            // que motivó la propuesta: en una pieza en L salía «Espesor 2, dos
            // caras a ≈ 260 px» midiendo 81, porque por el camino se topaba
            // con una pared más cercana.
            //
            // Eso son dos fallos en uno: un motivo que miente y una cota
            // repetida con otro nombre. Se descarta, y no se «arregla» el
            // texto: la propuesta prometía medir esas dos caras y no las mide.
            if (std::abs(p.measured - gap) > std::max(4.0, gap * 0.1)) {
                continue;
            }
            proposals.push_back(std::move(p));
        }
    }

    // Esquinas vivas: el ángulo entre dos caras consecutivas.
    //
    // El contorno es CERRADO, así que la última cara hace esquina con la
    // primera. Recorrerlo hasta `size()-1` perdía siempre esa esquina, y el
    // fallo no se veía mirando una pieza cualquiera: a un hexágono le proponía
    // cinco ángulos de seis y a un triángulo dos de tres. Un contador que
    // siempre se queda uno corto es peor que no tenerlo, porque cuadra con la
    // pieza casi siempre y falla justo cuando cuentas.
    int cornerIndex = 0;
    for (std::size_t i = 0; i < primitives.size(); ++i) {
        const auto& a = primitives[i];
        const auto& b = primitives[(i + 1) % primitives.size()];
        if (primitives.size() < 2) {
            break;
        }
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
    //
    // Antes se ordenaba «primero las longitudes de mayor a menor y los ángulos
    // al final», y luego se cortaba por el tope. El razonamiento de mandar los
    // ángulos al final era bueno —su medida está en grados y no se compara con
    // una longitud— pero la consecuencia era desastrosa: con el tope de doce, un
    // hexágono genera unas dieciocho propuestas y **perdía sus seis ángulos**,
    // todos. Ordenar por categoría y cortar por el final no recorta lo pequeño:
    // borra una categoría entera.
    //
    // Ahora se ordena DENTRO de cada clase de medida y se van tomando por
    // turnos. El recorte se lleva lo más pequeño de cada clase, que es lo que se
    // quería desde el principio, y ninguna desaparece por completo.
    std::vector<std::vector<AutoProposal>> byKind(5);
    for (auto& proposal : proposals) {
        byKind[static_cast<std::size_t>(proposal.kind)].push_back(std::move(proposal));
    }
    for (auto& group : byKind) {
        std::stable_sort(group.begin(), group.end(),
                         [](const AutoProposal& a, const AutoProposal& b) {
                             return a.measured > b.measured;
                         });
    }

    const std::size_t total = proposals.size();
    std::vector<AutoProposal> ordered;
    ordered.reserve(total);
    for (std::size_t round = 0; ordered.size() < total; ++round) {
        bool tookAny = false;
        for (auto& group : byKind) {
            if (round < group.size()) {
                ordered.push_back(std::move(group[round]));
                tookAny = true;
            }
        }
        if (!tookAny) {
            break;  // no quedaba nada en ninguna clase
        }
    }
    proposals = std::move(ordered);

    if (static_cast<int>(proposals.size()) > options.maxProposals) {
        if (dropped != nullptr) {
            *dropped = static_cast<int>(proposals.size()) - options.maxProposals;
        }
        proposals.resize(static_cast<std::size_t>(options.maxProposals));
    } else if (dropped != nullptr) {
        *dropped = 0;
    }
    return proposals;
}

}  // namespace pci::inspection
