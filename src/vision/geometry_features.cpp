#include "vision/geometry_features.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

#include "vision/fitting.h"

namespace pci::vision {

namespace {

constexpr double kRadToDeg = 57.29577951308232;

double polylineLength(const std::vector<cv::Point2f>& points, std::size_t from,
                      std::size_t to) {
    double total = 0.0;
    for (std::size_t i = from; i + 1 <= to && i + 1 < points.size(); ++i) {
        total += cv::norm(points[i + 1] - points[i]);
    }
    return total;
}

// Ajuste de un tramo: qué primitiva lo explica y con qué residuo.
struct SegmentFit {
    PrimitiveKind kind = PrimitiveKind::Line;
    double residual = 1e9;
    cv::Point2f center{0.0F, 0.0F};
    double radius = 0.0;
    double sweepDeg = 0.0;
    bool valid = false;
};

// Decide si un tramo se explica mejor por una recta o por un arco.
//
// El arco tiene que ganar con holgura y ademas describir un arco DE VERDAD. Sin
// esas dos condiciones, un lado recto se "explica" con una circunferencia de
// radio enorme que ajusta un pelo mejor por el dentado de la rasterizacion: al
// probarlo, dos lados rectos de 99 px de una pieza en L salian como arcos de
// radio 923 y 865 px. Un radio asi sobre un tramo de 99 px es una recta.
SegmentFit fitSegment(const std::vector<cv::Point2f>& points, std::size_t from,
                      std::size_t to, const DecomposeOptions& options) {
    SegmentFit best;
    if (to <= from || to >= points.size()) {
        return best;
    }
    const std::vector<cv::Point2f> slice(points.begin() + static_cast<std::ptrdiff_t>(from),
                                         points.begin() + static_cast<std::ptrdiff_t>(to) + 1);
    if (slice.size() < 3) {
        return best;
    }

    const LineFit line = fitLineTotal(slice);
    if (line.valid) {
        best.kind = PrimitiveKind::Line;
        best.residual = line.rmsResidual;
        best.valid = true;
    }

    const CircleFit circle = fitCircleTaubin(slice);
    if (circle.valid && circle.radius > 1.0) {
        const double arcLength = polylineLength(points, from, to);
        // Angulo barrido, sacado del propio circulo ajustado: longitud del arco
        // partida por su radio.
        //
        // Esta sola condicion ya descarta los arcos falsos de radio enorme: un
        // barrido de 15 grados equivale a radio <= 3,8 veces el largo del tramo,
        // asi que un lado recto de 99 px no puede pasar por arco de radio 900.
        // Se probo ademas con una guarda "radio <= 8 x cuerda" y hubo que
        // quitarla: en un contorno CERRADO la cuerda entre el primer y el ultimo
        // punto es casi cero, y un disco entero se rechazaba como arco.
        const double sweep = arcLength / circle.radius * kRadToDeg;
        const bool bigEnoughSweep = sweep >= options.minArcSweepDeg;
        // Y tiene que ajustar apreciablemente mejor, no por un pelo.
        const bool clearlyBetter =
            !best.valid || circle.rmsResidual < 0.8 * best.residual;
        if (bigEnoughSweep && clearlyBetter) {
            best.kind = PrimitiveKind::Arc;
            best.residual = circle.rmsResidual;
            best.center = circle.center;
            best.radius = circle.radius;
            best.sweepDeg = sweep;
            best.valid = true;
        }
    }
    return best;
}

ContourPrimitive makePrimitive(const std::vector<cv::Point2f>& points, std::size_t from,
                               std::size_t to, const DecomposeOptions& options) {
    const SegmentFit fit = fitSegment(points, from, to, options);
    ContourPrimitive primitive;
    primitive.kind = fit.valid ? fit.kind : PrimitiveKind::Line;
    primitive.start = points[from];
    primitive.end = points[to];
    primitive.mid = points[(from + to) / 2];
    primitive.rmsResidual = fit.valid ? fit.residual : 0.0;
    primitive.length = polylineLength(points, from, to);
    if (primitive.kind == PrimitiveKind::Arc) {
        primitive.center = fit.center;
        primitive.radius = fit.radius;
        primitive.sweepDeg = fit.sweepDeg;
        // Reajuste robusto del arco. El barrido voraz suele pasarse un poco
        // hacia el tramo recto tangente que viene despues, y esa cola aplana el
        // ajuste: el radio salia hasta un 40 % alto. Los puntos de la cola no
        // pertenecen a la circunferencia, asi que la reponderacion los deja
        // fuera y devuelve el radio del redondeo de verdad.
        const std::vector<cv::Point2f> slice(
            points.begin() + static_cast<std::ptrdiff_t>(from),
            points.begin() + static_cast<std::ptrdiff_t>(to) + 1);
        const CircleFit refined = fitCircleRobust(slice);
        if (refined.valid && refined.inlierCount >= 5) {
            primitive.center = refined.center;
            primitive.radius = refined.radius;
            primitive.rmsResidual = refined.rmsResidual;
            primitive.sweepDeg = primitive.length / refined.radius * kRadToDeg;
        }
    }
    return primitive;
}

// Suavizado circular del contorno remuestreado: media movil corta.
//
// Hace falta y no es cosmetico. El contorno de una mascara va de pixel en pixel
// y trae el dentado de la rasterizacion; sin suavizar, un lado PERFECTAMENTE
// recto daba residuo 1,2-1,4 px, es decir, al nivel de la tolerancia. Con el
// suelo de ruido tan alto no se puede distinguir un rasgo limpio de una mezcla
// de recta y arco, y el barrido se comia las transiciones.
std::vector<cv::Point2f> smoothClosed(const std::vector<cv::Point2f>& points, int window) {
    const std::size_t n = points.size();
    if (n < 8 || window < 1) {
        return points;
    }
    std::vector<cv::Point2f> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        cv::Point2f sum(0.0F, 0.0F);
        for (int k = -window; k <= window; ++k) {
            sum += points[(i + n + static_cast<std::size_t>(k + static_cast<int>(n))) % n];
        }
        out[i] = sum / static_cast<float>(2 * window + 1);
    }
    return out;
}

// Hasta donde puede crecer un tramo que empieza en `from` sin pasarse del
// residuo permitido. Se busca el corte por biseccion: se dobla el largo
// mientras aguante y luego se afina, en vez de probar punto a punto.
std::size_t growSegment(const std::vector<cv::Point2f>& points, std::size_t from,
                        std::size_t limit, const DecomposeOptions& options) {
    const auto minEnd = std::min(limit, from + static_cast<std::size_t>(options.minPoints));
    std::size_t good = minEnd;
    std::size_t step = static_cast<std::size_t>(options.minPoints);

    while (good + step <= limit) {
        const SegmentFit fit = fitSegment(points, from, good + step, options);
        if (!fit.valid || fit.residual > options.maxResidual) {
            break;
        }
        good += step;
        step *= 2;
    }
    // Afinado: el ultimo salto se reduce a la mitad hasta el paso minimo.
    step = std::max<std::size_t>(1, step / 2);
    while (step >= 1) {
        while (good + step <= limit) {
            const SegmentFit fit = fitSegment(points, from, good + step, options);
            if (!fit.valid || fit.residual > options.maxResidual) {
                break;
            }
            good += step;
        }
        if (step == 1) {
            break;
        }
        step /= 2;
    }
    return std::min(good, limit);
}

}  // namespace

std::vector<cv::Point2f> resampleClosedContour(const std::vector<cv::Point>& contour,
                                               double step) {
    std::vector<cv::Point2f> out;
    if (contour.size() < 3 || !(step > 0.0)) {
        return out;
    }
    // Perímetro total, cerrando el contorno.
    double perimeter = 0.0;
    for (std::size_t i = 0; i < contour.size(); ++i) {
        perimeter += cv::norm(contour[(i + 1) % contour.size()] - contour[i]);
    }
    if (perimeter < step * 3.0) {
        return out;
    }

    const auto count = static_cast<std::size_t>(perimeter / step);
    out.reserve(count + 1);
    double target = 0.0;
    double walked = 0.0;
    std::size_t segment = 0;
    cv::Point2f current = contour[0];
    for (std::size_t k = 0; k <= count; ++k) {
        target = step * k;
        while (segment < contour.size()) {
            const cv::Point2f a = contour[segment];
            const cv::Point2f b = contour[(segment + 1) % contour.size()];
            const double len = cv::norm(b - a);
            if (walked + len >= target || segment + 1 == contour.size()) {
                const double t = len > 1e-9 ? (target - walked) / len : 0.0;
                current = a + (b - a) * static_cast<float>(std::clamp(t, 0.0, 1.0));
                break;
            }
            walked += len;
            ++segment;
        }
        out.push_back(current);
    }
    return out;
}

// Afina las fronteras entre tramos. El barrido voraz corta DESPUES de la
// transicion real -sigue creciendo hasta que el residuo se pasa-, asi que cada
// tramo se lleva un trozo del siguiente. En un redondeo eso aplana el ajuste y
// el radio salia hasta un 40 % alto. Mover cada frontera al punto que minimiza
// la suma de los residuos de sus dos tramos las devuelve a su sitio.
void refineBoundaries(const std::vector<cv::Point2f>& points,
                      std::vector<std::size_t>& bounds, const DecomposeOptions& options) {
    if (bounds.size() < 3) {
        return;
    }
    const auto reach = static_cast<std::size_t>(options.minPoints);
    for (int pass = 0; pass < 3; ++pass) {
        bool moved = false;
        for (std::size_t i = 1; i + 1 < bounds.size(); ++i) {
            const std::size_t a = bounds[i - 1];
            const std::size_t c = bounds[i + 1];
            const std::size_t lo = std::max(a + reach, bounds[i] > reach ? bounds[i] - reach : a + reach);
            const std::size_t hi = std::min(c - reach, bounds[i] + reach);
            if (lo >= hi) {
                continue;
            }
            std::size_t best = bounds[i];
            double bestCost = 1e18;
            for (std::size_t b = lo; b <= hi; ++b) {
                const SegmentFit left = fitSegment(points, a, b, options);
                const SegmentFit right = fitSegment(points, b, c, options);
                if (!left.valid || !right.valid) {
                    continue;
                }
                const double cost = left.residual + right.residual;
                if (cost < bestCost) {
                    bestCost = cost;
                    best = b;
                }
            }
            if (best != bounds[i]) {
                bounds[i] = best;
                moved = true;
            }
        }
        if (!moved) {
            break;
        }
    }
}

std::vector<ContourPrimitive> decomposeContour(const std::vector<cv::Point>& contour,
                                               const DecomposeOptions& options) {
    std::vector<ContourPrimitive> primitives;
    const std::vector<cv::Point2f> raw =
        resampleClosedContour(contour, options.resampleStep);
    if (static_cast<int>(raw.size()) < options.minPoints * 3) {
        return primitives;
    }
    const std::vector<cv::Point2f> points = smoothClosed(raw, 2);

    // Barrido voraz: cada tramo crece mientras una sola primitiva lo explique, y
    // se corta donde deja de hacerlo. Esa rotura ES la transicion entre rasgos,
    // asi que encuentra por igual una esquina viva y la union tangente de una
    // recta con un redondeo -donde no hay esquina que detectar, solo cambia la
    // curvatura-.
    //
    // Se probo antes con particion recursiva por el punto de peor ajuste y se
    // descarto: era mas dificil de razonar y dejaba tramos de 541 px con
    // residuo 11 sin partir.
    const std::size_t last = points.size() - 1;
    std::vector<std::size_t> bounds{0};
    std::size_t from = 0;
    while (from < last) {
        const std::size_t to = growSegment(points, from, last, options);
        if (to <= from) {
            break;
        }
        bounds.push_back(to);
        from = to;
    }
    refineBoundaries(points, bounds, options);

    for (std::size_t i = 0; i + 1 < bounds.size(); ++i) {
        primitives.push_back(makePrimitive(points, bounds[i], bounds[i + 1], options));
    }

    // El contorno CIERRA, pero el barrido es lineal: empieza en un punto
    // cualquiera y al dar la vuelta deja un muñón en la costura. En un disco eso
    // se veía como "un arco de 701 px con R=119,5 y otro de 49 px con R=112,2",
    // cuando lo que hay es una sola circunferencia.
    //
    // El criterio para fundirlos es el único honesto: se ajusta la UNIÓN de los
    // dos y se acepta solo si sigue explicándose con una primitiva. Se probó
    // antes con heurísticas por clase -"dos rectas cuyos extremos se tocan"- y
    // era un error: en un contorno cerrado los extremos SIEMPRE se tocan en la
    // costura, así que fundía dos lados perpendiculares de un rectángulo.
    if (primitives.size() >= 3 && bounds.size() >= 3) {
        std::vector<cv::Point2f> wrapped(
            points.begin() + static_cast<std::ptrdiff_t>(bounds[bounds.size() - 2]),
            points.end());
        wrapped.insert(wrapped.end(), points.begin(),
                       points.begin() + static_cast<std::ptrdiff_t>(bounds[1]) + 1);
        if (wrapped.size() >= 3) {
            const SegmentFit merged = fitSegment(wrapped, 0, wrapped.size() - 1, options);
            if (merged.valid && merged.residual <= options.maxResidual) {
                primitives.front() = makePrimitive(wrapped, 0, wrapped.size() - 1, options);
                primitives.pop_back();
            }
        }
    }
    return primitives;
}

std::vector<std::vector<cv::Point>> findHoles(const cv::Mat& mask, double minAreaPx) {
    std::vector<std::vector<cv::Point>> holes;
    if (mask.empty() || mask.type() != CV_8UC1) {
        return holes;
    }
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    // RETR_CCOMP deja los contornos externos en el nivel 0 y sus huecos en el 1:
    // un contorno con padre ES un agujero.
    cv::findContours(mask, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE);
    for (std::size_t i = 0; i < contours.size(); ++i) {
        if (hierarchy[i][3] < 0) {
            continue;  // sin padre: es el borde exterior de la pieza
        }
        if (std::abs(cv::contourArea(contours[i])) < minAreaPx) {
            continue;
        }
        holes.push_back(contours[i]);
    }
    return holes;
}

ContourReport describeContour(const cv::Mat& mask, const DecomposeOptions& options) {
    ContourReport report;
    if (mask.empty() || mask.type() != CV_8UC1) {
        return report;
    }

    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(mask, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE);

    // Una sola llamada a findContours para el exterior Y los agujeros: los
    // agujeros que se devuelven son los HIJOS del contorno elegido, no todos los
    // huecos de la máscara. Con dos llamadas (o reutilizando findHoles) una
    // pieza pequeña con un agujero al lado de la principal aportaría su hueco al
    // área de la grande.
    int best = -1;
    double bestArea = 0.0;
    for (std::size_t i = 0; i < contours.size(); ++i) {
        if (hierarchy[i][3] >= 0) {
            continue;  // tiene padre: es un agujero, no una pieza
        }
        const double area = std::abs(cv::contourArea(contours[i]));
        if (area > bestArea) {
            bestArea = area;
            best = static_cast<int>(i);
        }
    }
    if (best < 0 || contours[static_cast<std::size_t>(best)].size() < 3) {
        return report;
    }

    report.outer = contours[static_cast<std::size_t>(best)];
    report.perimeter = cv::arcLength(report.outer, true);
    report.area = bestArea;
    report.bounds = cv::boundingRect(report.outer);
    report.minRect = cv::minAreaRect(report.outer);

    for (std::size_t i = 0; i < contours.size(); ++i) {
        if (hierarchy[i][3] != best) {
            continue;
        }
        const double area = std::abs(cv::contourArea(contours[i]));
        if (area < 40.0 || contours[i].size() < 3) {
            continue;  // ruido de la segmentación, no un agujero
        }
        report.holes.push_back(contours[i]);
        report.area -= area;
    }

    report.primitives = decomposeContour(report.outer, options);
    report.valid = true;
    return report;
}

std::string contourToCsv(const ContourReport& report, double mmPerPixel) {
    std::ostringstream out;
    // Locale clásico a la fuerza: en un Windows en español el separador decimal
    // por defecto es la coma, y un CSV con "12,50" en una columna separada por
    // comas no lo abre nadie.
    out.imbue(std::locale::classic());

    const bool inMm = mmPerPixel > 0.0;
    const double scale = inMm ? mmPerPixel : 1.0;
    const int decimals = inMm ? 4 : 2;
    out << "contorno,punto,x_" << (inMm ? "mm" : "px") << ",y_" << (inMm ? "mm" : "px") << '\n';
    if (!report.valid) {
        return out.str();
    }
    out << std::fixed << std::setprecision(decimals);

    const auto emit = [&](const std::string& name, const std::vector<cv::Point>& points) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            out << name << ',' << i << ',' << points[i].x * scale << ','
                << points[i].y * scale << '\n';
        }
    };
    emit("exterior", report.outer);
    for (std::size_t h = 0; h < report.holes.size(); ++h) {
        emit("agujero_" + std::to_string(h + 1), report.holes[h]);
    }
    return out.str();
}

}  // namespace pci::vision
