// SONDA temporal: solo imprime números para fijar después las cotas medidas.
#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "vision/fitting.h"
#include "vision/shape_class.h"

using pci::vision::ClassifyOptions;
using pci::vision::classifyShape;
using pci::vision::ShapeClass;
using pci::vision::ShapeKind;
using pci::vision::shapeKindName;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Rasteriza una polilínea cerrada con subpíxel (shift = 1/8 px) para que el
// contorno no herede el error de redondear los vértices a entero.
cv::Mat rasterize(const std::vector<cv::Point2d>& path, int size) {
    cv::Mat mask(size, size, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point> scaled;
    scaled.reserve(path.size());
    for (const auto& p : path) {
        scaled.emplace_back(static_cast<int>(std::lround(p.x * 8.0)),
                            static_cast<int>(std::lround(p.y * 8.0)));
    }
    cv::fillPoly(mask, scaled, cv::Scalar(255), cv::LINE_AA, 3);
    return mask > 127;
}

std::vector<cv::Point2d> circlePath(cv::Point2d c, double r, double stepPx = 2.0) {
    const auto n = std::max<int>(16, static_cast<int>(2.0 * kPi * r / stepPx));
    std::vector<cv::Point2d> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * kPi * i / n;
        out.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
    }
    return out;
}

// Polígono regular muestreado DENSO: los vértices siguen siendo exactos porque
// el muestreo de cada lado empieza en él.
std::vector<cv::Point2d> polygonPath(cv::Point2d c, double r, int sides, double rotDeg = 0.0,
                                     double stepPx = 2.0) {
    std::vector<cv::Point2d> out;
    for (int i = 0; i < sides; ++i) {
        const double a0 = rotDeg * kPi / 180.0 + 2.0 * kPi * i / sides;
        const double a1 = rotDeg * kPi / 180.0 + 2.0 * kPi * (i + 1) / sides;
        const cv::Point2d v0{c.x + r * std::cos(a0), c.y + r * std::sin(a0)};
        const cv::Point2d v1{c.x + r * std::cos(a1), c.y + r * std::sin(a1)};
        const double len = cv::norm(v1 - v0);
        const auto steps = std::max<int>(1, static_cast<int>(len / stepPx));
        for (int k = 0; k < steps; ++k) {
            const double t = static_cast<double>(k) / steps;
            out.push_back({v0.x + (v1.x - v0.x) * t, v0.y + (v1.y - v0.y) * t});
        }
    }
    return out;
}

// Diente de sierra radial de amplitud `amp` px (pico a valle 2·amp).
std::vector<cv::Point2d> withSawtooth(const std::vector<cv::Point2d>& path, cv::Point2d c,
                                      double amp, int period = 2) {
    std::vector<cv::Point2d> out;
    out.reserve(path.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        const cv::Point2d d = path[i] - c;
        const double len = std::hypot(d.x, d.y);
        const double sign = ((i / static_cast<std::size_t>(period)) % 2 == 0) ? 1.0 : -1.0;
        const double k = len > 1e-9 ? (len + sign * amp) / len : 1.0;
        out.push_back({c.x + d.x * k, c.y + d.y * k});
    }
    return out;
}

std::vector<cv::Point> outerContour(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> found;
    cv::findContours(mask, found, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    std::size_t best = 0;
    double bestArea = -1.0;
    for (std::size_t i = 0; i < found.size(); ++i) {
        const double area = std::abs(cv::contourArea(found[i]));
        if (area > bestArea) {
            bestArea = area;
            best = i;
        }
    }
    return found.empty() ? std::vector<cv::Point>{} : found[best];
}

ShapeClass classifyMask(const cv::Mat& mask, const ClassifyOptions& options = {}) {
    return classifyShape(outerContour(mask), mask, options);
}

// Cuánto se separa de verdad el contorno rasterizado de la circunferencia
// ideal: es el ruido REAL inyectado, no el que se pidió.
double worstRadial(const std::vector<cv::Point>& contour) {
    std::vector<cv::Point2f> pts;
    pts.reserve(contour.size());
    for (const auto& p : contour) {
        pts.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
    }
    const auto fit = pci::vision::fitCircleTaubin(pts);
    if (!fit.valid) {
        return -1.0;
    }
    double worst = 0.0;
    for (const auto& p : pts) {
        worst = std::max(worst, std::abs(cv::norm(p - fit.center) - fit.radius));
    }
    return worst;
}

cv::Mat washerMask(int size, double outer, double inner, cv::Point2d holeOffset = {0.0, 0.0}) {
    cv::Mat mask = rasterize(circlePath({size / 2.0, size / 2.0}, outer), size);
    const cv::Point2d hc{size / 2.0 + holeOffset.x, size / 2.0 + holeOffset.y};
    std::vector<cv::Point> scaled;
    for (const auto& p : circlePath(hc, inner)) {
        scaled.emplace_back(static_cast<int>(std::lround(p.x * 8.0)),
                            static_cast<int>(std::lround(p.y * 8.0)));
    }
    cv::fillPoly(mask, scaled, cv::Scalar(0), cv::LINE_AA, 3);
    return mask > 127;
}

void row(const char* what, const ShapeClass& s) {
    std::printf("%-28s %-20s lados=%2d desv=%6.2f Øext=%7.2f Øint=%7.2f | %s\n", what,
                shapeKindName(s.kind), s.sides, s.deviation, s.outerDiameter, s.innerDiameter,
                s.reason.c_str());
}

}  // namespace

// La pieza llega a la mesa girada como le da la gana, así que una clase que
// dependa del giro no sirve. Se barre el giro entero de 5 en 5 y se EXIGE la
// misma respuesta en los 19 pasos.
//
// Esto encontró un fallo de verdad: eligiendo «el primer epsilon que cumple» en
// vez de la meseta, un hexágono salía de 6 lados a 0°, de 7 a 10° y de 8 a 15°.
TEST(ShapeClassProbe, Rotacion) {
    for (int n : {3, 4, 5, 6, 8, 12}) {
        std::printf("n=%2d :", n);
        for (int deg = 0; deg <= 90; deg += 5) {
            const auto s = classifyMask(rasterize(polygonPath({250, 250}, 160, n, deg), 500));
            std::printf(" %d/%.1f", s.kind == ShapeKind::Polygon ? s.sides : -1, s.deviation);
            // Hasta seis lados se exige la respuesta exacta en los 19 giros.
            //
            // De ocho para arriba NO se exige, y el motivo está medido: un
            // dodecágono de radio 160 se separa solo 5,5 px de su propia
            // circunferencia, así que a ciertos ángulos gana el círculo. No es
            // un fallo que arreglar sino una ambigüedad real —a ese tamaño un
            // dodecágono ES casi un círculo— y forzar una respuesta ahí sería
            // fijar por contrato el ruido del rasterizado. Lo que hace falta
            // para contarle los lados es verlo más grande, y eso lo mide el
            // barrido de escala.
            if (n <= 6) {
                EXPECT_EQ(s.kind, ShapeKind::Polygon) << n << " lados girado " << deg << "°";
                EXPECT_EQ(s.sides, n) << n << " lados girado " << deg << "°: contó " << s.sides;
            }
        }
        std::printf("\n");
    }
}

TEST(ShapeClassProbe, Traslacion) {
    for (int n : {4, 6}) {
        for (cv::Point2d c : {cv::Point2d{250, 250}, cv::Point2d{180, 320}, cv::Point2d{171, 171},
                              cv::Point2d{330, 250}}) {
            const auto s = classifyMask(rasterize(polygonPath(c, 160, n, 11.0), 500));
            std::printf("n=%d centro=(%.0f,%.0f) -> %s lados=%d desv=%.2f\n", n, c.x, c.y,
                        shapeKindName(s.kind), s.sides, s.deviation);
        }
    }
}

// La escala, que es la invariancia que más se rompe sola. Aquí salieron DOS
// fallos reales:
//
// - Con el paso de remuestreo fijo en 2 px, un hexágono de radio 40 salía de
//   «4 rectas y 2 arcos»: 128 muestras no dan para resolver seis esquinas. Se
//   arregló manteniendo constante el NÚMERO de muestras.
// - Con la tolerancia fija en 6 px, un decágono de radio 400 salía «círculo»:
//   colocar un vértice un par de píxeles antes de la esquina inclina el lado
//   entero, y ese error SÍ crece con la pieza. Se arregló con un término
//   relativo del 2,5 % del radio.
//
// Y queda un límite que NO es un fallo y por eso se afirma tal cual: cuantos
// más lados tiene la pieza, más grande hay que verla para distinguirlos. Con
// 3 y 6 lados basta un radio de 12 px; con 10 o 12 hace falta llegar a 100.
// Por debajo salen «redondas», que es la respuesta honesta —a 24 px de ancho un
// dodecágono ES un círculo— y encima la buena: se le mide el diámetro.
TEST(ShapeClassProbe, Escala) {
    for (int n : {3, 4, 5, 6, 8, 10, 12}) {
        std::printf("n=%2d :", n);
        for (double r : {12.0, 18.0, 25.0, 35.0, 50.0, 100.0, 200.0, 400.0}) {
            const int size = static_cast<int>(r * 2.6) + 20;
            const auto s = classifyMask(rasterize(polygonPath({size / 2.0, size / 2.0}, r, n), size));
            std::printf("  r%.0f:%s/%d/%.2f", r, shapeKindName(s.kind),
                        s.kind == ShapeKind::Polygon ? s.sides : 0, s.deviation);
            // Pocos lados: la esquina es profunda y se resuelve en cuanto la
            // pieza mide 70 px de ancho. Medido: por debajo, un pentágono de
            // radio 25 se lee como cuadrado — a 50 px de ancho, una esquina de
            // 108° cabe en tres píxeles.
            if (n <= 6 && r >= 35.0) {
                EXPECT_EQ(s.kind, ShapeKind::Polygon) << n << " lados, radio " << r;
                EXPECT_EQ(s.sides, n) << n << " lados, radio " << r << ": contó " << s.sides;
            }
            // Muchos lados: se exige a partir de radio 100, que es donde la
            // medida dice que la esquina ya se ve.
            if (n > 6 && r >= 100.0) {
                EXPECT_EQ(s.kind, ShapeKind::Polygon) << n << " lados, radio " << r;
                EXPECT_EQ(s.sides, n) << n << " lados, radio " << r << ": contó " << s.sides;
            }
        }
        std::printf("\n");
    }
    // Un disco es un disco desde 6 px de radio hasta 400, sin excepción: no hay
    // ninguna esquina que resolver.
    std::printf("disco:");
    for (double r : {6.0, 10.0, 25.0, 50.0, 200.0, 400.0}) {
        const int size = static_cast<int>(r * 2.6) + 20;
        const auto s = classifyMask(rasterize(circlePath({size / 2.0, size / 2.0}, r), size));
        std::printf("  r%.0f:%s/%.2f/Ø%.1f", r, shapeKindName(s.kind), s.deviation,
                    s.outerDiameter);
        EXPECT_EQ(s.kind, ShapeKind::Circle) << "disco de radio " << r;
        // Y el diámetro escala con la pieza, que es lo que de verdad se va a
        // medir. Sin esto la clase podría ser correcta y el número basura.
        EXPECT_NEAR(s.outerDiameter, 2.0 * r, std::max(2.0, 0.02 * r)) << "disco de radio " << r;
    }
    std::printf("\n");
}

TEST(ShapeClassProbe, Ruido) {
    for (double amp : {0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 6.0}) {
        auto disc = withSawtooth(circlePath({250, 250}, 150), {250, 250}, amp);
        auto hex = withSawtooth(polygonPath({250, 250}, 160, 6), {250, 250}, amp);
        auto sq = withSawtooth(polygonPath({250, 250}, 160, 4), {250, 250}, amp);
        const cv::Mat dm = rasterize(disc, 500);
        const cv::Mat hm = rasterize(hex, 500);
        const cv::Mat sm = rasterize(sq, 500);
        const auto ds = classifyMask(dm);
        const auto hs = classifyMask(hm);
        const auto ss = classifyMask(sm);
        std::printf("amp=%.1f real=%.2f | disco %s/%.2f | hex %s/%d/%.2f | cuad %s/%d/%.2f\n", amp,
                    worstRadial(outerContour(dm)), shapeKindName(ds.kind), ds.deviation,
                    shapeKindName(hs.kind), hs.sides, hs.deviation, shapeKindName(ss.kind),
                    ss.sides, ss.deviation);

        // Dónde está la frontera, medida: un disco aguanta dientes de sierra de
        // hasta 7,2 px de desviación radial sin dejar de ser un disco —tiene
        // sentido, porque el ruido no le cambia la FORMA— y un polígono aguanta
        // hasta 3,5 px, que es cuando el diente empieza a parecerse a una
        // esquina.
        //
        // Se afirma el lado bueno de la frontera. El otro lado no se afirma a
        // propósito: pasado ese ruido la respuesta correcta es «no lo sé», y
        // exigir una degradación concreta sería fijar por contrato el
        // comportamiento del ruido.
        EXPECT_EQ(ds.kind, ShapeKind::Circle) << "disco con dientes de " << amp;
        if (amp <= 2.5) {
            EXPECT_EQ(hs.kind, ShapeKind::Polygon) << "hexagono con dientes de " << amp;
            EXPECT_EQ(hs.sides, 6) << "hexagono con dientes de " << amp;
            EXPECT_EQ(ss.kind, ShapeKind::Polygon) << "cuadrado con dientes de " << amp;
            EXPECT_EQ(ss.sides, 4) << "cuadrado con dientes de " << amp;
        }
    }
}

TEST(ShapeClassProbe, FronteraLados) {
    for (int n = 3; n <= 40; ++n) {
        const auto s = classifyMask(rasterize(polygonPath({250, 250}, 160, n), 500));
        std::printf("n=%2d -> %-20s lados=%2d desv=%5.2f | %s\n", n, shapeKindName(s.kind), s.sides,
                    s.deviation, s.reason.c_str());
    }
}

TEST(ShapeClassProbe, FronteraElipse) {
    for (int pct = 100; pct <= 200; pct += 5) {
        const double ratio = pct / 100.0;
        cv::Mat mask(500, 500, CV_8UC1, cv::Scalar(0));
        cv::ellipse(mask, {250, 250}, {static_cast<int>(150 * ratio), 150}, 0, 0, 360,
                    cv::Scalar(255), cv::FILLED, cv::LINE_AA);
        mask = mask > 127;
        const auto s = classifyMask(mask);
        std::printf("ratio=%.2f -> %-20s desv=%6.2f | %s\n", ratio, shapeKindName(s.kind),
                    s.deviation, s.reason.c_str());
    }
}

TEST(ShapeClassProbe, Arandela) {
    std::printf("-- tamaño de agujero (Ø ext 320) --\n");
    for (double frac : {0.05, 0.10, 0.12, 0.14, 0.15, 0.16, 0.18, 0.20, 0.30, 0.50, 0.70}) {
        const double inner = 160.0 * frac;
        const auto s = classifyMask(washerMask(500, 160.0, inner));
        std::printf("frac(radio)=%.2f Øint nominal=%.1f -> %s Øint=%.1f | %s\n", frac, 2 * inner,
                    shapeKindName(s.kind), s.innerDiameter, s.reason.c_str());
    }
    std::printf("-- descentrado (agujero Ø 140) --\n");
    for (double off : {0.0, 5.0, 10.0, 15.0, 19.0, 20.0, 21.0, 25.0, 40.0}) {
        const auto s = classifyMask(washerMask(500, 160.0, 70.0, {off, 0.0}));
        std::printf("off=%5.1f (frac radio=%.3f) -> %-12s Øint=%.1f | %s\n", off, off / 160.0,
                    shapeKindName(s.kind), s.innerDiameter, s.reason.c_str());
    }
    std::printf("-- sin máscara --\n");
    const cv::Mat w = washerMask(500, 160.0, 70.0);
    const auto noMask = classifyShape(outerContour(w));
    row("arandela sin mascara", noMask);
}

// Réplica del barrido interno de fitPolygon, para ver POR QUÉ elige lo que elige.
namespace {

double segDist(cv::Point2d p, cv::Point2d a, cv::Point2d b) {
    const cv::Point2d ab = b - a;
    const double l2 = ab.x * ab.x + ab.y * ab.y;
    if (l2 < 1e-12) {
        return cv::norm(p - a);
    }
    double t = ((p - a).x * ab.x + (p - a).y * ab.y) / l2;
    t = std::clamp(t, 0.0, 1.0);
    return cv::norm(p - (a + ab * t));
}

double worstToPoly(const std::vector<cv::Point>& contour, const std::vector<cv::Point>& poly) {
    double worst = 0.0;
    for (const auto& p : contour) {
        double best = 1e18;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            best = std::min(best, segDist(cv::Point2d(p.x, p.y),
                                          cv::Point2d(poly[i].x, poly[i].y),
                                          cv::Point2d(poly[(i + 1) % poly.size()].x,
                                                      poly[(i + 1) % poly.size()].y)));
        }
        worst = std::max(worst, best);
    }
    return worst;
}

void dumpSweep(const char* what, const std::vector<cv::Point>& contour) {
    const double per = cv::arcLength(contour, true);
    std::printf("== %s (per=%.0f, %zu puntos)\n", what, per, contour.size());
    for (double f = 0.001; f <= 0.06; f += 0.002) {
        std::vector<cv::Point> approx;
        cv::approxPolyDP(contour, approx, f * per, true);
        if (approx.size() < 3) {
            std::printf("   f=%.3f -> %zu vertices (corta)\n", f, approx.size());
            break;
        }
        std::printf("   f=%.3f eps=%5.2f -> %2zu vertices, desv=%5.2f%s\n", f, f * per,
                    approx.size(), worstToPoly(contour, approx),
                    worstToPoly(contour, approx) <= 2.5 ? "  OK" : "");
    }
}

}  // namespace

// ¿Existe un epsilon que SÍ acierta? Para cada número de vértices, la mejor
// desviación alcanzable con un barrido 25 veces más fino que el de producción.
TEST(ShapeClassProbe, BarridoFino) {
    struct Caso {
        const char* nombre;
        cv::Mat mask;
    };
    std::vector<Caso> casos{
        {"hex r160 0deg", rasterize(polygonPath({250, 250}, 160, 6), 500)},
        {"hex r160 5deg", rasterize(polygonPath({250, 250}, 160, 6, 5.0), 500)},
        {"hex r160 25deg", rasterize(polygonPath({250, 250}, 160, 6, 25.0), 500)},
        {"pent r160 5deg", rasterize(polygonPath({250, 250}, 160, 5, 5.0), 500)},
        {"12gon r200", rasterize(polygonPath({260, 260}, 200, 12), 540)},
        {"12gon r160 25deg", rasterize(polygonPath({250, 250}, 160, 12, 25.0), 500)},
        {"10gon r400", rasterize(polygonPath({530, 530}, 400, 10), 1060)},
        {"10gon r50", rasterize(polygonPath({70, 70}, 50, 10), 140)},
    };
    for (const auto& c : casos) {
        const auto contour = outerContour(c.mask);
        const double per = cv::arcLength(contour, true);
        std::vector<double> mejor(41, 1e9);
        for (double f = 0.0005; f <= 0.06; f *= 1.03) {
            std::vector<cv::Point> approx;
            cv::approxPolyDP(contour, approx, f * per, true);
            if (approx.size() < 3 || approx.size() > 40) {
                continue;
            }
            mejor[approx.size()] = std::min(mejor[approx.size()], worstToPoly(contour, approx));
        }
        std::printf("%-18s :", c.nombre);
        for (int k = 3; k <= 14; ++k) {
            if (mejor[static_cast<std::size_t>(k)] < 1e8) {
                std::printf("  %d:%.2f", k, mejor[static_cast<std::size_t>(k)]);
            }
        }
        std::printf("\n");
    }
}

TEST(ShapeClassProbe, Rasterizado) {
    // (a) como el banco actual: vértices enteros, LINE_AA, SIN binarizar
    cv::Mat a(500, 500, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point> v;
    for (int i = 0; i < 5; ++i) {
        const double t = 2.0 * kPi * i / 5;
        v.emplace_back(static_cast<int>(250 + 160 * std::cos(t)),
                       static_cast<int>(250 + 160 * std::sin(t)));
    }
    cv::fillPoly(a, v, cv::Scalar(255), cv::LINE_AA);
    dumpSweep("pent enteros AA sin binarizar", outerContour(a));
    cv::Mat ab = a > 127;
    dumpSweep("pent enteros AA binarizado", outerContour(ab));

    cv::Mat c = rasterize(polygonPath({250, 250}, 160, 5), 500);
    cv::Mat cb = c > 127;
    dumpSweep("pent denso shift AA binarizado", outerContour(cb));

    cv::Mat d(500, 500, CV_8UC1, cv::Scalar(0));
    cv::fillPoly(d, v, cv::Scalar(255));  // sin AA
    dumpSweep("pent enteros sin AA", outerContour(d));

    for (int n : {3, 4, 5, 6, 8, 12}) {
        const cv::Mat raw = rasterize(polygonPath({250, 250}, 160, n), 500);
        const cv::Mat bin = raw > 127;
        const auto s1 = classifyShape(outerContour(raw), raw);
        const auto s2 = classifyShape(outerContour(bin), bin);
        std::printf("n=%2d  AA:%s/%d/%.2f   binario:%s/%d/%.2f\n", n, shapeKindName(s1.kind),
                    s1.sides, s1.deviation, shapeKindName(s2.kind), s2.sides, s2.deviation);
    }
}

TEST(ShapeClassProbe, BarridoInterno) {
    dumpSweep("pentagono r=160", outerContour(rasterize(polygonPath({250, 250}, 160, 5), 500)));
    dumpSweep("triangulo r=160 a 45",
              outerContour(rasterize(polygonPath({250, 250}, 160, 3, 45.0), 500)));
    dumpSweep("12-gono r=200",
              outerContour(rasterize(polygonPath({260, 260}, 200, 12), 540)));
    dumpSweep("hexagono r=160 a 5",
              outerContour(rasterize(polygonPath({250, 250}, 160, 6, 5.0), 500)));
}

TEST(ShapeClassProbe, Degenerados) {
    row("vacio", classifyShape({}));
    row("3 puntos", classifyShape({{10, 10}, {20, 10}, {15, 20}}));
    std::vector<cv::Point> same(40, cv::Point(30, 30));
    row("40 iguales", classifyShape(same));
    std::vector<cv::Point> line;
    for (int i = 0; i < 40; ++i) {
        line.emplace_back(10 + i, 10);
    }
    row("40 en recta", classifyShape(line));
    std::vector<cv::Point> back;
    for (int i = 0; i < 20; ++i) {
        back.emplace_back(10 + i, 10);
    }
    for (int i = 19; i >= 0; --i) {
        back.emplace_back(10 + i, 11);
    }
    row("recta ida y vuelta", classifyShape(back));
    std::vector<cv::Point> tiny{{5, 5}, {6, 5}, {7, 5}, {7, 6}, {7, 7}, {6, 7}, {5, 7}, {5, 6}};
    row("cuadrado 2px", classifyShape(tiny));
    cv::Mat emptyMask(100, 100, CV_8UC1, cv::Scalar(0));
    row("disco + mascara vacia", classifyShape(outerContour(rasterize(circlePath({250, 250}, 100), 500)), emptyMask));
    // Pieza cortada por el borde de la imagen.
    cv::Mat clipped(300, 300, CV_8UC1, cv::Scalar(0));
    cv::circle(clipped, {150, 40}, 120, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    row("disco cortado arriba", classifyMask(clipped));
    cv::Mat corner(300, 300, CV_8UC1, cv::Scalar(0));
    cv::circle(corner, {0, 0}, 150, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    row("cuarto de disco esquina", classifyMask(corner));
    cv::Mat full(120, 120, CV_8UC1, cv::Scalar(255));
    row("mascara toda blanca", classifyMask(full));
    cv::Mat nan(500, 500, CV_8UC1, cv::Scalar(0));
    cv::line(nan, {50, 250}, {450, 250}, cv::Scalar(255), 1);
    row("segmento de 1 px de grosor", classifyMask(nan));
}
