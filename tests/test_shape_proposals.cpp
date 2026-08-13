// Banco de IMAGEN REAL para la medición automática por forma.
//
// Todo lo que había hasta ahora sobre `proposeTools` y `classifyShape` está
// medido sobre máscaras sintéticas impecables: bordes de antialias perfecto,
// pieza a 220 sobre fondo a 30, iluminación plana. Una cámara no da eso, y una
// propuesta que solo acierta con el dibujo perfecto no sirve en producción.
//
// Aquí la máscara NO se regala: se rendera la escena en gris, se ENSUCIA
// (ruido, desenfoque, poco contraste, iluminación desigual) y se vuelve a
// SEGMENTAR con Otsu, que es lo que hace la cadena de verdad. Así el ruido
// llega hasta el contorno y hasta la clasificación, que es donde duele.
//
// Y aparte de la suciedad, las INVARIANCIAS: la respuesta no puede depender de
// dónde está la pieza, de cómo está girada ni de a qué distancia se tomó la
// foto.
#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "inspection_editor/auto_measure.h"
#include "vision/shape_class.h"

using pci::inspection::AutoProposal;
using pci::inspection::proposeTools;
using pci::inspection::ProposeOptions;
using pci::inspection::ToolType;
using pci::vision::ShapeKind;

namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Generadores de figura
//
// Todas se construyen a partir de una POLILÍNEA en coordenadas locales que
// luego se escala, se gira y se coloca. Así rotación, traslación y escala son
// parámetros de la misma figura y no siete generadores distintos: si girar
// necesitara `warpAffine` sobre la máscara ya rasterizada, el remuestreo
// metería su propio error y no se sabría si el fallo es de la propuesta o del
// giro.
// ---------------------------------------------------------------------------

std::vector<cv::Point2d> place(const std::vector<cv::Point2d>& local, cv::Point2d centre,
                               double scale, double rotDeg) {
    const double a = rotDeg * kPi / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    std::vector<cv::Point2d> out;
    out.reserve(local.size());
    for (const auto& p : local) {
        const double x = p.x * scale;
        const double y = p.y * scale;
        out.push_back({centre.x + x * c - y * s, centre.y + x * s + y * c});
    }
    return out;
}

// Rasteriza con subpíxel (shift = 1/8 px): el contorno no hereda el error de
// redondear los vértices a entero, que en una pieza pequeña vale tanto como el
// ruido que se quiere medir.
void fillInto(cv::Mat& mask, const std::vector<cv::Point2d>& path, int value) {
    std::vector<cv::Point> scaled;
    scaled.reserve(path.size());
    for (const auto& p : path) {
        scaled.emplace_back(static_cast<int>(std::lround(p.x * 8.0)),
                            static_cast<int>(std::lround(p.y * 8.0)));
    }
    cv::fillPoly(mask, scaled, cv::Scalar(value), cv::LINE_AA, 3);
}

cv::Mat fillPath(int size, const std::vector<cv::Point2d>& path) {
    cv::Mat mask(size, size, CV_8UC1, cv::Scalar(0));
    fillInto(mask, path, 255);
    return mask;
}

std::vector<cv::Point2d> circlePath(double r, double stepPx = 1.5) {
    const int n = std::max(24, static_cast<int>(2.0 * kPi * r / stepPx));
    std::vector<cv::Point2d> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * kPi * i / n;
        out.push_back({r * std::cos(a), r * std::sin(a)});
    }
    return out;
}

std::vector<cv::Point2d> polygonPath(double r, int sides) {
    std::vector<cv::Point2d> out;
    out.reserve(static_cast<std::size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        const double a = 2.0 * kPi * i / sides;
        out.push_back({r * std::cos(a), r * std::sin(a)});
    }
    return out;
}

// Rectángulo con las cuatro esquinas redondeadas, muestreado por los arcos: los
// tramos rectos salen solos al cerrar la polilínea.
std::vector<cv::Point2d> roundedRectPath(double w, double h, double radius) {
    const double hw = w / 2.0 - radius;
    const double hh = h / 2.0 - radius;
    const cv::Point2d corners[4] = {{hw, hh}, {-hw, hh}, {-hw, -hh}, {hw, -hh}};
    std::vector<cv::Point2d> out;
    for (int k = 0; k < 4; ++k) {
        const double a0 = kPi / 2.0 * k;
        const int steps = std::max(6, static_cast<int>(radius));
        for (int i = 0; i <= steps; ++i) {
            const double a = a0 + kPi / 2.0 * i / steps;
            out.push_back({corners[k].x + radius * std::cos(a),
                           corners[k].y + radius * std::sin(a)});
        }
    }
    return out;
}

// Pieza en L: seis caras rectas, dos de ellas enfrentadas en cada brazo. Es la
// figura que destapó el «Espesor» que mentía, así que no puede faltar.
std::vector<cv::Point2d> lShapePath() {
    return {{-130, -130}, {130, -130}, {130, -50}, {-50, -50}, {-50, 130}, {-130, 130}};
}

cv::Mat discMask(int size, cv::Point2d c, double r) {
    return fillPath(size, place(circlePath(r), c, 1.0, 0.0));
}

cv::Mat washerMask(int size, cv::Point2d c, double outer, double inner) {
    cv::Mat mask = fillPath(size, place(circlePath(outer), c, 1.0, 0.0));
    fillInto(mask, place(circlePath(inner), c, 1.0, 0.0), 0);
    return mask;
}

cv::Mat polygonMask(int size, cv::Point2d c, double r, int sides, double rotDeg = 0.0) {
    return fillPath(size, place(polygonPath(r, sides), c, 1.0, rotDeg));
}

cv::Mat roundedRectMask(int size, cv::Point2d c, double w, double h, double radius,
                        double rotDeg = 0.0) {
    return fillPath(size, place(roundedRectPath(w, h, radius), c, 1.0, rotDeg));
}

cv::Mat lMask(int size, cv::Point2d c, double scale = 1.0, double rotDeg = 0.0) {
    return fillPath(size, place(lShapePath(), c, scale, rotDeg));
}

// ---------------------------------------------------------------------------
// Degradación de imagen
// ---------------------------------------------------------------------------

// Pinta la pieza sobre el fondo CONSERVANDO el antialias de la máscara. Un
// `setTo` con máscara daría un canto duro de 0 a 255 que ninguna cámara ve, y
// el detector de bordes subpíxel lo tendría más fácil que en la realidad.
cv::Mat renderGray(const cv::Mat& mask, double piece, double background) {
    cv::Mat alpha;
    mask.convertTo(alpha, CV_32F, 1.0 / 255.0);
    cv::Mat value = alpha * (piece - background) + background;
    cv::Mat out;
    value.convertTo(out, CV_8U);
    return out;
}

cv::Mat withNoise(const cv::Mat& gray, double sigma, int seed) {
    if (sigma <= 0.0) {
        return gray.clone();
    }
    cv::Mat value;
    gray.convertTo(value, CV_32F);
    cv::Mat noise(gray.size(), CV_32F);
    cv::RNG rng(static_cast<std::uint64_t>(seed));
    rng.fill(noise, cv::RNG::NORMAL, 0.0, sigma);
    value += noise;
    cv::Mat out;
    value.convertTo(out, CV_8U);  // satura en 0 y 255, como el sensor
    return out;
}

cv::Mat withBlur(const cv::Mat& gray, int kernel) {
    if (kernel <= 1) {
        return gray.clone();
    }
    cv::Mat out;
    cv::GaussianBlur(gray, out, cv::Size(kernel, kernel), 0.0);
    return out;
}

// Iluminación de un lado: rampa lineal de -amplitud a +amplitud a lo ancho.
cv::Mat withGradient(const cv::Mat& gray, double amplitude) {
    cv::Mat value;
    gray.convertTo(value, CV_32F);
    for (int x = 0; x < value.cols; ++x) {
        const double t = value.cols > 1 ? 2.0 * x / (value.cols - 1) - 1.0 : 0.0;
        value.col(x) += cv::Scalar(amplitude * t);
    }
    cv::Mat out;
    value.convertTo(out, CV_8U);
    return out;
}

// Viñeta: el clásico de un objetivo barato, oscurece las esquinas. `strength`
// es la fracción de luz que se pierde en la esquina.
cv::Mat withVignette(const cv::Mat& gray, double strength) {
    cv::Mat value;
    gray.convertTo(value, CV_32F);
    const double cx = (value.cols - 1) / 2.0;
    const double cy = (value.rows - 1) / 2.0;
    const double maxR2 = cx * cx + cy * cy;
    for (int y = 0; y < value.rows; ++y) {
        auto* row = value.ptr<float>(y);
        for (int x = 0; x < value.cols; ++x) {
            const double dx = x - cx;
            const double dy = y - cy;
            const double k = 1.0 - strength * (dx * dx + dy * dy) / maxR2;
            row[x] = static_cast<float>(row[x] * k);
        }
    }
    cv::Mat out;
    value.convertTo(out, CV_8U);
    return out;
}

// La segmentación que hace la cadena real: umbral global de Otsu. Se usa a
// propósito la más sencilla — si la propuesta solo aguanta con una
// segmentación lista, el límite que se mida sería el de la segmentación y no
// el de la propuesta.
cv::Mat segment(const cv::Mat& gray) {
    cv::Mat mask;
    cv::threshold(gray, mask, 0.0, 255.0, cv::THRESH_BINARY | cv::THRESH_OTSU);
    return mask;
}

// ---------------------------------------------------------------------------
// Lectura de las propuestas
// ---------------------------------------------------------------------------

struct Scene {
    cv::Mat gray;
    cv::Mat mask;
};

Scene sceneFrom(const cv::Mat& mask) {
    Scene scene;
    cv::threshold(mask, scene.mask, 127.0, 255.0, cv::THRESH_BINARY);
    scene.gray = renderGray(mask, 220.0, 30.0);
    return scene;
}

pci::vision::Fixture identity() { return {}; }

// Tope alto a propósito en casi todo el banco: con el tope por defecto de 12 un
// hexágono pierde tres de sus seis ángulos por recorte y no se podría saber si
// falta una propuesta porque no se generó o porque no cupo.
ProposeOptions wideOptions() {
    ProposeOptions options;
    options.maxProposals = 40;
    return options;
}

std::vector<cv::Point> largestContour(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> found;
    cv::findContours(mask, found, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (found.empty()) {
        return {};
    }
    return *std::max_element(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return cv::contourArea(a) < cv::contourArea(b);
    });
}

ShapeKind classOf(const cv::Mat& mask) {
    const auto contour = largestContour(mask);
    if (contour.empty()) {
        return ShapeKind::Irregular;
    }
    return pci::vision::classifyShape(contour, mask).kind;
}

int countPrefix(const std::vector<AutoProposal>& proposals, const std::string& prefix) {
    return static_cast<int>(
        std::count_if(proposals.begin(), proposals.end(), [&prefix](const AutoProposal& p) {
            return p.config.name.rfind(prefix, 0) == 0;
        }));
}

int countType(const std::vector<AutoProposal>& proposals, ToolType type) {
    return static_cast<int>(std::count_if(
        proposals.begin(), proposals.end(),
        [type](const AutoProposal& p) { return p.config.type == type; }));
}

const AutoProposal* named(const std::vector<AutoProposal>& proposals, const std::string& name) {
    for (const auto& p : proposals) {
        if (p.config.name == name) {
            return &p;
        }
    }
    return nullptr;
}

// Lo que hace falta saber de una escena para llenar una fila de tabla.
struct Reading {
    ShapeKind kind = ShapeKind::Irregular;
    int total = 0;
    int sides = 0;   // "Lado i"
    int radii = 0;   // "Radio i"
    int angles = 0;
    double main = 0.0;  // la cota que define la pieza: Ø, Ø exterior o Largo total
    bool key = false;   // ¿siguen estando las propuestas clave de esta figura?
};

// ¿Están las propuestas SIN LAS CUALES la figura no está medida? No es «salen
// propuestas»: es que salgan las que un operador habría dibujado.
bool keyProposalsPresent(ShapeKind kind, const std::vector<AutoProposal>& proposals) {
    switch (kind) {
        case ShapeKind::Circle:
            return named(proposals, "Ø") != nullptr && named(proposals, "Redondez") != nullptr;
        case ShapeKind::Ring:
            return named(proposals, "Ø exterior") != nullptr &&
                   named(proposals, "Ø interior") != nullptr &&
                   named(proposals, "Redondez") != nullptr;
        case ShapeKind::Polygon:
            return countPrefix(proposals, "Lados (") == 1 && countPrefix(proposals, "Lado ") >= 3;
        case ShapeKind::Rounded:
            return countPrefix(proposals, "Lado ") >= 3 && countPrefix(proposals, "Radio ") >= 1;
        case ShapeKind::Irregular:
            return named(proposals, "Largo total") != nullptr &&
                   named(proposals, "Ancho total") != nullptr;
    }
    return false;
}

Reading read(const cv::Mat& gray, const cv::Mat& mask, ShapeKind expected) {
    Reading r;
    r.kind = classOf(mask);
    const auto proposals = proposeTools(gray, mask, identity(), wideOptions());
    r.total = static_cast<int>(proposals.size());
    r.sides = countPrefix(proposals, "Lado ");
    r.radii = countPrefix(proposals, "Radio ");
    r.angles = countType(proposals, ToolType::Angle);
    for (const char* what : {"Ø", "Ø exterior", "Largo total"}) {
        if (const AutoProposal* p = named(proposals, what)) {
            r.main = p->measured;
            break;
        }
    }
    r.key = r.kind == expected && keyProposalsPresent(expected, proposals);
    return r;
}

const char* kindName(ShapeKind kind) { return pci::vision::shapeKindName(kind); }

// Las siete figuras del banco, con el centro y el tamaño que se usan en todas
// las tablas para que las filas se puedan comparar entre sí.
struct Figure {
    const char* name;
    cv::Mat mask;
    ShapeKind expected;
};

std::vector<Figure> figures() {
    const cv::Point2d c{250.0, 250.0};
    std::vector<Figure> out;
    out.push_back({"disco r150", discMask(500, c, 150.0), ShapeKind::Circle});
    out.push_back({"arandela 160/70", washerMask(500, c, 160.0, 70.0), ShapeKind::Ring});
    out.push_back({"hexagono r160", polygonMask(500, c, 160.0, 6), ShapeKind::Polygon});
    out.push_back({"triangulo r160", polygonMask(500, c, 160.0, 3), ShapeKind::Polygon});
    out.push_back({"cuadrado r160", polygonMask(500, c, 160.0, 4), ShapeKind::Polygon});
    out.push_back({"rect red 300x200", roundedRectMask(500, c, 300.0, 200.0, 40.0),
                   ShapeKind::Rounded});
    out.push_back({"L", lMask(500, c), ShapeKind::Polygon});
    return out;
}

void header(const char* what) {
    std::printf("\n=== %s ===\n", what);
    std::printf("%-18s %-10s | %-20s %5s %5s %5s %5s %9s %s\n", "figura", "caso", "clase", "prop",
                "lado", "radio", "ang", "principal", "clave");
}

void row(const char* figure, const std::string& caseName, const Reading& r) {
    std::printf("%-18s %-10s | %-20s %5d %5d %5d %5d %9.2f %s\n", figure, caseName.c_str(),
                kindName(r.kind), r.total, r.sides, r.radii, r.angles, r.main,
                r.key ? "si" : "NO");
}

// Imprime la fila Y EXIGE que la figura siga midiéndose. Sin esta segunda
// mitad, todas las tablas de suciedad de abajo eran un informe que pasaba
// siempre: enseñaban el número y no comprobaban nada.
void check(const char* figure, const std::string& caseName, const Reading& r) {
    row(figure, caseName, r);
    EXPECT_TRUE(r.key) << figure << " con " << caseName
                       << ": la suciedad se llevo por delante las propuestas clave";
}

// ---------------------------------------------------------------------------
// Utilidades de texto: comparar el MOTIVO con la MEDIDA
// ---------------------------------------------------------------------------

// Primer número que aparece tras `marker`. Devuelve false si no hay marcador o
// si detrás no hay un número.
bool numberAfter(const std::string& text, const std::string& marker, double& out) {
    const std::size_t at = text.find(marker);
    if (at == std::string::npos) {
        return false;
    }
    std::size_t i = at + marker.size();
    while (i < text.size() && text[i] == ' ') {
        ++i;
    }
    const std::size_t start = i;
    while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) != 0 ||
                               text[i] == '.' || text[i] == '-')) {
        ++i;
    }
    if (i == start) {
        return false;
    }
    out = std::stod(text.substr(start, i - start));
    return true;
}

}  // namespace

// ===========================================================================
// 1. Degradación de imagen: dónde está el límite
// ===========================================================================

// Sonda de referencia: qué sale con la escena LIMPIA, para que las tablas de
// suciedad tengan contra qué compararse. Sin esta fila no se sabe si una
// propuesta que falta la mató el ruido o nunca estuvo.
TEST(PropuestasImagenReal, LineaBaseLimpia) {
    header("linea base (pieza 220 / fondo 30, sin ensuciar)");
    for (const auto& f : figures()) {
        const Scene s = sceneFrom(f.mask);
        row(f.name, "limpia", read(s.gray, s.mask, f.expected));
        // Y con la máscara RESEGMENTADA por Otsu, que es la que usarán todas
        // las filas siguientes: si ya aquí difiere, el resto de la tabla mide
        // la segmentación y no la propuesta.
        row(f.name, "otsu", read(s.gray, segment(s.gray), f.expected));
    }
}

TEST(PropuestasImagenReal, RuidoGaussiano) {
    header("ruido gaussiano (sigma sobre pieza 220 / fondo 30)");
    for (const auto& f : figures()) {
        for (double sigma : {2.0, 5.0, 10.0, 20.0}) {
            const cv::Mat gray = withNoise(renderGray(f.mask, 220.0, 30.0), sigma, 1234);
            check(f.name, "s=" + std::to_string(static_cast<int>(sigma)),
                read(gray, segment(gray), f.expected));
        }
    }
}

TEST(PropuestasImagenReal, Desenfoque) {
    header("desenfoque gaussiano (kernel)");
    for (const auto& f : figures()) {
        for (int k : {3, 5, 9}) {
            const cv::Mat gray = withBlur(renderGray(f.mask, 220.0, 30.0), k);
            check(f.name, "k=" + std::to_string(k), read(gray, segment(gray), f.expected));
        }
    }
}

TEST(PropuestasImagenReal, ContrasteBajo) {
    header("contraste bajo (pieza 120 / fondo 90) y ruido encima");
    for (const auto& f : figures()) {
        const cv::Mat flat = renderGray(f.mask, 120.0, 90.0);
        check(f.name, "120/90", read(flat, segment(flat), f.expected));
        for (double sigma : {2.0, 5.0}) {
            const cv::Mat gray = withNoise(flat, sigma, 77);
            check(f.name, "120/90 s" + std::to_string(static_cast<int>(sigma)),
                read(gray, segment(gray), f.expected));
        }
    }
}

TEST(PropuestasImagenReal, IluminacionDesigual) {
    header("iluminacion desigual");
    for (const auto& f : figures()) {
        const cv::Mat full = renderGray(f.mask, 220.0, 30.0);
        const cv::Mat weak = renderGray(f.mask, 120.0, 90.0);
        for (double amp : {30.0, 60.0}) {
            const cv::Mat gray = withGradient(full, amp);
            check(f.name, "grad" + std::to_string(static_cast<int>(amp)),
                read(gray, segment(gray), f.expected));
        }
        for (double strength : {0.3, 0.6}) {
            const cv::Mat gray = withVignette(full, strength);
            check(f.name, "vig" + std::to_string(static_cast<int>(strength * 100.0)),
                read(gray, segment(gray), f.expected));
        }
        // Y la combinación que de verdad se ve en una célula mal iluminada:
        // poco contraste Y un lado más iluminado que el otro. AQUÍ SE ROMPE, y
        // el límite queda escrito porque saber dónde falla vale más que fingir
        // que no falla.
        //
        // Lo que se rompe NO es la clasificación: es la segmentación. Con 30
        // niveles de contraste y un gradiente de 15, Otsu corta por donde no
        // debe y la máscara se traga el encuadre entero; la clase que sale
        // describe fielmente esa mancha de 500 px. En la aplicación real esto
        // no llega, porque el `maxAreaFraction` del pipeline rechaza antes una
        // «pieza» que ocupa casi todo el frame.
        //
        // Se afirma justo eso, para que si algún día el fallo cambia de sitio
        // —y pasa a ser del clasificador— el test lo diga.
        const cv::Mat both = withGradient(weak, 15.0);
        const Reading broken = read(both, segment(both), f.expected);
        row(f.name, "120/90+g15", broken);
        EXPECT_GT(broken.main, 450.0)
            << f.name
            << ": se esperaba que aqui fallara la SEGMENTACION (mascara de casi todo el "
               "encuadre de 500 px); si la medida es razonable, el fallo es otro";
    }
}

// ===========================================================================
// 2. Invariancias
// ===========================================================================

TEST(PropuestasImagenReal, Rotacion) {
    std::printf("\n=== rotacion 0..90 de 10 en 10 ===\n");
    for (int sides : {4, 6}) {
        std::printf("poligono de %d lados:\n", sides);
        for (int deg = 0; deg <= 90; deg += 10) {
            const cv::Mat mask = polygonMask(500, {250.0, 250.0}, 160.0, sides,
                                             static_cast<double>(deg));
            const Scene s = sceneFrom(mask);
            const auto proposals = proposeTools(s.gray, s.mask, identity(), wideOptions());
            const AutoProposal* largo = named(proposals, "Largo total");
            const AutoProposal* ancho = named(proposals, "Ancho total");
            std::printf("  %3d -> %-12s lados=%d largo=%8.2f ancho=%8.2f total=%zu\n", deg,
                        kindName(classOf(s.mask)), countPrefix(proposals, "Lado "),
                        largo != nullptr ? largo->measured : -1.0,
                        ancho != nullptr ? ancho->measured : -1.0, proposals.size());
        }
    }
    std::printf("disco (no debe cambiar nada al girar):\n");
    for (int deg = 0; deg <= 90; deg += 30) {
        const cv::Mat mask = discMask(500, {250.0, 250.0}, 150.0);
        const Scene s = sceneFrom(mask);
        const auto proposals = proposeTools(s.gray, s.mask, identity(), wideOptions());
        const AutoProposal* d = named(proposals, "Ø");
        std::printf("  %3d -> total=%zu D=%8.2f\n", deg, proposals.size(),
                    d != nullptr ? d->measured : -1.0);
    }
}

TEST(PropuestasImagenReal, Traslacion) {
    std::printf("\n=== traslacion por la imagen ===\n");
    for (const cv::Point2d c : {cv::Point2d{250, 250}, cv::Point2d{190, 300},
                                cv::Point2d{310, 190}, cv::Point2d{181, 181}}) {
        const Scene hex = sceneFrom(polygonMask(500, c, 160.0, 6, 11.0));
        const auto hp = proposeTools(hex.gray, hex.mask, identity(), wideOptions());
        const AutoProposal* largo = named(hp, "Largo total");
        const Scene disc = sceneFrom(discMask(500, c, 150.0));
        const auto dp = proposeTools(disc.gray, disc.mask, identity(), wideOptions());
        const AutoProposal* d = named(dp, "Ø");
        std::printf("  centro (%3.0f,%3.0f) hex: %-10s lados=%d largo=%8.2f | disco: %-10s "
                    "D=%8.2f\n",
                    c.x, c.y, kindName(classOf(hex.mask)), countPrefix(hp, "Lado "),
                    largo != nullptr ? largo->measured : -1.0, kindName(classOf(disc.mask)),
                    d != nullptr ? d->measured : -1.0);
    }
}

TEST(PropuestasImagenReal, Escala) {
    std::printf("\n=== escala: la misma figura a r=40,80,160,320 ===\n");
    std::printf("%-16s %5s %-12s %5s %10s %10s\n", "figura", "r", "clase", "prop", "principal",
                "princ/r");
    for (double r : {40.0, 80.0, 160.0, 320.0}) {
        const int size = static_cast<int>(r * 3.0);
        const cv::Point2d c{size / 2.0, size / 2.0};
        const Scene disc = sceneFrom(discMask(size, c, r));
        const Reading dr = read(disc.gray, disc.mask, ShapeKind::Circle);
        std::printf("%-16s %5.0f %-12s %5d %10.2f %10.4f %s\n", "disco", r, kindName(dr.kind),
                    dr.total, dr.main, dr.main / r, dr.key ? "" : "  <-- CLAVE NO");

        const Scene hex = sceneFrom(polygonMask(size, c, r, 6));
        const Reading hr = read(hex.gray, hex.mask, ShapeKind::Polygon);
        std::printf("%-16s %5.0f %-12s %5d %10.2f %10.4f lados=%d %s\n", "hexagono", r,
                    kindName(hr.kind), hr.total, hr.main, hr.main / r, hr.sides,
                    hr.key ? "" : "  <-- CLAVE NO");

        const Scene rr = sceneFrom(roundedRectMask(size, c, r * 1.875, r * 1.25, r * 0.25));
        const Reading rrr = read(rr.gray, rr.mask, ShapeKind::Rounded);
        std::printf("%-16s %5.0f %-12s %5d %10.2f %10.4f lados=%d radios=%d %s\n", "rect red", r,
                    kindName(rrr.kind), rrr.total, rrr.main, rrr.main / r, rrr.sides, rrr.radii,
                    rrr.key ? "" : "  <-- CLAVE NO");

        const Scene ele = sceneFrom(lMask(size, c, r / 160.0));
        const Reading er = read(ele.gray, ele.mask, ShapeKind::Polygon);
        std::printf("%-16s %5.0f %-12s %5d %10.2f %10.4f lados=%d %s\n", "L", r, kindName(er.kind),
                    er.total, er.main, er.main / r, er.sides, er.key ? "" : "  <-- CLAVE NO");
    }
}

// ===========================================================================
// 3. Calibración en mm
// ===========================================================================

TEST(PropuestasImagenReal, SondaCalibracion) {
    std::printf("\n=== calibracion ===\n");
    const Scene hex = sceneFrom(polygonMask(500, {250.0, 250.0}, 160.0, 6));
    for (double mmPerPixel : {0.0, 0.05, 0.2}) {
        std::printf("-- mmPerPixel = %.3f\n", mmPerPixel);
        for (const auto& p : proposeTools(hex.gray, hex.mask, identity(), wideOptions(),
                                          mmPerPixel)) {
            std::printf("   %-14s %-10s %9.3f | %s\n", p.config.name.c_str(),
                        pci::inspection::toolTypeName(p.config.type), p.measured,
                        p.detail.c_str());
        }
    }
    const Scene disc = sceneFrom(discMask(500, {250.0, 250.0}, 150.0));
    for (double mmPerPixel : {0.0, 0.05, 0.2}) {
        std::printf("-- disco, mmPerPixel = %.3f\n", mmPerPixel);
        for (const auto& p : proposeTools(disc.gray, disc.mask, identity(), wideOptions(),
                                          mmPerPixel)) {
            std::printf("   %-14s %-10s %9.3f | %s\n", p.config.name.c_str(),
                        pci::inspection::toolTypeName(p.config.type), p.measured,
                        p.detail.c_str());
        }
    }
}

// ===========================================================================
// 4. Tolerancias sugeridas
// ===========================================================================

TEST(PropuestasImagenReal, SondaTolerancias) {
    std::printf("\n=== tolerancias sugeridas ===\n");
    std::printf("%-18s %-14s %-10s %9s %9s %9s %8s\n", "figura", "propuesta", "tipo", "medida",
                "min", "max", "ancho/m");
    for (const auto& f : figures()) {
        const Scene s = sceneFrom(f.mask);
        for (const auto& p : proposeTools(s.gray, s.mask, identity(), wideOptions())) {
            const double width = p.config.toleranceMax - p.config.toleranceMin;
            std::printf("%-18s %-14s %-10s %9.3f %9.3f %9.3f %8.3f\n", f.name,
                        p.config.name.c_str(), pci::inspection::toolTypeName(p.config.type),
                        p.measured, p.config.toleranceMin, p.config.toleranceMax,
                        std::abs(p.measured) > 1e-9 ? width / std::abs(p.measured) : -1.0);
        }
    }
}

// ===========================================================================
// 5. Coherencia motivo/medida
// ===========================================================================

TEST(PropuestasImagenReal, SondaCoherencia) {
    std::printf("\n=== motivo contra medida ===\n");
    for (const auto& f : figures()) {
        const Scene s = sceneFrom(f.mask);
        for (const auto& p : proposeTools(s.gray, s.mask, identity(), wideOptions())) {
            double promised = 0.0;
            const bool hasApprox = numberAfter(p.reason, "≈", promised);
            if (hasApprox) {
                std::printf("%-18s %-14s medida=%9.3f motivo dice %9.3f  desvio=%7.2f%%\n",
                            f.name, p.config.name.c_str(), p.measured, promised,
                            promised > 1e-9 ? 100.0 * (p.measured - promised) / promised : 0.0);
            }
            double outer = 0.0;
            if (numberAfter(p.reason, "Ø exterior", outer) ||
                numberAfter(p.reason, "Ø ", outer)) {
                std::printf("%-18s %-14s medida=%9.3f motivo Ø  %9.3f\n", f.name,
                            p.config.name.c_str(), p.measured, outer);
            }
            double bore = 0.0;
            if (numberAfter(p.reason, "agujero central de", bore)) {
                std::printf("%-18s %-14s medida=%9.3f motivo bore %9.3f\n", f.name,
                            p.config.name.c_str(), p.measured, bore);
            }
        }
    }
}

// ===========================================================================
// 6. Cotas repetidas
// ===========================================================================

namespace {

// Dónde está la cota, para poder decir si dos propuestas miden LO MISMO EN EL
// MISMO SITIO. Sin el sitio, los seis lados de un hexágono contarían como seis
// duplicados, y no lo son: miden seis caras distintas del mismo tamaño.
bool anchorOf(const AutoProposal& p, cv::Point2f& out) {
    if (const auto* ruler = std::get_if<pci::inspection::RulerGeometry>(&p.geometry)) {
        out = (ruler->p0 + ruler->p1) / 2.0F;
        return true;
    }
    if (const auto* caliper = std::get_if<pci::inspection::CaliperGeometry>(&p.geometry)) {
        out = (caliper->p0 + caliper->p1) / 2.0F;
        return true;
    }
    if (const auto* circle = std::get_if<pci::inspection::CircleGeometry>(&p.geometry)) {
        out = circle->center;
        return true;
    }
    if (const auto* arc = std::get_if<pci::inspection::ArcGeometry>(&p.geometry)) {
        out = arc->mid;
        return true;
    }
    return false;
}

bool isLengthTool(ToolType type) {
    return type == ToolType::Ruler || type == ToolType::Caliper || type == ToolType::Circle ||
           type == ToolType::Arc;
}

}  // namespace

TEST(PropuestasImagenReal, SondaDuplicados) {
    std::printf("\n=== cotas repetidas (mismo valor a <2%% y anclas a <25px) ===\n");
    for (const auto& f : figures()) {
        const Scene s = sceneFrom(f.mask);
        const auto proposals = proposeTools(s.gray, s.mask, identity(), wideOptions());
        int sameValue = 0;
        int sameCota = 0;
        for (std::size_t i = 0; i < proposals.size(); ++i) {
            for (std::size_t j = i + 1; j < proposals.size(); ++j) {
                if (!isLengthTool(proposals[i].config.type) ||
                    !isLengthTool(proposals[j].config.type)) {
                    continue;
                }
                const double ref = std::max(std::abs(proposals[i].measured), 1.0);
                if (std::abs(proposals[i].measured - proposals[j].measured) / ref >= 0.02) {
                    continue;
                }
                ++sameValue;
                cv::Point2f a;
                cv::Point2f b;
                if (anchorOf(proposals[i], a) && anchorOf(proposals[j], b) &&
                    cv::norm(a - b) < 25.0) {
                    ++sameCota;
                    std::printf("   %-18s DUPLICADO %s(%.2f) vs %s(%.2f) a %.1f px\n", f.name,
                                proposals[i].config.name.c_str(), proposals[i].measured,
                                proposals[j].config.name.c_str(), proposals[j].measured,
                                cv::norm(a - b));
                }
            }
        }
        std::printf("%-18s propuestas=%zu pares mismo valor=%d pares MISMA COTA=%d\n", f.name,
                    proposals.size(), sameValue, sameCota);
    }
}

// ===========================================================================
// 7. Piezas múltiples y casos límite
// ===========================================================================

TEST(PropuestasImagenReal, SondaCasosLimite) {
    std::printf("\n=== casos limite ===\n");
    struct Case {
        const char* name;
        cv::Mat mask;
    };
    std::vector<Case> cases;
    {
        cv::Mat m(500, 500, CV_8UC1, cv::Scalar(0));
        cv::rectangle(m, cv::Rect(0, 0, 260, 200), cv::Scalar(255), cv::FILLED);
        cases.push_back({"tocando esquina", m});
    }
    {
        cv::Mat m(500, 500, CV_8UC1, cv::Scalar(0));
        cv::rectangle(m, cv::Rect(120, 0, 260, 200), cv::Scalar(255), cv::FILLED);
        cases.push_back({"cortada arriba", m});
    }
    {
        cv::Mat m = discMask(500, {160.0, 250.0}, 120.0);
        fillInto(m, place(circlePath(45.0), {400.0, 400.0}, 1.0, 0.0), 255);
        cases.push_back({"dos piezas", m});
    }
    cases.push_back({"mascara vacia", cv::Mat(500, 500, CV_8UC1, cv::Scalar(0))});
    cases.push_back({"mascara toda blanca", cv::Mat(500, 500, CV_8UC1, cv::Scalar(255))});
    cases.push_back({"1x1", cv::Mat(1, 1, CV_8UC1, cv::Scalar(255))});
    for (const auto& c : cases) {
        const Scene s = sceneFrom(c.mask);
        std::vector<AutoProposal> proposals;
        ASSERT_NO_THROW(proposals = proposeTools(s.gray, s.mask, identity(), wideOptions()))
            << c.name;
        std::printf("%-22s clase=%-12s propuestas=%zu", c.name, kindName(classOf(s.mask)),
                    proposals.size());
        for (const auto& p : proposals) {
            std::printf(" [%s=%.1f]", p.config.name.c_str(), p.measured);
        }
        std::printf("\n");
    }
    // Imagen de un solo color: no hay pieza, y la máscara que da Otsu sobre
    // ruido puro es basura. Nada puede lanzar.
    for (int level : {0, 128, 255}) {
        const cv::Mat flat(400, 400, CV_8UC1, cv::Scalar(level));
        std::vector<AutoProposal> proposals;
        ASSERT_NO_THROW(proposals = proposeTools(flat, segment(flat), identity(), wideOptions()))
            << level;
        std::printf("un solo color %3d -> propuestas=%zu\n", level, proposals.size());
    }
}
