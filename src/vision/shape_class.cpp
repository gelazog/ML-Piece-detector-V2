#include "vision/shape_class.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>

#include "vision/fitting.h"
#include "vision/geometry_features.h"

namespace pci::vision {

namespace {

std::string round0(double value) {
    return std::to_string(static_cast<int>(std::lround(value)));
}

std::vector<cv::Point2f> asFloat(const std::vector<cv::Point>& contour) {
    std::vector<cv::Point2f> points;
    points.reserve(contour.size());
    for (const auto& p : contour) {
        points.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
    }
    return points;
}

// Distancia de un punto a un segmento. La necesita la desviación al polígono:
// medir contra los VÉRTICES daría casi cero para cualquier forma con muchos
// puntos, porque siempre hay un vértice cerca. Lo que hay que medir es la
// separación al LADO.
double distanceToSegment(const cv::Point2f& p, const cv::Point2f& a, const cv::Point2f& b) {
    const cv::Point2f ab = b - a;
    const double lengthSq = static_cast<double>(ab.x) * ab.x + static_cast<double>(ab.y) * ab.y;
    if (lengthSq < 1e-12) {
        return cv::norm(p - a);
    }
    const cv::Point2f ap = p - a;
    double t = (static_cast<double>(ap.x) * ab.x + static_cast<double>(ap.y) * ab.y) / lengthSq;
    t = std::clamp(t, 0.0, 1.0);
    const cv::Point2f projection = a + ab * static_cast<float>(t);
    return cv::norm(p - projection);
}

// Lo que peor encaja: el punto del contorno más lejos del polígono cerrado.
//
// Se usa el MÁXIMO y no la media a propósito. La media perdona una esquina
// redondeada entre veinte tramos rectos, y esa esquina es justo la diferencia
// entre «polígono» y «polígono redondeado», que se miden distinto.
double worstDistanceToPolygon(const std::vector<cv::Point>& contour,
                              const std::vector<cv::Point>& polygon) {
    if (polygon.size() < 2) {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    for (const auto& point : contour) {
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const cv::Point2f a(static_cast<float>(polygon[i].x),
                                static_cast<float>(polygon[i].y));
            const cv::Point2f b(
                static_cast<float>(polygon[(i + 1) % polygon.size()].x),
                static_cast<float>(polygon[(i + 1) % polygon.size()].y));
            best = std::min(best, distanceToSegment(cv::Point2f(static_cast<float>(point.x),
                                                                static_cast<float>(point.y)),
                                                    a, b));
        }
        worst = std::max(worst, best);
    }
    return worst;
}

// Lo que peor encaja contra la circunferencia ajustada.
double worstRadialDeviation(const std::vector<cv::Point2f>& points, const cv::Point2f& center,
                            double radius) {
    double worst = 0.0;
    for (const auto& point : points) {
        worst = std::max(worst, std::abs(cv::norm(point - center) - radius));
    }
    return worst;
}

// El polígono que mejor explica el contorno.
//
// `approxPolyDP` con un epsilon fijo no vale: el bueno depende de cuánto ruido
// tiene el borde y de cuántos lados hay. Así que se barre epsilon... y lo que
// se busca en ese barrido NO es «el primero que cumple», sino la MESETA: el
// número de lados que aguanta a lo largo del mayor rango de epsilon.
//
// La diferencia la enseñó un hexágono girado. Con «el primero que cumple», un
// hexágono a 0° salía de 6 lados y a 10° de 7 y a 15° de 8, porque al rasterizar
// un borde inclinado aparecen escalones y con el epsilon más fino se cuelan
// vértices de más. Medido, el ajuste de 6 vértices aparece en 29 de los 30
// epsilon barridos y el de 7 u 8 en uno solo: la meseta dice cuál es la
// respuesta y el primer acierto dice cuál fue la casualidad.
//
// Y una clase que cambia al girar la pieza no sirve para nada, porque la pieza
// llega a la mesa como llega.
struct PolygonFit {
    std::vector<cv::Point> vertices;
    double deviation = std::numeric_limits<double>::infinity();
    // Vértices del mejor ajuste SIN el tope de lados. Sirve para distinguir dos
    // cosas que si no se confunden: un contorno que ninguna recta explica (una
    // silueta irregular) de uno que explican muchas rectas (una curva
    // discretizada). El segundo no es irregular, es redondo.
    int unlimitedSides = 0;
};

PolygonFit fitPolygon(const std::vector<cv::Point>& contour, const ClassifyOptions& options) {
    PolygonFit best;
    const double perimeter = cv::arcLength(contour, true);
    if (perimeter <= 0.0) {
        return best;
    }

    // Cuántas veces sale cada número de lados a lo largo del barrido, y con qué
    // desviación en su mejor momento.
    struct Tally {
        int seen = 0;
        double bestDeviation = std::numeric_limits<double>::infinity();
        std::vector<cv::Point> vertices;
    };
    std::map<int, Tally> tallies;

    // El barrido empieza MUY fino. Con un paso inicial del 0,5 % del perímetro,
    // un polígono de 14 lados se quedaba sin ajuste: ese epsilon ya vale más
    // que la flecha de sus lados, así que `approxPolyDP` se comía vértices y
    // ningún resultado pasaba la tolerancia. El síntoma era peor que el fallo
    // —salía «irregular», ni lados ni diámetro—, y la causa no se ve mirando el
    // resultado, solo barriendo.
    for (double fraction = 0.001; fraction <= 0.06; fraction += 0.002) {
        std::vector<cv::Point> approx;
        cv::approxPolyDP(contour, approx, fraction * perimeter, true);
        if (approx.size() < 3) {
            break;  // más epsilon solo puede simplificar aún más
        }
        Tally& tally = tallies[static_cast<int>(approx.size())];
        ++tally.seen;
        const double deviation = worstDistanceToPolygon(contour, approx);
        if (deviation < tally.bestDeviation) {
            tally.bestDeviation = deviation;
            tally.vertices = approx;
        }
    }

    // La meseta más ancha gana, pero SOLO entre las que explican el contorno.
    //
    // El orden importa y costó un fallo: mirando primero la anchura y después
    // la desviación, un polígono de 12 lados salía «círculo». Su meseta más
    // ancha por debajo del tope era la de 6 lados —que a epsilon grande sigue
    // devolviendo 6 vértices, malísimos—, se descartaba por desviación, y con
    // ella se iba también el ajuste de 12 que sí era bueno. Una meseta que no
    // explica el contorno no es una candidata peor: no es una candidata.
    //
    // A igualdad de anchura mandan los menos lados, que es la navaja de Occam:
    // entre dos explicaciones igual de estables, la más simple.
    const auto widestThatFits = [&tallies, &options](int maxSides) -> const Tally* {
        const Tally* winner = nullptr;
        for (const auto& [sides, tally] : tallies) {
            if (sides < 3 || (maxSides > 0 && sides > maxSides)) {
                continue;
            }
            if (tally.bestDeviation > options.maxDeviationPx) {
                continue;
            }
            if (winner == nullptr || tally.seen > winner->seen) {
                winner = &tally;
            }
        }
        return winner;
    };

    if (const Tally* capped = widestThatFits(options.maxSides); capped != nullptr) {
        best.vertices = capped->vertices;
        best.deviation = capped->bestDeviation;
    }
    if (const Tally* free = widestThatFits(0); free != nullptr) {
        best.unlimitedSides = static_cast<int>(free->vertices.size());
    }
    return best;
}

// El agujero central que convierte un disco en arandela, si lo hay.
struct CentralHole {
    bool found = false;
    double diameter = 0.0;
};

CentralHole centralHole(const cv::Mat& mask, const cv::Point2f& center, double outerDiameter,
                        const ClassifyOptions& options) {
    CentralHole result;
    if (mask.empty() || outerDiameter <= 0.0) {
        return result;
    }
    for (const auto& hole : findHoles(mask)) {
        cv::Point2f holeCenter;
        float holeRadius = 0.0F;
        cv::minEnclosingCircle(hole, holeCenter, holeRadius);
        const double diameter = 2.0 * holeRadius;
        if (diameter < outerDiameter * options.minRingHoleFraction) {
            continue;
        }
        if (cv::norm(holeCenter - center) > outerDiameter * 0.5 * options.ringConcentricFraction) {
            continue;
        }
        if (diameter > result.diameter) {
            result.found = true;
            result.diameter = diameter;
        }
    }
    return result;
}

}  // namespace

DecomposeOptions decomposeOptionsFor(const std::vector<cv::Point>& contour) {
    DecomposeOptions options;
    const double perimeter = cv::arcLength(contour, true);
    if (perimeter <= 0.0) {
        return options;
    }
    constexpr double kSamplesWanted = 500.0;
    options.resampleStep = std::clamp(perimeter / kSamplesWanted, 0.8, 2.0);
    return options;
}

const char* shapeKindName(ShapeKind kind) {
    switch (kind) {
        case ShapeKind::Circle: return "circulo";
        case ShapeKind::Ring: return "arandela";
        case ShapeKind::Polygon: return "poligono";
        case ShapeKind::Rounded: return "poligono redondeado";
        case ShapeKind::Irregular: return "irregular";
    }
    return "irregular";
}

ShapeClass classifyShape(const std::vector<cv::Point>& contour, const cv::Mat& mask,
                         const ClassifyOptions& options) {
    ShapeClass shape;
    // Con menos de ocho puntos no hay forma que reconocer, solo ruido con
    // ínfulas. Devolver «irregular» aquí no es rendirse: es lo correcto, y
    // hace que quien llame siga midiendo como venía midiendo.
    if (contour.size() < 8) {
        shape.reason = "el contorno tiene muy pocos puntos para reconocer una figura";
        return shape;
    }

    const std::vector<cv::Point2f> points = asFloat(contour);

    // La tolerancia tiene DOS mitades, y hace falta entender por qué antes de
    // tocarla:
    //
    // - Un suelo absoluto, porque el dentado del rasterizado mide más o menos
    //   un píxel y no encoge cuando la pieza crece.
    // - Un término relativo, porque el error de COLOCAR un vértice sí crece con
    //   la pieza: `approxPolyDP` elige como vértice un punto del contorno, y si
    //   ese punto cae un par de píxeles antes de la esquina, el lado entero se
    //   inclina y el extremo lejano se separa mucho más.
    //
    // Con los 6 px fijos a secas, un decágono de radio 400 salía «círculo»: su
    // ajuste de 10 lados se separaba más de 6 px y ninguna otra explicación
    // cabía por debajo del tope de lados. El 2,5 % sale de medir — un hexágono
    // girado se separa 3,9 px con radio 160, que es un 2,4 %.
    cv::Point2f enclosingCentre;
    float enclosingRadius = 0.0F;
    cv::minEnclosingCircle(contour, enclosingCentre, enclosingRadius);
    ClassifyOptions scaled = options;
    scaled.maxDeviationPx = std::max(options.maxDeviationPx, 0.025 * enclosingRadius);

    // --- ¿Rectas unidas por redondeos? -------------------------------------
    // Se pregunta ANTES que nada, y el orden no es un detalle: un polígono con
    // muchos vértices también aproxima un rectángulo redondeado —basta con
    // poner vértices a lo largo de cada esquina— y entonces saldría «polígono
    // de 12 lados», que es la discretización de la curva y no la descripción de
    // la pieza. La descomposición sí distingue una recta de un arco, así que la
    // pregunta correcta es suya.
    //
    // El filtro es cuánto perímetro va en curva. Medido: un rectángulo de
    // 300x200 con redondeos de 40 px lleva el 27 % del contorno en arco; un
    // hexágono y una pieza en L llevan 0 %. El 10 % cae en medio con holgura.
    int straight = 0;
    int arcs = 0;
    double arcLength = 0.0;
    double totalLength = 0.0;
    for (const auto& primitive : decomposeContour(contour, decomposeOptionsFor(contour))) {
        totalLength += primitive.length;
        if (primitive.kind == PrimitiveKind::Line) {
            ++straight;
        } else {
            ++arcs;
            arcLength += primitive.length;
        }
    }
    const double curvedFraction = totalLength > 0.0 ? arcLength / totalLength : 0.0;
    if (straight >= 3 && straight <= scaled.maxSides && arcs >= 1 && curvedFraction >= 0.10) {
        shape.kind = ShapeKind::Rounded;
        shape.sides = straight;
        shape.deviation = 0.0;
        shape.reason = "contorno de " + std::to_string(straight) + " tramos rectos unidos por " +
                       std::to_string(arcs) + " redondeos (" +
                       round0(curvedFraction * 100.0) + " % del contorno va en curva)";
        return shape;
    }

    // --- ¿Es una circunferencia? ------------------------------------------
    const CircleFit circle = fitCircleTaubin(points);
    double circleDeviation = std::numeric_limits<double>::infinity();
    if (circle.valid && circle.radius > 0.0) {
        circleDeviation = worstRadialDeviation(points, circle.center, circle.radius);
    }

    // --- ¿Es un polígono? --------------------------------------------------
    const PolygonFit polygon = fitPolygon(contour, scaled);

    // Los dos modelos compiten con la MISMA vara: cuánto se separa el punto
    // peor. Sin esto habría que ordenarlos a mano —«primero mira si es
    // círculo»— y ese orden decide los empates en silencio. Un dodecágono está
    // a un pelo de ser un círculo, y quien tiene que resolverlo es la medida,
    // no el orden de dos `if`.
    const bool circleFits = circleDeviation <= scaled.maxDeviationPx;
    const bool polygonFits = !polygon.vertices.empty();

    // La tierra de nadie: un contorno con MÁS lados de los que merece la pena
    // medir uno a uno, pero que todavía no cae dentro del ruido de una
    // circunferencia. Un polígono de 16 lados aterrizaba aquí y salía
    // «irregular», que es la peor respuesta posible: ni se le medían los lados
    // ni el diámetro.
    //
    // Lo que hace falta es una vara RELATIVA, porque esto sí escala: separarse
    // 3 px de la circunferencia es un 1 % en una pieza de Ø320 y un 15 % en una
    // de Ø40. Con ese 5 % un polígono de muchos lados pasa a medirse como lo
    // que es en la práctica —una circunferencia, con su redondez diciendo la
    // verdad sobre las caras planas— y una estrella de veinte puntas, que se
    // separa un 40 %, se queda fuera.
    const bool roundEnoughForItsSize =
        circle.valid && circle.radius > 0.0 && circleDeviation <= circle.radius * 0.05;
    const bool polygonisedCurve =
        !polygonFits && polygon.unlimitedSides > scaled.maxSides && roundEnoughForItsSize;

    if (polygonFits && (!circleFits || polygon.deviation <= circleDeviation)) {
        shape.kind = ShapeKind::Polygon;
        shape.sides = static_cast<int>(polygon.vertices.size());
        shape.deviation = polygon.deviation;
        shape.reason = "contorno de " + std::to_string(shape.sides) +
                       " lados rectos (el punto peor se separa " +
                       round0(polygon.deviation) + " px de ellos)";
        return shape;
    }

    if (circleFits || polygonisedCurve) {
        shape.kind = ShapeKind::Circle;
        shape.center = circle.center;
        shape.outerDiameter = 2.0 * circle.radius;
        shape.deviation = circleDeviation;
        const MinimumZoneCircle zone = minimumZoneCircle(points);
        shape.roundness = zone.valid ? zone.width() : 0.0;

        const CentralHole hole = centralHole(mask, circle.center, shape.outerDiameter, scaled);
        if (hole.found) {
            shape.kind = ShapeKind::Ring;
            shape.innerDiameter = hole.diameter;
            shape.reason = "corona circular: Ø exterior " + round0(shape.outerDiameter) +
                           " px y agujero central de " + round0(hole.diameter) + " px";
            return shape;
        }
        // El motivo lo elige quien de verdad decidió. Si el contorno ya cae
        // dentro del ruido de una circunferencia, decir «16 tramos rectos» de
        // un disco sería exacto por dentro y una mentira por fuera.
        shape.reason =
            circleFits
                ? "contorno circular de Ø " + round0(shape.outerDiameter) +
                      " px (el punto peor se separa " + round0(circleDeviation) + " px)"
                : "contorno de " + std::to_string(polygon.unlimitedSides) +
                      " tramos rectos: demasiados para medirlos uno a uno, y se separan solo " +
                      round0(circleDeviation) + " px de una circunferencia de Ø " +
                      round0(shape.outerDiameter) + " px, así que se mide como redonda";
        return shape;
    }

    // «No encaja en ninguna figura» no le dice nada a nadie. Si el contorno SÍ
    // se explica con rectas y lo único que pasa es que hay demasiadas, eso es
    // lo que hay que decir: el operador entiende «tiene 16 lados» y no entiende
    // «irregular».
    if (polygon.unlimitedSides > scaled.maxSides) {
        shape.reason = "contorno de " + std::to_string(polygon.unlimitedSides) +
                       " lados rectos, más de los " + std::to_string(scaled.maxSides) +
                       " que se miden uno a uno, y tampoco es redondo: se mide como pieza suelta";
        return shape;
    }
    shape.reason = "el contorno no encaja en ninguna figura conocida: se mide como pieza suelta";
    return shape;
}

}  // namespace pci::vision
