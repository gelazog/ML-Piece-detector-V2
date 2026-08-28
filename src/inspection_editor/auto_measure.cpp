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
    // CON LA GEOMETRÍA DELANTE, no solo con el tipo.
    //
    // Hay dos sobrecargas y la de tipo lleva escrito al lado que «quien tenga la
    // geometría a mano debe llamar a la que la mira». Aquí se tiene, y se estaba
    // llamando a la otra.
    //
    // Hoy no cambia ningún número —el Área y el Perímetro acaban en la misma
    // banda relativa por los dos caminos— y por eso el fallo era invisible. Pero
    // la Región mide seis cosas con escalas que no se parecen: el día que se
    // proponga una que cuente agujeros, la banda de ±10 % diría «entre 1,8 y 2,2
    // agujeros», que no es una tolerancia, es un sinsentido con forma de número.
    suggestTolerances(proposal.geometry, proposal.measured, proposal.config.toleranceMin,
                      proposal.config.toleranceMax);
    // Y se dice cuando la cota no volverá a medir. El número de AHORA es una
    // medida de verdad —sale de la descomposición del contorno de esta pieza—
    // pero como herramienta guardada repetiría ese mismo valor en cada
    // inspección. Ofrecerla sin decirlo sería vender una comprobación que no
    // existe.
    if (!remeasuresThePiece(proposal.config.type)) {
        proposal.reason += " Se mide ahora sobre esta pieza; guardada, repite este "
                           "valor: vale como cota de referencia, no como comprobación.";
    }
    return true;
}

}  // namespace

const std::vector<ToolType>& proposableTypes() {
    // Las que este proponedor sabe ofrecer, en el orden en que conviene
    // enseñarlas: primero las cotas de tamaño, que son las que casi todo el
    // mundo quiere, y al final las de forma.
    //
    // Vive aquí, al lado de quién las propone, para que añadir una clase nueva
    // y olvidarse de la interfaz no sea posible.
    static const std::vector<ToolType> kTypes = {
        ToolType::Caliper, ToolType::Ruler,     ToolType::Circle,  ToolType::Arc,
        ToolType::Angle,   ToolType::Roundness, ToolType::Polygon, ToolType::Thread,
        ToolType::Gear,    ToolType::Region};
    return kTypes;
}

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

    // --- Área y perímetro de la silueta -----------------------------------
    //
    // LAS DOS ÚNICAS COTAS QUE MIRAN LA PIEZA ENTERA, y faltaban.
    //
    // Todo lo de arriba mide un rasgo: este diámetro, aquel lado, esta esquina.
    // Una pieza puede pasar las doce y estar mellada en un sitio que ninguna
    // cota tocaba. El área y el perímetro no dejan ese hueco: cualquier trozo
    // que sobre o que falte, esté donde esté, mueve los dos números.
    //
    // Y son COMPROBACIÓN, no referencia, que es lo que las distingue del «Largo
    // total» de aquí arriba. La diferencia no está en el valor sino en si el
    // rasgo se puede volver a encontrar en la SIGUIENTE pieza: «Lado 4» es el
    // cuarto tramo en que la descomposición cortó ESTE contorno, y en la pieza
    // de al lado el cuarto tramo es otra cosa. El área de la silueta es la
    // misma pregunta en todas: la Región vuelve a umbralizar y a recorrer el
    // contorno en cada inspección, y por eso `remeasuresThePiece` no le pone el
    // descargo.
    //
    // El recuadro se traza sobre los ejes de la PIEZA con un 15 % de holgura:
    // ceñido al contorno, la umbralización de la Región se queda sin fondo con
    // el que contrastar y el área sale disparada.
    {
        const bool darkPiece = pieceIsDarkerThanBackground(gray, mask);
        const float margin = 1.15F;
        const cv::Point2f centre = toPiece(fixture, box.center);
        const float wide = box.size.width * margin;
        const float tall = box.size.height * margin;

        struct WholePieceMeasure {
            RegionMeasure measure;
            const char* name;
            const char* reason;
        };
        static const WholePieceMeasure kWholePiece[] = {
            {RegionMeasure::Area, "Área",
             "Superficie de la silueta, con los agujeros descontados. Es la cota que "
             "vigila la pieza ENTERA: una mella, una rebaba o una pieza cambiada la "
             "mueven, aunque caigan donde no llega ninguna otra cota."},
            {RegionMeasure::Perimeter, "Perímetro",
             "Longitud del contorno exterior. Acompaña al área porque no se estropean "
             "igual: un borde dentado alarga mucho el perímetro y apenas toca el área, "
             "así que juntas cogen defectos que por separado se escapan."},
        };

        for (const auto& whole : kWholePiece) {
            AutoProposal p;
            p.config.type = ToolType::Region;
            p.config.name = whole.name;
            RegionGeometry g;
            g.center = centre;
            g.width = wide;
            g.height = tall;
            g.measure = whole.measure;
            g.darkPiece = darkPiece;
            p.geometry = g;
            p.reason = whole.reason;
            if (measureProposal(gray, fixture, mmPerPixel, p)) {
                proposals.push_back(std::move(p));
            }
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

    // --- ¿ESTA PIEZA SE REPITE? RUEDA DENTADA Y ROSCA -----------------------
    //
    // Esto es lo que faltaba, y faltaba entero: la medición automática conocía
    // SIETE de las treinta y dos clases de herramienta, y ni la Rosca ni el
    // Engranaje estaban entre ellas. Medido sobre el banco de fotos: cero
    // propuestas de rosca en tres fotos de rosca evidente, cero de engranaje en
    // el engranaje. A un tornillo roscado le ofrecía nueve «Radio» y tres
    // reglas; a la rueda de veinte dientes, nueve «Lado».
    //
    // Y no es que estas dos herramientas no supieran medirlo: la del engranaje
    // saca z=20 en esa foto con dos recuentos independientes que coinciden, y la
    // de la rosca mide 65,9 px de paso en `rosca-1.png`, donde la propia imagen
    // declara 6 hilos por pulgada y la pulgada mide 399 px — o sea 66,5 px. Un
    // 0,9 % de error. Estaban ahí, escritas y funcionando, sin que nadie se las
    // ofreciera al operador.
    //
    // POR QUÉ ES SEGURO PROPONERLAS: este generador ya EJECUTA cada propuesta
    // antes de ofrecerla y tira la que no consigue medir. Con eso, ofrecer un
    // engranaje a una arandela no cuesta nada — la herramienta se niega y la
    // propuesta desaparece. Esa red solo funciona si la herramienta sabe decir
    // que no, y por eso la Rosca tuvo que aprender a hacerlo antes (decía que sí
    // en las dieciséis fotos, con perlas como «paso=1,3 px» en unas arandelas).
    cv::Point2f rimCentre;
    float rimRadius = 0.0F;
    cv::minEnclosingCircle(contour, rimCentre, rimRadius);
    const cv::RotatedRect axisBox = cv::minAreaRect(contour);

    // SE APAGA LA PIEZA ENTERA, Y NO SOLO EL TRAMO ROSCADO. POR QUÉ.
    //
    // Lo intuitivo sería apagar solo lo que cae dentro del tramo de eje sobre el
    // que la Rosca consiguió medir, y así conservar las caras y las esquinas de
    // la cabeza de un tornillo, que son cotas de verdad. Se probó y se midió, y
    // no vale: **ese tramo no delimita la rosca**.
    //
    // El buscador de colocaciones se queda con la primera que MIDA, y medir no
    // es lo mismo que acotar. En `tornillo-1.png`, descomponiendo el contorno, la
    // rosca va del 0 % al 89 % del eje —tramos de 0,6 a 0,9 pasos, uno cada 3,5 %
    // del eje, que es justo el paso— y la cabeza ocupa del 89 % al 100 %, con
    // tramos de 2,5 a 3,8 pasos. La colocación ganadora fue del 30 % al 100 %:
    // metía la cabeza entera dentro y dejaba fuera el primer 30 % de rosca.
    //
    // El resultado de filtrar por ese tramo, medido: volvían NUEVE arcos al
    // tornillo, sentados al 4, 8, 11, 15, 18, 22 y 25 % del eje y separados
    // exactamente un paso. Se llamaban «Radio 14», «Radio 15», «Radio 16»... que
    // es palabra por palabra el «se pasa» del que venimos.
    //
    // Así que se apaga todo. El precio es real y conviene decirlo: un tornillo de
    // cabeza hexagonal se queda sin las cotas de su cabeza y hay que dibujarlas a
    // mano. Se paga porque la alternativa medida es peor.
    bool rimRepeatsItself = false;

    if (options.allows(ToolType::Gear) && rimRadius > options.minFeatureLength) {
        AutoProposal p;
        p.config.type = ToolType::Gear;
        p.config.name = "Dientes (z)";
        GearGeometry g;
        g.center = toPiece(fixture, rimCentre);
        g.outerRadius = rimRadius;
        // Por dentro de la raíz del diente y por fuera del cubo. Un diente
        // normalizado sobresale ~2,25 módulos, así que el 55 % del radio de
        // cabeza cae holgadamente dentro de la corona en cualquier rueda de uso.
        g.innerRadius = rimRadius * 0.55F;
        g.rayCount = 1440;
        p.geometry = g;
        p.reason = "El contorno se repite alrededor del centro: cuenta los dientes y da "
                   "el Ø de cabeza, el de raíz y la excentricidad. El recuento se "
                   "comprueba por dos caminos y la herramienta se niega si no coinciden.";
        // UN HEXÁGONO NO ES UNA RUEDA DE SEIS DIENTES.
        //
        // Y sin embargo lo es para la herramienta: el radio de una tuerca
        // hexagonal se repite SEIS veces por vuelta, exactamente igual de
        // periódico que un engranaje. La primera versión de esto proponía
        // engranaje a los hexágonos y, peor, al darlos por periódicos les
        // apagaba sus seis lados y sus seis ángulos. Lo cazaron quince pruebas
        // que ya existían.
        //
        // La distinción no hay que inventarla, ya está hecha: el clasificador de
        // formas dice «polígono» o «polígono redondeado» y con cuántos lados, y
        // se rinde con «irregular» justo cuando el contorno tiene más detalle
        // del que un polígono explica. Ese es el sitio donde vive un engranaje.
        //
        // Y se deja una puerta por si una rueda de pocos dientes llegara a
        // clasificarse como polígono: por encima del techo de lados del
        // clasificador —doce— ya no hay polígono que valga, es una rueda.
        const bool couldBeAPolygon = shape.kind == vision::ShapeKind::Polygon ||
                                     shape.kind == vision::ShapeKind::Rounded;
        if (measureProposal(gray, fixture, mmPerPixel, p) &&
            (!couldBeAPolygon || p.measured > vision::ClassifyOptions{}.maxSides)) {
            rimRepeatsItself = true;
            proposals.push_back(std::move(p));
        }
    }

    if (options.allows(ToolType::Thread) && !rimRepeatsItself) {
        const double angle = axisBox.angle * CV_PI / 180.0;
        cv::Point2f dir(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
        float longSide = axisBox.size.width;
        float shortSide = axisBox.size.height;
        if (axisBox.size.height > axisBox.size.width) {
            dir = cv::Point2f(-dir.y, dir.x);
            longSide = axisBox.size.height;
            shortSide = axisBox.size.width;
        }
        const cv::Point2f tail = axisBox.center - dir * (longSide / 2.0F);

        // DÓNDE TRAZAR EL EJE, PROBANDO — porque importa y está medido.
        //
        // Con el eje de punta a punta y la banda al ancho entero, la Rosca no
        // mide NINGUNA de las tres roscas del banco: en un tornillo, ese eje
        // mete dentro la cabeza, y el perfil deja de repetirse. Un operador no
        // lo trazaría así, trazaría sobre la caña. Así que se prueban varias
        // colocaciones y se queda la primera que mida — la herramienta ya
        // rechaza las que no, de modo que «probar» no es disparar a ciegas.
        //
        // Se prueban los dos extremos porque de qué lado cae la cabeza no se
        // sabe: el eje del rectángulo mínimo no tiene sentido preferente.
        struct Placement {
            double from;
            double to;
            double band;
        };
        static const Placement kPlacements[] = {
            {0.00, 1.00, 1.00},  // la pieza entera, que es lo que vale en una varilla
            {0.30, 1.00, 0.60}, {0.00, 0.70, 0.60},
            {0.30, 1.00, 1.00}, {0.00, 0.70, 1.00},
        };
        for (const auto& placement : kPlacements) {
            AutoProposal p;
            p.config.type = ToolType::Thread;
            p.config.name = "Paso de rosca";
            ThreadGeometry g;
            g.axisFrom = toPiece(fixture,
                                 tail + dir * static_cast<float>(longSide * placement.from));
            g.axisTo = toPiece(fixture,
                               tail + dir * static_cast<float>(longSide * placement.to));
            g.searchBand = static_cast<float>(shortSide / 2.0 * placement.band);
            g.stations = 240;
            p.geometry = g;
            p.reason = "El perfil se repite a lo largo del eje: es una rosca vista de "
                       "perfil. Da el paso, el Ø exterior y el Ø de fondo, y con "
                       "calibración px→mm propone la designación métrica.";
            if (measureProposal(gray, fixture, mmPerPixel, p)) {
                rimRepeatsItself = true;
                proposals.push_back(std::move(p));
                break;
            }
        }
    }

    // --- Descomposición del contorno --------------------------------------
    // Con las mismas opciones de remuestreo que usó el clasificador, ajustadas
    // al tamaño de la pieza.
    //
    // OJO: compartir las opciones NO basta para que las dos partes digan lo
    // mismo, y aquí ponía que sí. El clasificador cuenta lados con
    // `approxPolyDP` y esto parte el contorno en rectas y arcos: son dos
    // algoritmos, y daban dos respuestas. Medido sobre el banco, de 106 piezas
    // que el clasificador llama polígono, en UNA coincidía el número de lados
    // propuestos — a una tuerca hexagonal la aplicación le decía «6 lados» y le
    // ofrecía «2 lados y 3 redondeos».
    //
    // Por eso, cuando el clasificador ha decidido POLÍGONO, los lados y los
    // ángulos salen de sus vértices (más abajo) y no de aquí. Esta
    // descomposición sigue sirviendo para todo lo demás: los redondeados, el
    // contorno libre, los calibres de caras enfrentadas.
    const auto primitives =
        vision::decomposeContour(contour, vision::decomposeOptionsFor(contour));

    // Los lados, uno a uno: «Lados» dice cuántas caras hay y estas dicen cuánto
    // mide cada una.
    //
    // Se proponen para CUALQUIER forma que no sea redonda, no solo para el
    // polígono y el redondeado. La condición anterior dejaba sin una sola cota
    // de sus caras a las piezas «de contorno libre», aunque la descomposición ya
    // las tuviera medidas y aunque sí recibieran sus ángulos y su envolvente.
    //
    // Qué cae ahí se midió, porque adivinarlo salió mal dos veces: una escuadra
    // con entalla y extremo redondeado la clasifica como REDONDEADA, y un rebaje
    // semicircular en medio de una cara, también. Las que sí caen en contorno
    // libre teniendo caras rectas son, por ejemplo, un canto escalonado (once
    // caras, antes ninguna) o un círculo rematado en punta (dos).
    //
    // La pieza REDONDA sigue fuera, y ahí la razón se mantiene: en un disco el
    // «tramo recto» que aparece es un trozo de la circunferencia mal ajustado,
    // no una cara. Y una ELIPSE, que también es de contorno libre, no recibe
    // lados porque no tiene ninguno — esa es la frontera que impide que quitar
    // la condición se convierta en inventar cotas.
    // CUANDO LA PIEZA ES PERIÓDICA, SUS PRIMITIVAS NO SON COTAS.
    //
    // Aquí es donde se arregla el «se pasa». La descomposición de un contorno
    // dentado o roscado devuelve DECENAS de tramos —la rueda de veinte dientes
    // da unos cuarenta flancos, la rosca da más de cien— y el generador tiene un
    // presupuesto de doce propuestas. Cualquier docena que elija de ahí es una
    // MUESTRA ARBITRARIA: en el engranaje ofrecía «Lado 2», «Lado 7», «Lado 8»,
    // «Lado 17», «Lado 22», «Lado 25», «Lado 30» —ocho de cuarenta flancos, y el
    // número del nombre es un índice interno que al operador no le dice nada—, y
    // en la rosca, seis ángulos que medían los seis 102°: el mismo flanco
    // contado seis veces.
    //
    // Y de paso se llevaban el presupuesto entero, así que las cotas que sí
    // valen —el diámetro, la envolvente, los agujeros— se quedaban fuera.
    //
    // Con la rueda o la rosca ya propuestas, esos tramos están medidos donde
    // corresponde: en «z=20 dientes, Ø cabeza, Ø raíz, excentricidad» y en
    // «paso, Ø exterior, Ø de fondo». Volver a ofrecerlos de uno en uno no
    // añade una cota, añade ruido.
    // SI ES UN POLÍGONO, SUS LADOS SON LOS DEL POLÍGONO CON EL QUE SE DECIDIÓ.
    //
    // No los tramos rectos que encuentre la descomposición: esos son otra
    // lectura del mismo contorno y daban otro número. Una tuerca hexagonal
    // recibía dos lados, y los cuatro que faltaban aparecían como «redondeos».
    const bool fromTheFit =
        shape.kind == vision::ShapeKind::Polygon && shape.vertices.size() >= 3;
    if (fromTheFit && !rimRepeatsItself) {
        const auto& v = shape.vertices;
        for (std::size_t i = 0; i < v.size(); ++i) {
            const cv::Point2f a(v[i]);
            const cv::Point2f b(v[(i + 1) % v.size()]);
            if (cv::norm(b - a) < options.minFeatureLength) {
                continue;
            }
            AutoProposal p;
            p.config.type = ToolType::Ruler;
            p.config.name = "Lado " + std::to_string(i + 1);
            p.geometry = RulerGeometry{toPiece(fixture, a), toPiece(fixture, b)};
            p.reason = "Uno de los " + std::to_string(shape.sides) +
                       " lados con los que se reconoció la pieza, de vértice a vértice.";
            if (measureProposal(gray, fixture, mmPerPixel, p) && !alreadyCovered(proposals, p)) {
                proposals.push_back(std::move(p));
            }
        }
    } else if (!isRound && !rimRepeatsItself) {
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
    // Ya se midió arriba, para acotar la corona de dientes.
    const float pieceRadius = rimRadius;

    // Y EN UN POLÍGONO NO HAY NINGUNO, por definición: si tuviera las esquinas
    // redondeadas, el clasificador habría dicho «polígono redondeado».
    //
    // Sin esta condición, a una tuerca hexagonal se le ofrecían TRES «Radio» —de
    // 28, 22 y 20 px— que son sus propias caras planas leídas como arco por la
    // descomposición. Un redondeo que no existe no es una cota de más: es una
    // cota que el operador acepta, guarda en la plantilla y luego no cuadra con
    // el plano.
    int arcIndex = 0;
    for (const auto& primitive : primitives) {
        if (isRound || rimRepeatsItself || fromTheFit ||
            primitive.kind != vision::PrimitiveKind::Arc ||
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
    for (std::size_t i = 0; !rimRepeatsItself && i < lines.size(); ++i) {
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
    // Y en un polígono, sus esquinas son las de sus vértices — por lo mismo que
    // los lados. Con las primitivas, a una tuerca hexagonal le salía UN ángulo
    // de seis, porque cuatro de sus caras se habían leído como arcos y un
    // ángulo necesita dos rectas consecutivas.
    int cornerIndex = 0;
    if (fromTheFit && !rimRepeatsItself) {
        const auto& v = shape.vertices;
        for (std::size_t i = 0; i < v.size(); ++i) {
            const cv::Point2f previous(v[(i + v.size() - 1) % v.size()]);
            const cv::Point2f corner(v[i]);
            const cv::Point2f next(v[(i + 1) % v.size()]);
            if (cv::norm(corner - previous) < options.minFeatureLength ||
                cv::norm(next - corner) < options.minFeatureLength) {
                continue;
            }
            ++cornerIndex;
            AutoProposal p;
            p.config.type = ToolType::Angle;
            p.config.name = "Ángulo " + std::to_string(cornerIndex);
            p.geometry = AngleGeometry{toPiece(fixture, corner), toPiece(fixture, previous),
                                       toPiece(fixture, next)};
            p.reason = "Una de las " + std::to_string(shape.sides) +
                       " esquinas con las que se reconoció la pieza.";
            if (measureProposal(gray, fixture, mmPerPixel, p) && !alreadyCovered(proposals, p)) {
                proposals.push_back(std::move(p));
            }
        }
    }
    for (std::size_t i = 0; !fromTheFit && !rimRepeatsItself && i < primitives.size(); ++i) {
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

    // --- Filtro de clases --------------------------------------------------
    //
    // Se aplica AQUÍ y no en cada sitio donde se propone algo: son diez sitios,
    // y diez guardas es una para olvidar. En un solo punto tampoco se puede
    // colar una clase nueva sin pasar por el filtro.
    //
    // Y va ANTES del recorte por el tope a propósito. Al revés, el tope de doce
    // se gastaría en cotas que el operador no quiere y luego se filtrarían: le
    // llegarían tres diámetros de los doce que había. Así el tope se reparte
    // entre lo que SÍ pidió.
    if (!options.allowedTypes.empty()) {
        std::vector<AutoProposal> kept;
        kept.reserve(proposals.size());
        for (auto& proposal : proposals) {
            if (options.allows(proposal.config.type)) {
                kept.push_back(std::move(proposal));
            }
        }
        proposals = std::move(kept);
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
        // Y DENTRO DE CADA CLASE, EL RECORTE TAMPOCO SE LO LLEVA TODO DE UN
        // LADO.
        //
        // Ordenar solo por tamaño repetía en pequeño el error que el comentario
        // de aquí arriba arregló en grande: no recortaba lo menos valioso,
        // recortaba lo más corto. Medido sobre una placa de 340 px con dos
        // agujeros, el grupo de longitudes salía
        //
        //     Perímetro 1328 · Largo 339 · Lado 337 · Lado 337 · Lado 336 ·
        //     Lado 336 · Ø agujero 100 · Ø agujero 70
        //
        // y con el tope de doce el recorte se llevaba LOS DOS AGUJEROS: cuatro
        // lados que repiten el mismo número echaban fuera las dos únicas cotas
        // del interior de la pieza.
        //
        // Poner delante lo que comprueba tampoco vale: entonces desaparecía el
        // lado más largo, que es la primera medida que cualquiera busca. Las
        // dos ordenaciones son la misma trampa vista desde cada extremo.
        //
        // Así que se reparte, igual que entre clases: una de las que comprueban,
        // una de las de referencia, y vuelta a empezar. Al cortar caen las
        // últimas de las dos listas y no una lista entera.
        std::vector<AutoProposal> checks;
        std::vector<AutoProposal> references;
        for (auto& proposal : group) {
            // La misma pregunta que decide el descargo que lee el operador,
            // para que el orden y el texto no puedan discrepar.
            (remeasuresThePiece(proposal.config.type) ? checks : references)
                .push_back(std::move(proposal));
        }
        const auto biggestFirst = [](const AutoProposal& a, const AutoProposal& b) {
            return a.measured > b.measured;
        };
        std::stable_sort(checks.begin(), checks.end(), biggestFirst);
        std::stable_sort(references.begin(), references.end(), biggestFirst);

        group.clear();
        for (std::size_t i = 0; i < std::max(checks.size(), references.size()); ++i) {
            if (i < checks.size()) {
                group.push_back(std::move(checks[i]));
            }
            if (i < references.size()) {
                group.push_back(std::move(references[i]));
            }
        }
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
