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

#include <cstdio>

namespace {

std::string round0(double value) {
    return std::to_string(static_cast<int>(std::lround(value)));
}

// UN RESIDUO SUBPÍXEL NO SE ESCRIBE COMO CERO.
//
// `round0` redondea a entero, y con residuos por debajo del píxel eso escribe
// «se separa 0 px» — que es exactamente la lectura engañosa que se acababa de
// quitar del propio número. Da igual medir bien si luego se rotula a cero.
//
// Por debajo de 10 px se dan dos decimales, que es donde vive un residuo de
// ajuste; por encima el detalle no aporta y estorba.
std::string roundFine(double value) {
    char buffer[32];
    if (value < 10.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    }
    return buffer;
}

// Rellena los tramos largos con puntos intermedios.
//
// Hace falta porque el contorno que devuelve `analyzeFrame` viene extraído con
// `CHAIN_APPROX_SIMPLE`, o sea con los tramos rectos COLAPSADOS: de un cuadrado
// alineado a los ejes quedan ocho puntos, todos en las esquinas. Y clasificar
// esos ocho puntos daba «círculo» con desviación CERO — porque una
// circunferencia pasa por las cuatro esquinas de un cuadrado y, sin ningún
// punto en medio de los lados, no hay nada que la desmienta.
//
// Es el peor fallo que puede tener esta función: exacta según su propia medida
// y completamente falsa. Y la trampa estaba puesta justo para quien hiciera lo
// natural, porque la cabecera promete clasificar «el contorno exterior» y el
// contorno exterior es lo que `analyzeFrame` devuelve.
//
// Rellenar en vez de rechazar: los ocho puntos describen el cuadrado
// perfectamente, solo que la MEDIDA de desviación necesita puntos donde poder
// separarse. Sobre un contorno ya denso no añade nada, así que sale gratis.
std::vector<cv::Point> densify(const std::vector<cv::Point>& contour, double step = 1.0) {
    if (contour.size() < 2) {
        return contour;
    }
    std::vector<cv::Point> dense;
    dense.reserve(contour.size());
    for (std::size_t i = 0; i < contour.size(); ++i) {
        const cv::Point& from = contour[i];
        const cv::Point& to = contour[(i + 1) % contour.size()];
        dense.push_back(from);
        const double length = cv::norm(to - from);
        const int pieces = static_cast<int>(length / std::max(step, 0.5));
        for (int k = 1; k < pieces; ++k) {
            const double t = static_cast<double>(k) / pieces;
            dense.emplace_back(
                static_cast<int>(std::lround(from.x + (to.x - from.x) * t)),
                static_cast<int>(std::lround(from.y + (to.y - from.y) * t)));
        }
    }
    return dense;
}

// El mismo relleno de puntos, pero en coma flotante y sin redondear.
//
// Hace falta su propia versión: `densify` redondea cada punto intermedio al
// entero, y pasar por ahí un contorno afinado a décimas de píxel tiraría
// exactamente lo que se acaba de ganar.
std::vector<cv::Point2f> densifyFloat(const std::vector<cv::Point2f>& contour,
                                      double step = 1.0) {
    if (contour.size() < 2) {
        return contour;
    }
    std::vector<cv::Point2f> dense;
    dense.reserve(contour.size());
    for (std::size_t i = 0; i < contour.size(); ++i) {
        const cv::Point2f& from = contour[i];
        const cv::Point2f& to = contour[(i + 1) % contour.size()];
        dense.push_back(from);
        const double length = cv::norm(to - from);
        const int pieces = static_cast<int>(length / std::max(step, 0.5));
        for (int k = 1; k < pieces; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(pieces);
            dense.emplace_back(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
        }
    }
    return dense;
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
                         const ClassifyOptions& options,
                         const std::vector<cv::Point2f>* subpixel) {
    ShapeClass shape;
    // Con menos de ocho puntos no hay forma que reconocer, solo ruido con
    // ínfulas. Devolver «irregular» aquí no es rendirse: es lo correcto, y
    // hace que quien llame siga midiendo como venía midiendo.
    if (contour.size() < 8) {
        shape.reason = "el contorno tiene muy pocos puntos para reconocer una figura";
        return shape;
    }

    // Todo lo que sigue mide DISTANCIAS de los puntos del contorno a un modelo,
    // así que necesita puntos a lo largo de los lados y no solo en las esquinas.
    const std::vector<cv::Point> dense = densify(contour);
    // Las medidas de DISTANCIA salen del contorno afinado cuando lo hay: el
    // ajuste de circunferencia, la redondez y cuánto se separa el contorno de su
    // modelo son justo donde media décima de píxel se nota. El ajuste de
    // polígono se queda con el contorno entero unas líneas más abajo, porque sus
    // vértices son puntos del contorno y no posiciones interpoladas.
    const bool useSubpixel = subpixel != nullptr && subpixel->size() == contour.size() &&
                             subpixel->size() >= 8;
    const std::vector<cv::Point2f> points =
        useSubpixel ? densifyFloat(*subpixel) : asFloat(dense);

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
    cv::minEnclosingCircle(dense, enclosingCentre, enclosingRadius);
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
    // Y LO LARGOS QUE SON, que es lo que distingue una esquina de un lado mal
    // leído. Ver el comentario de la condición, más abajo.
    std::vector<double> lineLengths;
    std::vector<double> arcLengths;
    // El PEOR residuo de las primitivas, que es el que se va a publicar como
    // desviación si esto resulta ser un polígono redondeado.
    double worstResidual = 0.0;
    for (const auto& primitive : decomposeContour(dense, decomposeOptionsFor(dense))) {
        totalLength += primitive.length;
        worstResidual = std::max(worstResidual, primitive.rmsResidual);
        if (primitive.kind == PrimitiveKind::Line) {
            ++straight;
            lineLengths.push_back(primitive.length);
        } else {
            ++arcs;
            arcLength += primitive.length;
            arcLengths.push_back(primitive.length);
        }
    }
    const double curvedFraction = totalLength > 0.0 ? arcLength / totalLength : 0.0;

    // LOS ARCOS DE UN POLÍGONO REDONDEADO SON SUS ESQUINAS, y una esquina es
    // más corta que el lado al que pertenece. Si el arco mide más que el lado,
    // no es una esquina: es un lado que la descomposición ha leído curvo.
    //
    // Esta condición faltaba y es la que separa de verdad. Medido:
    //
    //     rectángulo 300x200 redondeo 20    4 rectas 245,7   4 arcos  34,4   0,14
    //     rectángulo 300x200 redondeo 60    4 rectas 168,0   4 arcos 100,3   0,60
    //     rectángulo 800x600 redondeo 30    4 rectas 733,7   4 arcos  52,3   0,07
    //     ---------------------------------------------------------------------
    //     polígono de 12 lados, radio 100   5 rectas  33,6   4 arcos 120,0   3,57
    //     polígono de 16 lados, radio 200  12 rectas  75,3   3 arcos 182,2   2,42
    //     tuerca de la bandeja              1 recta   41,3   6 arcos  40,3   0,97
    //
    // Los redondeados de verdad van de 0,07 a 0,60 y los mal leídos de 0,97 a
    // 3,57. El corte en 0,75 cae en un hueco ancho.
    //
    // Se compara con MEDIANAS y no con medias: un solo tramo raro —la costura
    // del contorno, un trozo de ruido— arrastraría la media y decidiría por
    // toda la pieza.
    const auto medianOf = [](std::vector<double> values) {
        if (values.empty()) {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    };
    const double medianLine = medianOf(lineLengths);
    const double medianArc = medianOf(arcLengths);
    const bool arcsAreCorners =
        medianLine > 0.0 && medianArc < kArcIsACornerBelow * medianLine;
    // Y TANTOS ARCOS COMO LADOS: cada esquina de un polígono redondeado es un
    // arco, así que las cuentas cuadran o no es un polígono redondeado.
    //
    // Sola, la condición de esquina corta no bastaba: se midió y el polígono de
    // 16 lados con antialiasing daba un cociente arco/lado de 0,62 mientras los
    // rectángulos redondeados legítimos llegan a 0,60. Se solapan, y ningún
    // umbral sobre ese número puede separarlos. Contar sí: el rectángulo da 4
    // y 4, y el dodecágono mal leído 4 arcos con 5 rectas.
    //
    // Esta rama se pregunta la PRIMERA, así que lo que se cuele aquí ya no
    // llega a las de círculo ni polígono. Por eso pide las dos cosas.
    if (straight >= 3 && straight <= scaled.maxSides && arcs == straight &&
        curvedFraction >= 0.10 && arcsAreCorners) {
        shape.kind = ShapeKind::Rounded;
        shape.sides = straight;
        // EL RESIDUO DE VERDAD, no un cero puesto a mano.
        //
        // Aquí había `shape.deviation = 0.0`, y eso incumplía lo que promete la
        // propia cabecera de este campo: «es el número con el que se decidió», y
        // «una clasificación sin su residuo es una opinión». Un cero no dice
        // «no aplica»: dice «ajuste exacto», que es la lectura contraria a la
        // verdadera y la que invita a fiarse.
        //
        // Se veía en las sondas sobre piezas reales: engranajes y tornillos
        // salían como «polígono redondeado(5) desv 0,00 px», o sea con la
        // etiqueta más discutible del clasificador y el residuo más tranquilo
        // que existe.
        //
        // El residuo sí está disponible: cada primitiva del contorno trae el
        // suyo. Se toma el PEOR, que es lo que hacen las otras dos ramas —el
        // punto que más se separa del modelo— y no la media, que escondería
        // un tramo malo entre veinte buenos.
        shape.deviation = worstResidual;
        shape.reason = "contorno de " + std::to_string(straight) + " tramos rectos unidos por " +
                       std::to_string(arcs) + " redondeos (" +
                       round0(curvedFraction * 100.0) + " % del contorno va en curva; el " +
                       "tramo peor se separa " + roundFine(worstResidual) + " px de su modelo)";
        return shape;
    }

    // --- ¿Es una circunferencia? ------------------------------------------
    const CircleFit circle = fitCircleTaubin(points);
    double circleDeviation = std::numeric_limits<double>::infinity();
    if (circle.valid && circle.radius > 0.0) {
        circleDeviation = worstRadialDeviation(points, circle.center, circle.radius);
    }

    // --- ¿Es un polígono? --------------------------------------------------
    const PolygonFit polygon = fitPolygon(dense, scaled);

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
    // Una circunferencia no puede ser MUCHO mayor que la pieza a la que se
    // ajusta, y comprobarlo hacia falta.
    //
    // Lo destapo una fotografia real: un contorno largo y casi recto —una regla
    // metalica que la segmentacion tomo por la pieza— ajusto una circunferencia
    // de Ø 130.901 px en una imagen de 1920 px de ancho. Sesenta y ocho veces
    // mas ancha que la foto, publicada con un motivo que la explicaba con toda
    // seguridad.
    //
    // La causa es que la vara de abajo es RELATIVA AL RADIO AJUSTADO: cuanto
    // mas absurdo es el circulo, mas grande es su radio y mas facil le resulta
    // pasar el 5 %. Se justifica a si mismo. Con Ø 130.901 px, el 5 % son 3.272
    // px de margen, y el contorno se separaba 175: aprobado sin despeinarse.
    //
    // El limite lo pone la propia pieza. Para una circunferencia de verdad, el
    // diametro es la diagonal de su caja partido por raiz de dos (0,71 veces);
    // para cualquier silueta cerrada razonable se queda por debajo de la
    // diagonal. Se admite hasta el DOBLE, que es holgadisimo, y aun asi deja
    // fuera el caso de la regla por un factor de cincuenta.
    //
    // Solo afecta a este camino —el del contorno "demasiados lados para
    // medirlos uno a uno"—. Un ajuste que cae dentro de la tolerancia absoluta
    // (`circleFits`) ya esta acotado por ella y no necesita esto.
    const cv::Rect contourBox = cv::boundingRect(points);
    const double boxDiagonal = std::hypot(static_cast<double>(contourBox.width),
                                          static_cast<double>(contourBox.height));
    const bool circleFitsInsideThePiece =
        boxDiagonal > 0.0 && 2.0 * circle.radius <= 2.0 * boxDiagonal;

    const bool roundEnoughForItsSize = circle.valid && circle.radius > 0.0 &&
                                       circleFitsInsideThePiece &&
                                       circleDeviation <= circle.radius * 0.05;
    const bool polygonisedCurve =
        !polygonFits && polygon.unlimitedSides > scaled.maxSides && roundEnoughForItsSize;

    if (polygonFits && (!circleFits || polygon.deviation <= circleDeviation)) {
        shape.kind = ShapeKind::Polygon;
        shape.sides = static_cast<int>(polygon.vertices.size());
        shape.deviation = polygon.deviation;
        shape.reason = "contorno de " + std::to_string(shape.sides) +
                       " lados rectos (el punto peor se separa " +
                       roundFine(polygon.deviation) + " px de ellos)";
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
                      " px (el punto peor se separa " + roundFine(circleDeviation) + " px)"
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
