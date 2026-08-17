// Banco de las OPCIONES de cada herramienta.
//
// Las otras suites comprueban que cada herramienta MIDE bien. Esta comprueba lo
// otro, que es de donde salen las averías que hacen que el operador deje de
// fiarse: que cada parámetro que se puede mover HAGA algo, y que lo que la
// herramienta necesita para medir se pueda mover.
//
// Las dos formas de estar mal, y las dos hacen el mismo daño:
//
//   1. Un parámetro que se toca y no cambia nada. El operador sube el valor,
//      vuelve a medir, sale lo mismo, y a partir de ahí no se cree ni ese
//      parámetro ni la herramienta. Aquí se detecta EJECUTANDO: se mide, se
//      cambia SOLO ese campo, se vuelve a medir y se exige que el número o el
//      veredicto se muevan.
//
//   2. Un parámetro que falta: el ejecutor usa un valor fijo escrito en el
//      código donde debería haber un campo, o la descripción promete un campo
//      que no existe. Eso no se puede «probar» con una aserción de igualdad, así
//      que se prueba enseñando que ese valor SÍ decide la medida — y entonces
//      no poder tocarlo es el fallo.
//
// Todas las escenas son sintéticas y de cotas conocidas: así cada número se
// compara contra lo que debería salir, no contra «que salga algo». Y todos los
// umbrales van precedidos del `printf` con el número real medido, porque una
// cota que nadie ha visto es una cota inventada.
#include <gtest/gtest.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"
#include "vision/position_fixture.h"

using namespace pci::inspection;  // NOLINT(google-build-using-namespace)
using pci::vision::Fixture;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Fixture identidad: coordenadas de pieza == coordenadas de imagen. Lo que se
// prueba aquí son las opciones, no el enganche a la pieza.
const Fixture kIdentity{{0.0F, 0.0F}, 0.0};

ToolConfig configOf(const ToolGeometry& geometry, const std::string& name = "opcion",
                    double tolMin = 0.0, double tolMax = 1e9) {
    ToolConfig config;
    config.type = typeOf(geometry);
    config.name = name;
    config.geometryJson = toJson(geometry);
    config.toleranceMin = tolMin;
    config.toleranceMax = tolMax;
    return config;
}

// Mide con esta geometría. Pasa por `toJson`/`geometryFromJson` igual que en
// producción: un parámetro que no se persistiera se perdería aquí, que es
// justamente otra forma de «se mueve y no hace nada».
ToolRunResult read(const cv::Mat& gray, const ToolGeometry& geometry) {
    const auto result = runTool(gray, kIdentity, configOf(geometry));
    EXPECT_TRUE(result.isOk()) << result.error().message;
    return result.isOk() ? result.value() : ToolRunResult{};
}

// El corazón del banco: cambiar UN campo tiene que notarse. Se acepta que se
// note en el número o en el veredicto —hay parámetros cuyo trabajo es que la
// herramienta pueda medir o no—, pero algo tiene que moverse.
void expectKnobWorks(const char* knob, const ToolRunResult& before,
                     const ToolRunResult& after, double minChange) {
    const double delta = std::abs(after.measured - before.measured);
    std::printf("  %-42s %s %10.4f  ->  %s %10.4f   (Δ=%.4f)\n", knob,
                before.ok ? "OK" : "NG", before.measured, after.ok ? "OK" : "NG",
                after.measured, delta);
    EXPECT_TRUE(delta >= minChange || before.ok != after.ok)
        << knob << ": mover el parámetro no cambió ni la medida ni el veredicto.\n"
        << "  antes:   " << before.detail << "\n  después: " << after.detail;
}

// --- Escenas sintéticas -----------------------------------------------------

constexpr unsigned char kDark = 40;
constexpr unsigned char kLight = 220;

cv::Mat canvas(int size, unsigned char background = kLight) {
    return cv::Mat(size, size, CV_8UC1, cv::Scalar(background));
}

// Barra vertical que SOLO es estrecha en una franja central: ancha (100 px) en
// casi toda la altura y estrecha (40 px) entre `y=140` e `y=160`.
//
// Es la escena que separa lo que mide un calíper de banda fina de lo que mide
// uno de banda gorda, y por eso vale a la vez para probar que la banda hace algo
// y para enseñar hasta dónde llega el disparate de dejarla crecer sin tope.
cv::Mat steppedBar() {
    cv::Mat gray = canvas(300);
    cv::rectangle(gray, cv::Rect(100, 0, 100, 300), cv::Scalar(kDark), cv::FILLED);
    cv::rectangle(gray, cv::Rect(100, 140, 30, 21), cv::Scalar(kLight), cv::FILLED);
    cv::rectangle(gray, cv::Rect(170, 140, 30, 21), cv::Scalar(kLight), cv::FILLED);
    return gray;
}

// Disco oscuro centrado, con `lobes` lóbulos de amplitud `amplitude` px: una
// pieza redonda cuya FORMA se conoce, que es lo que hace falta para juzgar la
// redondez y no solo el diámetro.
cv::Mat disc(double radius, int lobes = 0, double amplitude = 0.0, int size = 400) {
    cv::Mat gray = canvas(size);
    const cv::Point2f centre(size / 2.0F, size / 2.0F);
    std::vector<cv::Point> poly;
    poly.reserve(720);
    for (int k = 0; k < 720; ++k) {
        const double theta = 2.0 * kPi * k / 720.0;
        const double r = radius + (lobes > 0 ? amplitude * std::cos(lobes * theta) : 0.0);
        poly.emplace_back(cvRound(centre.x + r * std::cos(theta)),
                          cvRound(centre.y + r * std::sin(theta)));
    }
    cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{poly}, cv::Scalar(kDark),
                 cv::LINE_AA);
    return gray;
}

// Borde recto horizontal en y=200 (bloque oscuro por debajo) con una mella
// rectangular de `depth` px de profundidad y 20 px de ancho en el centro.
cv::Mat notchedEdge(double depth) {
    cv::Mat gray = canvas(400);
    cv::rectangle(gray, cv::Rect(0, 200, 400, 200), cv::Scalar(kDark), cv::FILLED);
    if (depth > 0.0) {
        cv::rectangle(gray, cv::Rect(190, 200, 20, cvRound(depth)), cv::Scalar(kLight),
                      cv::FILLED);
    }
    return gray;
}

// Barra horizontal de torno: diámetro 100 px, eje en y=250.
cv::Mat turnedBar(int size = 500) {
    cv::Mat gray = canvas(size);
    cv::rectangle(gray, cv::Rect(40, 200, size - 80, 100), cv::Scalar(kDark), cv::FILLED);
    return gray;
}

// La misma barra con una entalla rectangular centrada, de cotas conocidas.
cv::Mat barWithGroove(double grooveWidth, double grooveDepth) {
    cv::Mat gray = canvas(500);
    const double cy = 250.0;
    const double half = 50.0;
    const double rootHalf = half - grooveDepth;
    const double x0 = 250.0 - grooveWidth / 2.0;
    const double x1 = 250.0 + grooveWidth / 2.0;
    const auto P = [](double x, double y) { return cv::Point(cvRound(x), cvRound(y)); };
    const std::vector<cv::Point> poly = {
        P(40.0, cy - half),   P(x0, cy - half),     P(x0, cy - rootHalf),
        P(x1, cy - rootHalf), P(x1, cy - half),     P(460.0, cy - half),
        P(460.0, cy + half),  P(x1, cy + half),     P(x1, cy + rootHalf),
        P(x0, cy + rootHalf), P(x0, cy + half),     P(40.0, cy + half)};
    cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{poly}, cv::Scalar(kDark));
    return gray;
}

// Tornillo visto de perfil, de paso y diámetros conocidos (perfil trapecial).
cv::Mat threadBar(double pitchPx, double majorDia, double minorDia, double flankDeg) {
    cv::Mat gray(500, 700, CV_8UC1, cv::Scalar(kLight));
    const cv::Point2f centre(350.0F, 250.0F);
    constexpr double kLength = 460.0;
    const double majorR = majorDia / 2.0;
    const double minorR = minorDia / 2.0;
    const double height = majorR - minorR;
    const double flankRun = height * std::tan(flankDeg * kPi / 360.0);
    const double flat = std::max(0.0, (pitchPx - 2.0 * flankRun) / 2.0);
    const auto radiusAt = [&](double t) {
        double phase = std::fmod(t, pitchPx);
        if (phase < 0.0) {
            phase += pitchPx;
        }
        if (phase < flankRun) {
            return minorR + height * (phase / flankRun);
        }
        if (phase < flankRun + flat) {
            return majorR;
        }
        if (phase < 2.0 * flankRun + flat) {
            return majorR - height * ((phase - flankRun - flat) / flankRun);
        }
        return minorR;
    };
    std::vector<cv::Point> poly;
    constexpr int kSteps = 600;
    for (int i = 0; i <= kSteps; ++i) {
        const double t = kLength * i / kSteps;
        poly.emplace_back(cvRound(centre.x - kLength / 2.0 + t),
                          cvRound(centre.y + radiusAt(t)));
    }
    for (int i = kSteps; i >= 0; --i) {
        const double t = kLength * i / kSteps;
        poly.emplace_back(cvRound(centre.x - kLength / 2.0 + t),
                          cvRound(centre.y - radiusAt(t)));
    }
    cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{poly}, cv::Scalar(kDark),
                 cv::LINE_AA);
    return gray;
}

// Rueda dentada vista de cara, con dientes trapeciales de cotas conocidas.
cv::Mat gearWheel(int teeth, double tipRadius, double rootRadius) {
    cv::Mat gray(700, 700, CV_8UC1, cv::Scalar(kLight));
    const cv::Point2f centre(350.0F, 350.0F);
    std::vector<cv::Point> poly;
    constexpr int kSteps = 3600;
    for (int i = 0; i < kSteps; ++i) {
        const double theta = 2.0 * kPi * i / kSteps;
        const double phase = std::fmod(theta * teeth / (2.0 * kPi), 1.0);
        double r = rootRadius;
        if (phase < 0.25) {
            r = rootRadius + (tipRadius - rootRadius) * (phase / 0.25);
        } else if (phase < 0.5) {
            r = tipRadius;
        } else if (phase < 0.75) {
            r = tipRadius - (tipRadius - rootRadius) * ((phase - 0.5) / 0.25);
        }
        poly.emplace_back(cvRound(centre.x + r * std::cos(theta)),
                          cvRound(centre.y + r * std::sin(theta)));
    }
    cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{poly}, cv::Scalar(kDark),
                 cv::LINE_AA);
    return gray;
}

// Cuadrado de 200 px con dos agujeros redondos. `pieceIsDark` invierte la
// polaridad ENTERA de la escena: la misma pieza, iluminada al revés.
cv::Mat squareWithHoles(bool pieceIsDark) {
    const unsigned char piece = pieceIsDark ? kDark : kLight;
    const unsigned char back = pieceIsDark ? kLight : kDark;
    cv::Mat gray = canvas(400, back);
    cv::rectangle(gray, cv::Rect(100, 100, 200, 200), cv::Scalar(piece), cv::FILLED);
    cv::circle(gray, {160, 200}, 22, cv::Scalar(back), cv::FILLED);
    cv::circle(gray, {240, 200}, 22, cv::Scalar(back), cv::FILLED);
    return gray;
}

// Hexágono regular de circunradio 150.
cv::Mat hexagon(bool pieceIsDark = true) {
    const unsigned char piece = pieceIsDark ? kDark : kLight;
    const unsigned char back = pieceIsDark ? kLight : kDark;
    cv::Mat gray = canvas(400, back);
    std::vector<cv::Point> poly;
    for (int k = 0; k < 6; ++k) {
        const double a = 2.0 * kPi * k / 6.0;
        poly.emplace_back(cvRound(200.0 + 150.0 * std::cos(a)),
                          cvRound(200.0 + 150.0 * std::sin(a)));
    }
    cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{poly}, cv::Scalar(piece));
    return gray;
}

// Dos discos separados por un hueco conocido.
cv::Mat twoDiscs(double gap, bool pieceIsDark = true) {
    const unsigned char piece = pieceIsDark ? kDark : kLight;
    const unsigned char back = pieceIsDark ? kLight : kDark;
    cv::Mat gray = canvas(400, back);
    const int radius = 60;
    const int centreX = static_cast<int>(std::lround(200.0 - gap / 2.0 - radius));
    cv::circle(gray, {centreX, 200}, radius, cv::Scalar(piece), cv::FILLED);
    cv::circle(gray, {400 - centreX, 200}, radius, cv::Scalar(piece), cv::FILLED);
    return gray;
}

// Brida con `holes` agujeros repartidos sobre un primitivo de radio 120.
cv::Mat flange(int holes, bool pieceIsDark = true) {
    const unsigned char piece = pieceIsDark ? kDark : kLight;
    const unsigned char back = pieceIsDark ? kLight : kDark;
    cv::Mat gray = canvas(400, back);
    cv::circle(gray, {200, 200}, 170, cv::Scalar(piece), cv::FILLED, cv::LINE_AA);
    for (int k = 0; k < holes; ++k) {
        const double a = 2.0 * kPi * k / holes;
        cv::circle(gray,
                   {cvRound(200.0 + 120.0 * std::cos(a)), cvRound(200.0 + 120.0 * std::sin(a))},
                   16, cv::Scalar(back), cv::FILLED, cv::LINE_AA);
    }
    return gray;
}

// Bloque con la esquina superior derecha achaflanada (catetos conocidos).
cv::Mat chamferedCorner(double legX, double legY, bool pieceIsDark = true) {
    const unsigned char piece = pieceIsDark ? kDark : kLight;
    const unsigned char back = pieceIsDark ? kLight : kDark;
    cv::Mat gray(400, 500, CV_8UC1, cv::Scalar(back));
    const int cornerX = 320;
    const int cornerY = 140;
    const std::vector<cv::Point> poly{
        {60, cornerY},
        {static_cast<int>(std::lround(cornerX - legX)), cornerY},
        {cornerX, static_cast<int>(std::lround(cornerY + legY))},
        {cornerX, 360},
        {60, 360}};
    cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{poly}, cv::Scalar(piece));
    return gray;
}

// Bloque con la esquina superior derecha redondeada, tangente por construcción.
cv::Mat filletedCorner(double radius, bool pieceIsDark = true) {
    const unsigned char piece = pieceIsDark ? kDark : kLight;
    const unsigned char back = pieceIsDark ? kLight : kDark;
    cv::Mat gray(400, 500, CV_8UC1, cv::Scalar(back));
    const double cornerX = 320.0;
    const double cornerY = 140.0;
    const double centreX = cornerX - radius;
    const double centreY = cornerY + radius;
    std::vector<cv::Point> poly;
    poly.emplace_back(60, cvRound(cornerY));
    poly.emplace_back(cvRound(centreX), cvRound(cornerY));
    for (int k = 0; k <= 24; ++k) {
        const double a = -kPi / 2.0 + (kPi / 2.0) * k / 24.0;
        poly.emplace_back(cvRound(centreX + radius * std::cos(a)),
                          cvRound(centreY + radius * std::sin(a)));
    }
    poly.emplace_back(cvRound(cornerX), 360);
    poly.emplace_back(60, 360);
    cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{poly}, cv::Scalar(piece));
    return gray;
}

// Rectángulo girado, para los extremos de la silueta.
cv::Mat turnedRectangle(double longSide, double shortSide, double turnDeg) {
    cv::Mat gray = canvas(400);
    const cv::RotatedRect rect(cv::Point2f(200.0F, 200.0F),
                               cv::Size2f(static_cast<float>(longSide),
                                          static_cast<float>(shortSide)),
                               static_cast<float>(turnDeg));
    cv::Point2f corners[4];
    rect.points(corners);
    std::vector<cv::Point> poly;
    for (const auto& c : corners) {
        poly.emplace_back(cvRound(c.x), cvRound(c.y));
    }
    cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{poly}, cv::Scalar(kDark));
    return gray;
}

}  // namespace

// ---------------------------------------------------------------------------
// Bloque 1 — cada parámetro ajustable mueve la medida
// ---------------------------------------------------------------------------

TEST(ToolOptions, TheCaliperBandDecidesWhatFeatureIsMeasured) {
    // La banda del calíper no es un ajuste fino: decide QUÉ se mide. Sobre una
    // barra que solo es estrecha en una franja de 21 px, una banda fina mide la
    // franja (40 px) y una banda gorda promedia con el resto y mide la barra
    // ancha (100 px). Las dos son correctas para lo que promedian; el operador
    // tiene que saber que ese campo cambia la respuesta, no su precisión.
    const cv::Mat gray = steppedBar();
    const cv::Point2f from(60.0F, 150.0F);
    const cv::Point2f to(240.0F, 150.0F);

    const ToolRunResult thin = read(gray, CaliperGeometry{from, to, 1.0F});
    const ToolRunResult wide = read(gray, CaliperGeometry{from, to, 200.0F});
    expectKnobWorks("Caliper · banda 1 px -> 200 px", thin, wide, 5.0);
    EXPECT_NEAR(thin.measured, 40.0, 1.5) << thin.detail;
    EXPECT_NEAR(wide.measured, 100.0, 2.0) << wide.detail;
}

TEST(ToolOptions, TheCircleSearchBandAndRayCountBothDoTheirJob) {
    // Banda de búsqueda: es lo que decide si el borde cae dentro del anillo que
    // se explora. Con el radio trazado 20 px de más y una banda de 6, no se ve
    // nada; con 30 se recupera el mismo diámetro.
    const cv::Mat gray = disc(80.0);
    const cv::Point2f centre(200.0F, 200.0F);

    const ToolRunResult tooNarrow = read(gray, CircleGeometry{centre, 100.0F, 6.0F, 36});
    const ToolRunResult wideEnough = read(gray, CircleGeometry{centre, 100.0F, 30.0F, 36});
    expectKnobWorks("Círculo · banda 6 px -> 30 px", tooNarrow, wideEnough, 0.0);
    EXPECT_FALSE(tooNarrow.ok) << tooNarrow.detail;
    EXPECT_NEAR(wideEnough.measured, 160.0, 3.0) << wideEnough.detail;

    // Rayos: el diámetro no puede cambiar (sería un diámetro que depende del
    // muestreo), pero el número de puntos explorados sí, y eso es lo que se
    // comprueba: que el campo llega al ejecutor.
    const ToolRunResult few = read(gray, CircleGeometry{centre, 80.0F, 12.0F, 12});
    const ToolRunResult many = read(gray, CircleGeometry{centre, 80.0F, 12.0F, 180});
    std::printf("  Círculo · rayos 12 -> 180: %zu puntos -> %zu puntos (Ø %.2f -> %.2f)\n",
                few.overlayPoints.size(), many.overlayPoints.size(), few.measured,
                many.measured);
    EXPECT_GT(many.overlayPoints.size(), few.overlayPoints.size() + 100U);
    EXPECT_NEAR(few.measured, many.measured, 1.5)
        << "el diámetro no puede depender de cuántos rayos se tiren";
}

TEST(ToolOptions, TheRoundnessRayCountIsTheMeasurementAndNotJustSampling) {
    // En el Círculo los rayos son muestreo; en la Redondez son LA MEDIDA: lo que
    // se juzga es la forma, y una forma que no se muestrea no existe. Sobre un
    // disco con cinco lóbulos de 3 px, doce rayos no pueden ver los cinco
    // lóbulos y el número sale pequeño; con 360 aparece entero.
    const cv::Mat gray = disc(120.0, 5, 3.0);
    const cv::Point2f centre(200.0F, 200.0F);

    const ToolRunResult few = read(gray, RoundnessGeometry{centre, 120.0F, 14.0F, 12});
    const ToolRunResult many = read(gray, RoundnessGeometry{centre, 120.0F, 14.0F, 360});
    expectKnobWorks("Redondez · rayos 12 -> 360", few, many, 0.5);

    // Y la banda, igual que en el Círculo: si no llega al borde, no hay forma
    // que juzgar y se dice en vez de dar un número bonito.
    const ToolRunResult blind = read(gray, RoundnessGeometry{centre, 160.0F, 6.0F, 72});
    const ToolRunResult seeing = read(gray, RoundnessGeometry{centre, 160.0F, 50.0F, 72});
    expectKnobWorks("Redondez · banda 6 px -> 50 px", blind, seeing, 0.0);
    EXPECT_FALSE(blind.ok) << blind.detail;
    EXPECT_TRUE(seeing.ok) << seeing.detail;
}

TEST(ToolOptions, TheArcSearchBandAndRayCountBothDoTheirJob) {
    // Los tres puntos solo SITÚAN el arco; el radio se mide sobre el borde real
    // dentro de la banda. Se marcan a propósito sobre una circunferencia de
    // radio 130 concéntrica con el borde verdadero (radio 100): con una banda
    // corta no se alcanza el borde, y con una banda amplia sale el radio real.
    const cv::Mat gray = disc(100.0);
    const cv::Point2f centre(200.0F, 200.0F);
    const auto at = [&centre](double radius, double degrees) {
        const double a = degrees * kPi / 180.0;
        return cv::Point2f(static_cast<float>(centre.x + radius * std::cos(a)),
                           static_cast<float>(centre.y + radius * std::sin(a)));
    };

    const ToolRunResult blind =
        read(gray, ArcGeometry{at(130.0, 200.0), at(130.0, 250.0), at(130.0, 300.0), 8.0F, 24});
    const ToolRunResult seeing =
        read(gray, ArcGeometry{at(130.0, 200.0), at(130.0, 250.0), at(130.0, 300.0), 50.0F, 24});
    expectKnobWorks("Arco · banda 8 px -> 50 px", blind, seeing, 0.0);
    EXPECT_FALSE(blind.ok) << blind.detail;
    EXPECT_NEAR(seeing.measured, 100.0, 4.0) << seeing.detail;

    const ToolRunResult few =
        read(gray, ArcGeometry{at(100.0, 200.0), at(100.0, 250.0), at(100.0, 300.0), 20.0F, 5});
    const ToolRunResult many =
        read(gray, ArcGeometry{at(100.0, 200.0), at(100.0, 250.0), at(100.0, 300.0), 20.0F, 180});
    std::printf("  Arco · rayos 5 -> 180: %zu puntos -> %zu puntos (R %.2f -> %.2f)\n",
                few.overlayPoints.size(), many.overlayPoints.size(), few.measured,
                many.measured);
    EXPECT_GT(many.overlayPoints.size(), few.overlayPoints.size() + 50U);
}

TEST(ToolOptions, TheReachOfEveryAxialToolDecidesWhetherItSeesTheEdgeAtAll) {
    // Eje torneado, Eje medio, Rosca y Ranura comparten gesto y comparten este
    // parámetro: hasta dónde buscar el borde a cada lado del eje trazado. Con
    // una barra de 100 px de diámetro, un alcance de 10 px no llega a ningún
    // flanco y ninguna de las cuatro puede medir.
    const cv::Mat bar = turnedBar();
    const cv::Point2f from(120.0F, 250.0F);
    const cv::Point2f to(380.0F, 250.0F);

    const ToolRunResult shaftShort = read(bar, ShaftGeometry{from, to, 10.0F, 32});
    const ToolRunResult shaftLong = read(bar, ShaftGeometry{from, to, 80.0F, 32});
    expectKnobWorks("Eje/Diámetro · alcance 10 px -> 80 px", shaftShort, shaftLong, 0.0);
    EXPECT_FALSE(shaftShort.ok) << shaftShort.detail;
    EXPECT_NEAR(shaftLong.measured, 100.0, 2.0) << shaftLong.detail;

    const ToolRunResult axisShort = read(bar, MedianAxisGeometry{from, to, 10.0F, 32});
    const ToolRunResult axisLong = read(bar, MedianAxisGeometry{from, to, 80.0F, 32});
    expectKnobWorks("Eje medio · alcance 10 px -> 80 px", axisShort, axisLong, 0.0);
    EXPECT_FALSE(axisShort.ok) << axisShort.detail;
    EXPECT_TRUE(axisLong.ok) << axisLong.detail;

    const cv::Mat grooved = barWithGroove(40.0, 20.0);
    const ToolRunResult grooveShort =
        read(grooved, GrooveGeometry{{100.0F, 250.0F}, {400.0F, 250.0F}, 10.0F, 120,
                                     GrooveMeasure::Width});
    const ToolRunResult grooveLong =
        read(grooved, GrooveGeometry{{100.0F, 250.0F}, {400.0F, 250.0F}, 80.0F, 120,
                                     GrooveMeasure::Width});
    expectKnobWorks("Ranura · alcance 10 px -> 80 px", grooveShort, grooveLong, 0.0);
    EXPECT_FALSE(grooveShort.ok) << grooveShort.detail;
    EXPECT_NEAR(grooveLong.measured, 40.0, 4.0) << grooveLong.detail;

    const cv::Mat screw = threadBar(40.0, 120.0, 80.0, 60.0);
    const ToolRunResult threadShort =
        read(screw, ThreadGeometry{{150.0F, 250.0F}, {550.0F, 250.0F}, 10.0F, 400});
    const ToolRunResult threadLong =
        read(screw, ThreadGeometry{{150.0F, 250.0F}, {550.0F, 250.0F}, 110.0F, 400});
    expectKnobWorks("Rosca · alcance 10 px -> 110 px", threadShort, threadLong, 0.0);
    EXPECT_FALSE(threadShort.ok) << threadShort.detail;
    EXPECT_NEAR(threadLong.measured, 40.0, 2.0) << threadLong.detail;
}

TEST(ToolOptions, TheStationCountIsTheResolutionOfTheGrooveAndOfTheThread) {
    // El número de cortes es la RESOLUCIÓN axial, y la herramienta lo dice: una
    // ranura más estrecha que unos pocos cortes no se puede medir. Con 20 cortes
    // sobre 300 px el paso es de 15,8 px y una ranura de 24 px no tiene los
    // flancos resueltos; con 200 el paso baja a 1,5 px y sale la cota.
    const cv::Mat grooved = barWithGroove(24.0, 12.0);
    const ToolRunResult coarse =
        read(grooved, GrooveGeometry{{100.0F, 250.0F}, {400.0F, 250.0F}, 80.0F, 20,
                                     GrooveMeasure::Width});
    const ToolRunResult fine =
        read(grooved, GrooveGeometry{{100.0F, 250.0F}, {400.0F, 250.0F}, 80.0F, 200,
                                     GrooveMeasure::Width});
    expectKnobWorks("Ranura · cortes 20 -> 200", coarse, fine, 0.0);
    EXPECT_FALSE(coarse.ok) << coarse.detail;
    EXPECT_NEAR(fine.measured, 24.0, 3.0) << fine.detail;

    // En la rosca, el paso sale del PERIODO del perfil, así que los cortes son
    // la resolución de esa medida: con 40 cortes sobre 400 px cada vuelta de una
    // rosca de 24 px ocupa dos muestras y el paso que sale no es el de la pieza.
    const cv::Mat screw = threadBar(24.0, 120.0, 90.0, 60.0);
    const ToolRunResult few =
        read(screw, ThreadGeometry{{150.0F, 250.0F}, {550.0F, 250.0F}, 110.0F, 40});
    const ToolRunResult many =
        read(screw, ThreadGeometry{{150.0F, 250.0F}, {550.0F, 250.0F}, 110.0F, 400});
    expectKnobWorks("Rosca · cortes 40 -> 400", few, many, 1.0);
    EXPECT_NEAR(many.measured, 24.0, 1.5) << many.detail;
    EXPECT_GT(std::abs(few.measured - 24.0), 1.5)
        << "con 40 cortes el paso no puede salir bien, y sale: " << few.detail;
}

TEST(ToolOptions, TheShaftDiameterDoesNotDependOnHowManyStationsAreUsed) {
    // La otra cara de lo mismo, y hay que dejarla fijada: en el Eje torneado los
    // cortes NO pueden cambiar el diámetro de una pieza sana —sería un diámetro
    // que depende del muestreo—. Lo que cambian es la robustez y la rectitud.
    // Sin este test, «cortes» parecería un parámetro muerto en el Eje.
    const cv::Mat bar = turnedBar();
    const ToolRunResult few =
        read(bar, ShaftGeometry{{120.0F, 250.0F}, {380.0F, 250.0F}, 80.0F, 5});
    const ToolRunResult many =
        read(bar, ShaftGeometry{{120.0F, 250.0F}, {380.0F, 250.0F}, 80.0F, 200});
    std::printf("  Eje · cortes 5 -> 200: Ø %.4f -> %.4f\n", few.measured, many.measured);
    EXPECT_NEAR(few.measured, many.measured, 1.0)
        << "few: " << few.detail << " / many: " << many.detail;
}

TEST(ToolOptions, TheGearRadiiAndRayCountBothDecideWhetherTheTeethAreSeen) {
    // Los dos radios delimitan la corona de dientes: si el anillo cae por dentro
    // de la raíz no hay ningún borde que ver, y la herramienta lo dice en vez de
    // contar cero dientes.
    const cv::Mat wheel = gearWheel(40, 140.0, 110.0);
    const cv::Point2f centre(350.0F, 350.0F);

    const ToolRunResult inside = read(wheel, GearGeometry{centre, 40.0F, 90.0F, 1440});
    const ToolRunResult onTheCrown = read(wheel, GearGeometry{centre, 100.0F, 165.0F, 1440});
    expectKnobWorks("Engranaje · radios (40,90) -> (100,165)", inside, onTheCrown, 0.0);
    EXPECT_FALSE(inside.ok) << inside.detail;
    EXPECT_NEAR(onTheCrown.measured, 40.0, 0.5) << onTheCrown.detail;

    // Y los rayos deciden si el recuento es CORRECTO, que es peor que decidir si
    // se puede medir: con 180 rayos y 40 dientes salen 4,5 rayos por diente y el
    // periodo se alía, dando la MITAD.
    //
    // Antes ese 20 se publicaba como medida —era lo que se comparaba con la
    // tolerancia— mientras el aviso se quedaba en el texto. Ahora no se publica
    // ningún recuento cuando los dos métodos discrepan doblándose: los dientes
    // son la identidad de la rueda y uno de más o de menos ya no es la misma
    // pieza.
    const ToolRunResult few = read(wheel, GearGeometry{centre, 100.0F, 165.0F, 180});
    const ToolRunResult many = read(wheel, GearGeometry{centre, 100.0F, 165.0F, 1440});
    EXPECT_NEAR(many.measured, 40.0, 0.5) << many.detail;

    EXPECT_FALSE(few.ok) << few.detail;
    EXPECT_NE(few.detail.find("no es fiable"), std::string::npos) << few.detail;
    // Y el motivo trae los DOS números, que es lo que permite entender qué pasó.
    EXPECT_NE(few.detail.find("20"), std::string::npos) << few.detail;
    EXPECT_NE(few.detail.find("40"), std::string::npos) << few.detail;
}

TEST(ToolOptions, TheScanLengthAndTheScanCountAreTwoDifferentKnobs) {
    // Son los dos campos de todas las herramientas de borde (Borde liso,
    // Rectitud, Orientación, Rebabas), y hacen cosas distintas:
    //   · el LARGO decide qué desviaciones caben dentro de la ventana;
    //   · el NÚMERO decide cuántos puntos del borde se miran.
    // Con una mella de 26 px y una ventana de 16, la mella queda fuera y no hay
    // número de escaneos que la rescate.
    const cv::Mat gray = notchedEdge(26.0);
    const cv::Point2f from(60.0F, 200.0F);
    const cv::Point2f to(340.0F, 200.0F);

    const ToolRunResult shortWindow = read(gray, StraightnessGeometry{from, to, 16.0F, 60});
    const ToolRunResult longWindow = read(gray, StraightnessGeometry{from, to, 70.0F, 60});
    expectKnobWorks("Rectitud · largo de escaneo 16 px -> 70 px", shortWindow, longWindow,
                    5.0);
    EXPECT_LT(shortWindow.measured, 3.0) << "con la ventana corta la mella no se ve";
    EXPECT_GT(longWindow.measured, 20.0) << longWindow.detail;

    // El número de escaneos, sobre la MISMA escena y con la ventana ya amplia,
    // cambia cuántos puntos del borde entran en el juicio.
    const ToolRunResult sparse = read(gray, StraightnessGeometry{from, to, 70.0F, 8});
    const ToolRunResult dense = read(gray, StraightnessGeometry{from, to, 70.0F, 200});
    std::printf("  Rectitud · escaneos 8 -> 200: %zu puntos -> %zu puntos\n",
                sparse.overlayPoints.size(), dense.overlayPoints.size());
    EXPECT_GT(dense.overlayPoints.size(), sparse.overlayPoints.size() + 50U);
}

TEST(ToolOptions, TheDefectHeightAndThePieceSideAreBothRealKnobsOfTheDefectTool) {
    // Altura mínima: es LA definición de la herramienta —«cuántos defectos de
    // más de esto hay»— y el recuento tiene que seguirla.
    const cv::Mat gray = notchedEdge(6.0);
    const cv::Point2f from(60.0F, 200.0F);
    const cv::Point2f to(340.0F, 200.0F);

    const ToolRunResult sensitive =
        read(gray, EdgeDefectsGeometry{from, to, 40.0F, 120, 1.5F, true});
    const ToolRunResult blunt =
        read(gray, EdgeDefectsGeometry{from, to, 40.0F, 120, 20.0F, true});
    expectKnobWorks("Rebabas · altura mínima 1,5 px -> 20 px", sensitive, blunt, 0.5);
    EXPECT_GE(sensitive.measured, 1.0) << sensitive.detail;
    EXPECT_DOUBLE_EQ(blunt.measured, 0.0) << blunt.detail;

    // De qué lado está el material decide si un defecto es REBABA o MELLA. No es
    // cosmético: son dos averías distintas y se arreglan de forma distinta.
    const ToolRunResult asDark =
        read(gray, EdgeDefectsGeometry{from, to, 40.0F, 120, 1.5F, true});
    const ToolRunResult asLight =
        read(gray, EdgeDefectsGeometry{from, to, 40.0F, 120, 1.5F, false});
    std::printf("  Rebabas · pieza oscura -> clara:\n    %s\n    %s\n", asDark.detail.c_str(),
                asLight.detail.c_str());
    EXPECT_NE(asDark.detail.find("mella"), std::string::npos) << asDark.detail;
    EXPECT_NE(asLight.detail.find("rebaba"), std::string::npos) << asLight.detail;
}

TEST(ToolOptions, TheNominalAngleTurnsParallelismIntoPerpendicularity) {
    // Paralelismo, perpendicularidad y angularidad son la misma medida con
    // distinto ángulo nominal, así que ese campo tiene que cambiar tanto el
    // nombre de la cota como el número: la banda se orienta según el datum
    // GIRADO ese ángulo.
    const cv::Mat gray = notchedEdge(0.0);
    ToolConfig datum;
    datum.type = ToolType::Ruler;
    datum.name = "cara A";
    datum.geometryJson = toJson(ToolGeometry(RulerGeometry{{40.0F, 320.0F}, {360.0F, 320.0F}}));
    datum.toleranceMax = 1e9;

    const auto runWithNominal = [&](float nominalDeg) {
        ToolConfig tolerated =
            configOf(ToolGeometry(OrientationGeometry{
                         {60.0F, 200.0F}, {340.0F, 200.0F}, 40.0F, 80, nominalDeg}),
                     "orientacion");
        tolerated.reference = "cara A";
        ToolRunResult found;
        for (const auto& r : runTools(gray, kIdentity, {datum, tolerated})) {
            if (r.name == "orientacion") {
                found = r;
            }
        }
        return found;
    };

    const ToolRunResult parallel = runWithNominal(0.0F);
    const ToolRunResult perpendicular = runWithNominal(90.0F);
    expectKnobWorks("Orientación · nominal 0° -> 90°", parallel, perpendicular, 5.0);
    EXPECT_NE(parallel.detail.find("paralelismo"), std::string::npos) << parallel.detail;
    EXPECT_NE(perpendicular.detail.find("perpendicularidad"), std::string::npos)
        << perpendicular.detail;
}

TEST(ToolOptions, EveryMeasureSelectorPicksADifferentNumber) {
    // Cuatro herramientas dan varios números y una tolerancia sola: el selector
    // decide cuál se juzga. Si el selector no llegara al ejecutor, la tolerancia
    // vigilaría siempre el primero de la lista y el operador no se enteraría.
    const cv::Mat grooved = barWithGroove(40.0, 20.0);
    const auto groove = [&grooved](GrooveMeasure measure) {
        return read(grooved, GrooveGeometry{{100.0F, 250.0F}, {400.0F, 250.0F}, 80.0F, 120,
                                            measure});
    };
    const ToolRunResult width = groove(GrooveMeasure::Width);
    const ToolRunResult depth = groove(GrooveMeasure::Depth);
    const ToolRunResult root = groove(GrooveMeasure::RootDiameter);
    std::printf("  Ranura · ancho=%.2f  profundidad=%.2f  Ø fondo=%.2f\n", width.measured,
                depth.measured, root.measured);
    EXPECT_NEAR(width.measured, 40.0, 4.0) << width.detail;
    EXPECT_NEAR(depth.measured, 20.0, 3.0) << depth.detail;
    EXPECT_NEAR(root.measured, 60.0, 4.0) << root.detail;

    const cv::Mat chamfer = chamferedCorner(80.0, 30.0);
    const auto chamferAs = [&chamfer](ChamferMeasure measure) {
        return read(chamfer, ChamferGeometry{{250.0F, 200.0F}, 320.0F, 260.0F, measure, true});
    };
    const ToolRunResult angle = chamferAs(ChamferMeasure::Angle);
    const ToolRunResult legLong = chamferAs(ChamferMeasure::LegLong);
    const ToolRunResult legShort = chamferAs(ChamferMeasure::LegShort);
    std::printf("  Chaflán · ángulo=%.2f  cateto mayor=%.2f  cateto menor=%.2f\n",
                angle.measured, legLong.measured, legShort.measured);
    EXPECT_NEAR(legLong.measured, 80.0, 5.0) << legLong.detail;
    EXPECT_NEAR(legShort.measured, 30.0, 5.0) << legShort.detail;
    EXPECT_EQ(angle.kind, MeasuredKind::Angle);
    EXPECT_NE(legLong.kind, MeasuredKind::Angle);

    const cv::Mat fillet = filletedCorner(50.0);
    const ToolRunResult radius =
        read(fillet, FilletGeometry{{250.0F, 200.0F}, 320.0F, 260.0F, FilletMeasure::Radius,
                                    true});
    const ToolRunResult tangency =
        read(fillet, FilletGeometry{{250.0F, 200.0F}, 320.0F, 260.0F, FilletMeasure::Tangency,
                                    true});
    std::printf("  Acuerdo · radio=%.2f  tangencia=%.2f\n", radius.measured,
                tangency.measured);
    EXPECT_NEAR(radius.measured, 50.0, 6.0) << radius.detail;
    EXPECT_LT(tangency.measured, 8.0) << tangency.detail;
    EXPECT_EQ(tangency.kind, MeasuredKind::Angle);

    const cv::Mat rect = turnedRectangle(200.0, 80.0, 30.0);
    const ToolRunResult narrow =
        read(rect, ExtremesGeometry{{200.0F, 200.0F}, 380.0F, 380.0F, ExtremeMeasure::MinWidth,
                                    true});
    const ToolRunResult span =
        read(rect, ExtremesGeometry{{200.0F, 200.0F}, 380.0F, 380.0F, ExtremeMeasure::MaxSpan,
                                    true});
    std::printf("  Máx./mín. · anchura mínima=%.2f  diámetro máximo=%.2f\n", narrow.measured,
                span.measured);
    EXPECT_NEAR(narrow.measured, 80.0, 3.0) << narrow.detail;
    EXPECT_NEAR(span.measured, std::hypot(200.0, 80.0), 4.0) << span.detail;

    const cv::Mat square = squareWithHoles(true);
    const auto region = [&square](RegionMeasure measure) {
        return read(square, RegionGeometry{{200.0F, 200.0F}, 260.0F, 260.0F, measure, true});
    };
    std::printf("  Región · área=%.1f  perímetro=%.1f  agujeros=%.0f\n",
                region(RegionMeasure::Area).measured,
                region(RegionMeasure::Perimeter).measured,
                region(RegionMeasure::HoleCount).measured);
    EXPECT_NEAR(region(RegionMeasure::HoleCount).measured, 2.0, 0.01);
    EXPECT_GT(region(RegionMeasure::Area).measured, 30000.0);
}

TEST(ToolOptions, ThePolygonEpsilonDecidesTheSideCount) {
    // Epsilon es el único parámetro de la herramienta Lados y decide TODO: con
    // uno minúsculo el contorno rasterizado deja de ser un polígono claro, y con
    // uno enorme la figura se queda sin vértices. En medio están los seis lados
    // del hexágono.
    const cv::Mat gray = hexagon();
    const auto sides = [&gray](float epsilon) {
        return read(gray, PolygonGeometry{{200.0F, 200.0F}, 340.0F, 340.0F, epsilon, true});
    };

    const ToolRunResult tiny = sides(0.0005F);
    const ToolRunResult sane = sides(0.02F);
    const ToolRunResult huge = sides(0.40F);
    std::printf("  Lados · eps 0,0005 -> %s %.0f | eps 0,02 -> %s %.0f | eps 0,40 -> %s %.0f\n",
                tiny.ok ? "OK" : "NG", tiny.measured, sane.ok ? "OK" : "NG", sane.measured,
                huge.ok ? "OK" : "NG", huge.measured);
    EXPECT_DOUBLE_EQ(sane.measured, 6.0) << sane.detail;
    EXPECT_TRUE(sane.ok) << sane.detail;
    EXPECT_FALSE(huge.ok) << "con un epsilon enorme no queda polígono: " << huge.detail;
    EXPECT_NE(sane.measured, huge.measured);
}

TEST(ToolOptions, TheExpectedHoleCountTurnsAMissingHoleIntoTheDefect) {
    // Con 0 se miden los agujeros que haya; con un número puesto, que no
    // coincida ES el defecto y la herramienta deja de medir el reparto angular.
    const cv::Mat gray = flange(6);
    const auto pattern = [&gray](int expected) {
        return read(gray, BoltPatternGeometry{{200.0F, 200.0F}, 380.0F, 380.0F, expected, true});
    };

    const ToolRunResult free = pattern(0);
    const ToolRunResult right = pattern(6);
    const ToolRunResult wrong = pattern(8);
    expectKnobWorks("Patrón · agujeros esperados 0 -> 8", free, wrong, 0.0);
    EXPECT_TRUE(free.ok) << free.detail;
    EXPECT_NEAR(right.measured, free.measured, 1e-6)
        << "declarar el número correcto no puede cambiar la medida";
    EXPECT_FALSE(wrong.ok) << wrong.detail;
    EXPECT_NE(wrong.detail.find("se esperaban 8"), std::string::npos) << wrong.detail;
}

TEST(ToolOptions, TheMinimumBlobAreaAndThePolarityBothFilterWhatIsCounted) {
    cv::Mat gray = canvas(300);
    cv::circle(gray, {120, 150}, 12, cv::Scalar(kDark), cv::FILLED);
    cv::circle(gray, {160, 150}, 12, cv::Scalar(kDark), cv::FILLED);
    cv::circle(gray, {200, 150}, 3, cv::Scalar(kDark), cv::FILLED);

    const ToolRunResult all =
        read(gray, BlobGeometry{{160.0F, 150.0F}, 200.0F, 120.0F, 10.0F, true});
    const ToolRunResult filtered =
        read(gray, BlobGeometry{{160.0F, 150.0F}, 200.0F, 120.0F, 200.0F, true});
    expectKnobWorks("Blob · área mínima 10 px² -> 200 px²", all, filtered, 0.5);
    EXPECT_DOUBLE_EQ(all.measured, 3.0) << all.detail;
    EXPECT_DOUBLE_EQ(filtered.measured, 2.0) << filtered.detail;

    // Y la polaridad: buscar manchas claras donde las hay oscuras no puede dar
    // el mismo recuento.
    const ToolRunResult inverted =
        read(gray, BlobGeometry{{160.0F, 150.0F}, 200.0F, 120.0F, 10.0F, false});
    expectKnobWorks("Blob · manchas oscuras -> claras", all, inverted, 0.5);

    // El blob poligonal comparte los dos parámetros y tiene que comportarse
    // igual: es la misma herramienta con la región dibujada a mano.
    const std::vector<cv::Point2f> region{{60.0F, 90.0F}, {260.0F, 90.0F},
                                          {260.0F, 210.0F}, {60.0F, 210.0F}};
    const ToolRunResult polyAll = read(gray, PolyBlobGeometry{region, 10.0F, true});
    const ToolRunResult polyFiltered = read(gray, PolyBlobGeometry{region, 200.0F, true});
    expectKnobWorks("Blob poligonal · área mínima 10 px² -> 200 px²", polyAll, polyFiltered,
                    0.5);
    EXPECT_DOUBLE_EQ(polyAll.measured, 3.0) << polyAll.detail;
}

TEST(ToolOptions, EveryPolaritySwitchChangesWhatTheToolSees) {
    // «La pieza es lo oscuro» es un parámetro de siete herramientas de silueta.
    // Sobre una pieza CLARA sobre fondo oscuro, dejarlo puesto en oscuro hace
    // que se mida el fondo, y ninguna de las siete puede dar lo mismo.
    const cv::Mat lightPiece = squareWithHoles(false);

    const ToolRunResult regionWrong =
        read(lightPiece, RegionGeometry{{200.0F, 200.0F}, 260.0F, 260.0F, RegionMeasure::Area,
                                        true});
    const ToolRunResult regionRight =
        read(lightPiece, RegionGeometry{{200.0F, 200.0F}, 260.0F, 260.0F, RegionMeasure::Area,
                                        false});
    expectKnobWorks("Región · pieza oscura -> clara", regionWrong, regionRight, 100.0);
    EXPECT_NEAR(regionRight.measured, 200.0 * 200.0 - 2.0 * kPi * 22.0 * 22.0, 900.0)
        << regionRight.detail;

    const ToolRunResult symmetryWrong =
        read(lightPiece, SymmetryGeometry{{200.0F, 200.0F}, 260.0F, 260.0F, true});
    const ToolRunResult symmetryRight =
        read(lightPiece, SymmetryGeometry{{200.0F, 200.0F}, 260.0F, 260.0F, false});
    expectKnobWorks("Simetría · pieza oscura -> clara", symmetryWrong, symmetryRight, 0.005);

    const cv::Mat lightHexagon = hexagon(false);
    const ToolRunResult polygonWrong =
        read(lightHexagon, PolygonGeometry{{200.0F, 200.0F}, 340.0F, 340.0F, 0.02F, true});
    const ToolRunResult polygonRight =
        read(lightHexagon, PolygonGeometry{{200.0F, 200.0F}, 340.0F, 340.0F, 0.02F, false});
    expectKnobWorks("Lados · pieza oscura -> clara", polygonWrong, polygonRight, 0.5);
    EXPECT_DOUBLE_EQ(polygonRight.measured, 6.0) << polygonRight.detail;

    const cv::Mat lightPair = twoDiscs(30.0, false);
    const ToolRunResult clearanceWrong =
        read(lightPair, ClearanceGeometry{{200.0F, 200.0F}, 380.0F, 200.0F, true});
    const ToolRunResult clearanceRight =
        read(lightPair, ClearanceGeometry{{200.0F, 200.0F}, 380.0F, 200.0F, false});
    expectKnobWorks("Holgura · pieza oscura -> clara", clearanceWrong, clearanceRight, 1.0);
    EXPECT_NEAR(clearanceRight.measured, 30.0, 3.0) << clearanceRight.detail;

    const cv::Mat lightRect = turnedRectangle(200.0, 80.0, 30.0);
    cv::Mat invertedRect;
    cv::bitwise_not(lightRect, invertedRect);
    const ToolRunResult extremesWrong =
        read(invertedRect, ExtremesGeometry{{200.0F, 200.0F}, 380.0F, 380.0F,
                                            ExtremeMeasure::MinWidth, true});
    const ToolRunResult extremesRight =
        read(invertedRect, ExtremesGeometry{{200.0F, 200.0F}, 380.0F, 380.0F,
                                            ExtremeMeasure::MinWidth, false});
    expectKnobWorks("Máx./mín. · pieza oscura -> clara", extremesWrong, extremesRight, 5.0);
    EXPECT_NEAR(extremesRight.measured, 80.0, 3.0) << extremesRight.detail;

    const cv::Mat lightFlange = flange(6, false);
    const ToolRunResult patternWrong =
        read(lightFlange, BoltPatternGeometry{{200.0F, 200.0F}, 380.0F, 380.0F, 0, true});
    const ToolRunResult patternRight =
        read(lightFlange, BoltPatternGeometry{{200.0F, 200.0F}, 380.0F, 380.0F, 0, false});
    expectKnobWorks("Patrón de agujeros · pieza oscura -> clara", patternWrong, patternRight,
                    0.0);
    EXPECT_TRUE(patternRight.ok) << patternRight.detail;

    const cv::Mat lightChamfer = chamferedCorner(80.0, 30.0, false);
    const ToolRunResult chamferWrong =
        read(lightChamfer, ChamferGeometry{{250.0F, 200.0F}, 320.0F, 260.0F,
                                           ChamferMeasure::LegLong, true});
    const ToolRunResult chamferRight =
        read(lightChamfer, ChamferGeometry{{250.0F, 200.0F}, 320.0F, 260.0F,
                                           ChamferMeasure::LegLong, false});
    expectKnobWorks("Chaflán · pieza oscura -> clara", chamferWrong, chamferRight, 0.0);
    EXPECT_NEAR(chamferRight.measured, 80.0, 6.0) << chamferRight.detail;

    const cv::Mat lightFillet = filletedCorner(50.0, false);
    const ToolRunResult filletWrong =
        read(lightFillet, FilletGeometry{{250.0F, 200.0F}, 320.0F, 260.0F,
                                         FilletMeasure::Radius, true});
    const ToolRunResult filletRight =
        read(lightFillet, FilletGeometry{{250.0F, 200.0F}, 320.0F, 260.0F,
                                         FilletMeasure::Radius, false});
    expectKnobWorks("Acuerdo · pieza oscura -> clara", filletWrong, filletRight, 0.0);
    EXPECT_NEAR(filletRight.measured, 50.0, 6.0) << filletRight.detail;
}

TEST(ToolOptions, ThePositionAxisAndItsTheoreticalPointBothChangeTheNumber) {
    // El selector de eje: la misma desviación juzgada en radial, en X o en Y son
    // tres números distintos, y la tolerancia solo tiene sentido si el operador
    // sabe cuál está vigilando.
    const cv::Mat gray = canvas(400, 128);
    const auto axis = [&gray](PositionAxis which) {
        return read(gray, PositionGeometry{{30.0F, 40.0F}, which, {0.0F, 0.0F}});
    };
    const ToolRunResult radial = axis(PositionAxis::Radial);
    const ToolRunResult onX = axis(PositionAxis::X);
    const ToolRunResult onY = axis(PositionAxis::Y);
    std::printf("  Posición · radial=%.2f  X=%.2f  Y=%.2f\n", radial.measured, onX.measured,
                onY.measured);
    EXPECT_NEAR(radial.measured, 50.0, 1e-6) << radial.detail;
    EXPECT_NEAR(onX.measured, 30.0, 1e-6) << onX.detail;
    EXPECT_NEAR(onY.measured, 40.0, 1e-6) << onY.detail;

    // Y el punto teórico, con marco de referencia declarado: es contra lo que se
    // mide la posición verdadera, así que moverlo tiene que mover la cota.
    ToolConfig datumA;
    datumA.type = ToolType::Ruler;
    datumA.name = "datum A";
    datumA.geometryJson = toJson(ToolGeometry(RulerGeometry{{0.0F, 0.0F}, {200.0F, 0.0F}}));
    datumA.toleranceMax = 1e9;
    ToolConfig datumB;
    datumB.type = ToolType::Ruler;
    datumB.name = "datum B";
    datumB.geometryJson = toJson(ToolGeometry(RulerGeometry{{0.0F, -50.0F}, {0.0F, 50.0F}}));
    datumB.toleranceMax = 1e9;

    const auto withNominal = [&](cv::Point2f nominal) {
        ToolConfig config = configOf(
            ToolGeometry(PositionGeometry{{103.0F, 54.0F}, PositionAxis::Radial, nominal}),
            "posicion");
        config.reference = "datum A";
        config.reference2 = "datum B";
        ToolRunResult found;
        for (const auto& r : runTools(gray, kIdentity, {datumA, datumB, config})) {
            if (r.name == "posicion") {
                found = r;
            }
        }
        return found;
    };
    const ToolRunResult atOrigin = withNominal({0.0F, 0.0F});
    const ToolRunResult atDrawing = withNominal({100.0F, 50.0F});
    expectKnobWorks("Posición verdadera · teórico (0;0) -> (100;50)", atOrigin, atDrawing,
                    50.0);
    EXPECT_NEAR(atDrawing.measured, 10.0, 1e-3)
        << "el rasgo está a (3;4) del teórico: Ø de zona = 10";
}

// ---------------------------------------------------------------------------
// Bloque 2 — rangos muertos: el campo se mueve y el ejecutor lo ignora
// ---------------------------------------------------------------------------

TEST(ToolOptionsDeadRange, TheRayCountSpinDoesNothingOutsideTheRangeTheExecutorAccepts) {
    // El único campo numérico del panel va de 1 a 1000 sea cual sea la
    // herramienta seleccionada, y el ejecutor recorta los rayos del Círculo a
    // [8, 360]. O sea: 993 de los 1000 valores que el operador puede elegir
    // hacen exactamente lo mismo que sus extremos, sin que nada se lo diga.
    //
    // El test lo fija comparando AL BIT: si algún día el recorte cambia, esto
    // salta y hay que decidir qué rango ofrece el panel.
    const cv::Mat gray = disc(80.0);
    const cv::Point2f centre(200.0F, 200.0F);
    const auto rays = [&](int count) {
        return read(gray, CircleGeometry{centre, 80.0F, 12.0F, count});
    };

    const ToolRunResult atOne = rays(1);
    const ToolRunResult atEight = rays(8);
    const ToolRunResult atTopOfRange = rays(360);
    const ToolRunResult beyond = rays(1000);
    std::printf("  Círculo · rayos    1 -> Ø%.6f (%zu puntos)\n", atOne.measured,
                atOne.overlayPoints.size());
    std::printf("  Círculo · rayos    8 -> Ø%.6f (%zu puntos)\n", atEight.measured,
                atEight.overlayPoints.size());
    std::printf("  Círculo · rayos  360 -> Ø%.6f (%zu puntos)\n", atTopOfRange.measured,
                atTopOfRange.overlayPoints.size());
    std::printf("  Círculo · rayos 1000 -> Ø%.6f (%zu puntos)\n", beyond.measured,
                beyond.overlayPoints.size());

    EXPECT_DOUBLE_EQ(atOne.measured, atEight.measured)
        << "por debajo de 8 rayos el campo no hace nada";
    EXPECT_EQ(atOne.overlayPoints.size(), atEight.overlayPoints.size());
    EXPECT_DOUBLE_EQ(atTopOfRange.measured, beyond.measured)
        << "por encima de 360 rayos el campo no hace nada";
    EXPECT_EQ(atTopOfRange.overlayPoints.size(), beyond.overlayPoints.size());
}

TEST(ToolOptionsDeadRange, TheScanCountSpinDoesNothingAboveTwoHundred) {
    // Lo mismo con el Borde liso: el panel llega a 1000 y el ejecutor recorta en
    // 200. Del 201 al 1000 el operador está moviendo un campo muerto.
    const cv::Mat gray = notchedEdge(4.0);
    const auto scans = [&](int count) {
        return read(gray, EdgeFlawGeometry{{60.0F, 200.0F}, {340.0F, 200.0F}, 30.0F, count});
    };

    const ToolRunResult atTop = scans(200);
    const ToolRunResult beyond = scans(1000);
    std::printf("  Borde liso · escaneos  200 -> %.6f (%zu puntos)\n", atTop.measured,
                atTop.overlayPoints.size());
    std::printf("  Borde liso · escaneos 1000 -> %.6f (%zu puntos)\n", beyond.measured,
                beyond.overlayPoints.size());
    EXPECT_DOUBLE_EQ(atTop.measured, beyond.measured);
    EXPECT_EQ(atTop.overlayPoints.size(), beyond.overlayPoints.size());

    // Y por abajo: 1 y 2 escaneos no existen, se recortan a 3.
    const ToolRunResult atOne = scans(1);
    const ToolRunResult atThree = scans(3);
    std::printf("  Borde liso · escaneos    1 -> %.6f | 3 -> %.6f\n", atOne.measured,
                atThree.measured);
    EXPECT_DOUBLE_EQ(atOne.measured, atThree.measured);
}

TEST(ToolOptionsDeadRange, ABandWiderThanTheImageIsRefusedWithItsReason) {
    // La banda promedia perpendicularmente al trazo. Si es más ancha que la
    // imagen deja de promediar el borde y promedia TODA la escena: medido, con
    // 1000 px de banda sobre una imagen de 300 devolvía 100,00 donde la zona que
    // el operador cruzó mide 40,00 — un número creíble sacado de otra geometría,
    // y sin una palabra en el detalle.
    //
    // No se recorta en silencio: una banda así es un error de la plantilla, no
    // una preferencia, y corregirla por dentro dejaría al operador con un número
    // que no es el de lo que trazó.
    const cv::Mat gray = steppedBar();
    const cv::Point2f from(60.0F, 150.0F);
    const cv::Point2f to(240.0F, 150.0F);

    const ToolRunResult sane = read(gray, CaliperGeometry{from, to, 10.0F});
    const ToolRunResult absurd = read(gray, CaliperGeometry{from, to, 1000.0F});
    std::printf("  Caliper · banda 10 px -> %.3f | banda 1000 px (imagen de 300) -> %.3f\n",
                sane.measured, absurd.measured);
    EXPECT_NEAR(sane.measured, 40.0, 1.5) << sane.detail;

    EXPECT_FALSE(absurd.ok) << absurd.detail;
    EXPECT_NE(absurd.detail.find("banda"), std::string::npos)
        << "se niega, pero sin decir que el problema es la banda: " << absurd.detail;
    // Y una banda negativa tampoco se toma por su valor absoluto, que es lo que
    // pasaba antes: daba el mismo número que la positiva.
    const ToolRunResult negative = read(gray, CaliperGeometry{from, to, -12.0F});
    EXPECT_FALSE(negative.ok) << negative.detail;
}

TEST(ToolOptionsDeadRange, AbsurdValuesAreSilentlyCorrectedInsteadOfRefused) {
    // Una plantilla escrita a mano (o exportada de otra versión) puede traer
    // cero rayos, cero escaneos o un epsilon negativo. El ejecutor no revienta
    // —bien— pero tampoco lo dice: los recorta y mide como si nada, así que el
    // fichero y lo que se mide dejan de coincidir en silencio.
    const cv::Mat gray = disc(80.0);
    const ToolRunResult zeroRays =
        read(gray, CircleGeometry{{200.0F, 200.0F}, 80.0F, 12.0F, 0});
    std::printf("  Círculo · rayos 0 -> %s Ø%.3f (%zu puntos)\n", zeroRays.ok ? "OK" : "NG",
                zeroRays.measured, zeroRays.overlayPoints.size());
    EXPECT_TRUE(zeroRays.ok) << zeroRays.detail;
    EXPECT_GE(zeroRays.overlayPoints.size(), 8U) << "cero rayos se recorta a ocho";

    const ToolRunResult zeroScans =
        read(notchedEdge(4.0), EdgeFlawGeometry{{60.0F, 200.0F}, {340.0F, 200.0F}, 30.0F, 0});
    std::printf("  Borde liso · escaneos 0 -> %s %.3f (%zu puntos)\n",
                zeroScans.ok ? "OK" : "NG", zeroScans.measured,
                zeroScans.overlayPoints.size());
    EXPECT_GE(zeroScans.overlayPoints.size(), 3U) << "cero escaneos se recorta a tres";

    // Un epsilon negativo no se rechaza: `max(1.0, eps*perímetro)` lo convierte
    // en un epsilon de 1 px, que sobre un hexágono rasterizado no da un polígono
    // claro. El operador ve «no es un polígono claro» y no «epsilon inválido».
    const ToolRunResult negativeEpsilon =
        read(hexagon(), PolygonGeometry{{200.0F, 200.0F}, 340.0F, 340.0F, -0.5F, true});
    std::printf("  Lados · epsilon -0,5 -> %s %.0f lados: %s\n",
                negativeEpsilon.ok ? "OK" : "NG", negativeEpsilon.measured,
                negativeEpsilon.detail.c_str());
    EXPECT_EQ(negativeEpsilon.detail.find("epsilon inválido"), std::string::npos)
        << "hoy no se rechaza, se recorta: " << negativeEpsilon.detail;

    // Y una banda de búsqueda negativa tampoco: el anillo se explora del revés y
    // sale un diámetro perfectamente creíble.
    const ToolRunResult negativeBand =
        read(gray, CircleGeometry{{200.0F, 200.0F}, 80.0F, -12.0F, 36});
    std::printf("  Círculo · banda -12 px -> %s Ø%.3f\n", negativeBand.ok ? "OK" : "NG",
                negativeBand.measured);
    EXPECT_TRUE(negativeBand.ok)
        << "hoy una banda negativa mide igual que la positiva: " << negativeBand.detail;
}

// ---------------------------------------------------------------------------
// Bloque 3 — parámetros que faltan
// ---------------------------------------------------------------------------

TEST(ToolOptionsMissing, TheOnlyKnobThatSavesADeepNotchIsNotTheOneTheDescriptionNames) {
    // La descripción del Borde liso dice: «solo ve lo que cae dentro del largo
    // de escaneo (campo Escaneos/largo) [...] súbelo si esperas defectos
    // grandes». El único campo del panel se llama «Escaneos» y mueve el NÚMERO
    // de escaneos, no el largo.
    //
    // Este test enseña que son knobs distintos y que el que salva la pieza es
    // justo el que no está: con una mella de 26 px y una ventana de 16, subir
    // los escaneos de 20 a 200 no cambia nada, y subir el largo sí.
    const cv::Mat gray = notchedEdge(26.0);
    const cv::Point2f from(60.0F, 200.0F);
    const cv::Point2f to(340.0F, 200.0F);

    const ToolRunResult fewScans = read(gray, EdgeFlawGeometry{from, to, 16.0F, 20});
    const ToolRunResult manyScans = read(gray, EdgeFlawGeometry{from, to, 16.0F, 200});
    const ToolRunResult longerWindow = read(gray, EdgeFlawGeometry{from, to, 70.0F, 20});
    std::printf("  Borde liso · mella de 26 px:\n");
    std::printf("    largo 16 · escaneos  20 -> %s desv. %.3f\n", fewScans.ok ? "OK" : "NG",
                fewScans.measured);
    std::printf("    largo 16 · escaneos 200 -> %s desv. %.3f\n", manyScans.ok ? "OK" : "NG",
                manyScans.measured);
    std::printf("    largo 70 · escaneos  20 -> %s desv. %.3f\n",
                longerWindow.ok ? "OK" : "NG", longerWindow.measured);

    // Con la ventana corta, la herramienta DECLARABA el borde perfectamente liso
    // —desviación 0,000 y veredicto OK— sobre una pieza con una mella de 26 px.
    // Los escaneos que caen sobre la mella no encuentran borde, porque está más
    // allá de media ventana, y se descartaban en silencio.
    //
    // Ahora se niega y manda subir el largo. Sigue faltando el campo para
    // tocarlo —que es lo que este test documenta— pero ya no se puede dar por
    // buena una pieza mala sin que nadie se entere.
    EXPECT_FALSE(fewScans.ok) << fewScans.detail;
    EXPECT_NE(fewScans.detail.find("sube el largo de escaneo"), std::string::npos)
        << fewScans.detail;

    // Y subir los ESCANEOS sigue sin rescatarla: el knob que salva la pieza es
    // el otro, que es justo lo que este test existe para demostrar.
    EXPECT_FALSE(manyScans.ok) << manyScans.detail;
    EXPECT_LT(manyScans.measured, 0.5)
        << "subir los escaneos NO rescata la mella: " << manyScans.detail;

    EXPECT_GT(longerWindow.measured, 20.0)
        << "el largo sí la rescata: " << longerWindow.detail;
    EXPECT_TRUE(longerWindow.ok) << longerWindow.detail;
}

TEST(ToolOptions, TheProfileToolNeedsToBeToldWhichSideThePieceIsOn) {
    // Todas las herramientas de silueta llevan «la pieza es lo oscuro»: Región,
    // Simetría, Lados, Holgura, Máx./mín., Chaflán, Acuerdo, Patrón. El Perfil
    // de línea era la ÚNICA que no: binarizaba siempre con THRESH_BINARY_INV, o
    // sea daba por hecho que la pieza es la oscura.
    //
    // Con una pieza clara sobre fondo oscuro —el contraluz de toda la vida—
    // comparaba el nominal contra el FONDO, y lo que devolvía no era un aviso:
    // era un número de perfil enorme con toda la pinta de ser una medida.
    std::vector<cv::Point2f> nominal;
    for (int k = 0; k < 180; ++k) {
        const double a = 2.0 * kPi * k / 180.0;
        nominal.emplace_back(static_cast<float>(200.0 + 80.0 * std::cos(a)),
                             static_cast<float>(200.0 + 80.0 * std::sin(a)));
    }

    cv::Mat darkPiece = canvas(400, kLight);
    cv::circle(darkPiece, {200, 200}, 80, cv::Scalar(kDark), cv::FILLED, cv::LINE_AA);
    cv::Mat lightPiece;
    cv::bitwise_not(darkPiece, lightPiece);

    const ToolRunResult onDark = read(darkPiece, ProfileGeometry{nominal});
    const ToolRunResult onLight = read(lightPiece, ProfileGeometry{nominal});
    std::printf("  Perfil · pieza oscura -> %s zona %.3f\n", onDark.ok ? "OK" : "NG",
                onDark.measured);
    std::printf("  Perfil · pieza clara  -> %s zona %.3f  (%s)\n", onLight.ok ? "OK" : "NG",
                onLight.measured, onLight.detail.c_str());

    EXPECT_LT(onDark.measured, 4.0) << "la pieza contra su propio contorno: " << onDark.detail;
    EXPECT_GT(onLight.measured, 5.0 * std::max(onDark.measured, 1.0))
        << "con la polaridad equivocada tiene que salir un número claramente distinto";

    // Y la mitad que faltaba: DICIÉNDOLE de qué lado está la pieza, la misma
    // imagen clara mide igual de bien que la oscura. Sin esta comprobación, el
    // campo podría existir y no hacer nada.
    ProfileGeometry lightGeometry{nominal};
    lightGeometry.darkPiece = false;
    const ToolRunResult told = read(lightPiece, lightGeometry);
    std::printf("  Perfil · pieza clara con polaridad puesta -> %s zona %.3f\n",
                told.ok ? "OK" : "NG", told.measured);
    EXPECT_LT(told.measured, 4.0)
        << "con la polaridad correcta la pieza clara tiene que medirse como la oscura: "
        << told.detail;
    EXPECT_NEAR(told.measured, onDark.measured, 2.0)
        << "la misma pieza, invertida y con la polaridad puesta, tiene que dar lo mismo";
}

TEST(ToolOptionsMissing, TheStraightnessWindowMattersJustAsMuchAndHasNoFieldEither) {
    // Rectitud, Orientación y Rebabas tienen los mismos dos campos que el Borde
    // liso (largo y número de escaneos) y ninguno de los dos se puede tocar
    // desde el editor. El largo decide qué desviaciones caben en la ventana, y
    // aquí está el número que lo demuestra sobre las tres.
    const cv::Mat gray = notchedEdge(26.0);
    const cv::Point2f from(60.0F, 200.0F);
    const cv::Point2f to(340.0F, 200.0F);

    const ToolRunResult straightShort = read(gray, StraightnessGeometry{from, to, 16.0F, 60});
    const ToolRunResult straightLong = read(gray, StraightnessGeometry{from, to, 70.0F, 60});

    // Con la ventana corta, la Rectitud DABA CERO —un borde perfecto según la
    // norma— sobre un borde con una mella de 26 px, y su detalle no mencionaba
    // ningún tramo ciego. Los escaneos que caen sobre la mella no encuentran
    // borde, porque está más allá de media ventana, y se descartaban en
    // silencio: la banda mínima se calculaba solo con el tramo bueno.
    //
    // Ahora se niega y dice qué hacer, igual que ya hacía «Rebabas y mellas».
    // Sigue faltando el campo para tocar el largo, que es lo que este test
    // documenta; lo que ya no falta es la red que impide firmar una cota que
    // nadie ha medido.
    EXPECT_FALSE(straightShort.ok) << straightShort.detail;
    EXPECT_NE(straightShort.detail.find("sube el largo de escaneo"), std::string::npos)
        << straightShort.detail;
    // Y con la ventana larga sí mide, y mide la mella.
    EXPECT_TRUE(straightLong.ok) << straightLong.detail;
    EXPECT_GT(straightLong.measured, 15.0)
        << "con ventana suficiente tiene que ver la mella de 26 px";

    const ToolRunResult defectsShort =
        read(gray, EdgeDefectsGeometry{from, to, 16.0F, 120, 1.5F, true});
    const ToolRunResult defectsLong =
        read(gray, EdgeDefectsGeometry{from, to, 70.0F, 120, 1.5F, true});
    std::printf("  Rebabas · largo 16 -> %s (%s)\n", defectsShort.ok ? "OK" : "NG",
                defectsShort.detail.c_str());
    std::printf("  Rebabas · largo 70 -> %s (%s)\n", defectsLong.ok ? "OK" : "NG",
                defectsLong.detail.c_str());
    // Con la ventana corta la herramienta NO da el borde por limpio: detecta el
    // tramo ciego y manda subir el largo. Es lo correcto, y a la vez la prueba
    // de que ese campo tiene que existir en el panel.
    EXPECT_FALSE(defectsShort.ok) << defectsShort.detail;
    EXPECT_NE(defectsShort.detail.find("sube el largo de escaneo"), std::string::npos)
        << defectsShort.detail;
    EXPECT_TRUE(defectsLong.ok) << defectsLong.detail;
}

// ---------------------------------------------------------------------------
// Bloque 4 — la descripción y el parámetro tienen que decir lo mismo
// ---------------------------------------------------------------------------

TEST(ToolOptionsDescription, EveryFieldTheDescriptionPromisesIsAParameterThatGetsSaved) {
    // Una descripción que nombra un campo y una geometría que no lo guarda es
    // una promesa que no se puede cumplir. Se comprueba de la única forma que no
    // miente: la clave tiene que aparecer en el JSON con el que la herramienta
    // se persiste, porque lo que no se guarda no se puede configurar.
    struct Promise {
        ToolType type;
        const char* saidInTheDescription;  // texto literal de `toolTypeDescription`
        const char* jsonKey;               // clave con la que se persiste
        ToolGeometry geometry;
    };
    const std::vector<Promise> promises{
        {ToolType::EdgeFlaw, "campo\n"
                             "Escaneos/largo",
         "scanLen", EdgeFlawGeometry{}},
        {ToolType::Polygon, "El campo Epsilon", "eps", PolygonGeometry{}},
        {ToolType::EdgeDefects, "El campo Altura mínima (px)", "minH", EdgeDefectsGeometry{}},
        {ToolType::BoltPattern, "Con Agujeros esperados puesto", "holes",
         BoltPatternGeometry{}},
        {ToolType::Orientation, "el que\npongas en el campo Ángulo", "nominal",
         OrientationGeometry{}},
        {ToolType::Groove, "sube el número de cortes", "stations", GrooveGeometry{}},
        {ToolType::Extremes, "elige en Medida cuál vigilar", "mode", ExtremesGeometry{}},
        {ToolType::Chamfer, "Elige en Medida cuál de los tres números", "mode",
         ChamferGeometry{}},
        {ToolType::Region, "elige qué medir", "mode", RegionGeometry{}},
    };

    for (const auto& promise : promises) {
        const std::string description = toolTypeDescription(promise.type);
        EXPECT_NE(description.find(promise.saidInTheDescription), std::string::npos)
            << toolTypeLabel(promise.type) << ": la descripción ya no dice «"
            << promise.saidInTheDescription << "»";
        const std::string json = toJson(promise.geometry);
        const std::string key = std::string("\"") + promise.jsonKey + "\"";
        EXPECT_NE(json.find(key), std::string::npos)
            << toolTypeLabel(promise.type) << ": promete un campo que no se guarda ("
            << promise.jsonKey << "): " << json;
        std::printf("  %-22s promete «%s» y guarda %s\n", toolTypeLabel(promise.type),
                    promise.jsonKey, key.c_str());
    }
}

TEST(ToolOptionsDescription, TheEpsilonTheDescriptionCallsThousandthsIsStoredAsAFraction) {
    // La descripción de Lados dice «El campo Epsilon (en milésimas del
    // perímetro)» y el modelo guarda una FRACCIÓN: el valor por defecto es 0,02,
    // que son 20 milésimas. Son diez veces distintos, y el único campo numérico
    // del panel es un entero.
    //
    // O sea: si ese campo se añadiera al spin tal como está, teclear «20»
    // —creyendo que son milésimas, como dice la descripción— escribiría un
    // epsilon de 20 veces el perímetro y la figura se evaporaría. El test fija
    // qué significa de verdad el número.
    const PolygonGeometry defaults;
    std::printf("  Lados · epsilon por defecto = %.4f (fracción) = %.1f milésimas\n",
                static_cast<double>(defaults.epsilonFraction),
                1000.0 * static_cast<double>(defaults.epsilonFraction));
    EXPECT_FLOAT_EQ(defaults.epsilonFraction, 0.02F);
    EXPECT_NE(std::string(toolTypeDescription(ToolType::Polygon))
                  .find("en milésimas del perímetro"),
              std::string::npos);

    const cv::Mat gray = hexagon();
    const ToolRunResult asFraction =
        read(gray, PolygonGeometry{{200.0F, 200.0F}, 340.0F, 340.0F, 0.02F, true});
    const ToolRunResult asIfThousandths =
        read(gray, PolygonGeometry{{200.0F, 200.0F}, 340.0F, 340.0F, 20.0F, true});
    std::printf("  Lados · eps 0,02 -> %s %.0f lados | eps 20 (lo que teclearía quien lea "
                "«milésimas») -> %s (%s)\n",
                asFraction.ok ? "OK" : "NG", asFraction.measured,
                asIfThousandths.ok ? "OK" : "NG", asIfThousandths.detail.c_str());
    EXPECT_DOUBLE_EQ(asFraction.measured, 6.0) << asFraction.detail;
    EXPECT_FALSE(asIfThousandths.ok)
        << "con epsilon = 20 veces el perímetro no queda figura: " << asIfThousandths.detail;
}

// ---------------------------------------------------------------------------
// Bloque 5 — valores por defecto
// ---------------------------------------------------------------------------

TEST(ToolOptionsDefaults, TheDefaultSamplingOfEveryToolIsInsideWhatTheExecutorAccepts) {
    // Un valor por defecto fuera del rango que el ejecutor admite es un
    // parámetro que nace muerto: la herramienta recién dibujada mediría con otro
    // valor distinto del que guarda su plantilla. Se comprueba midiendo, no
    // leyendo: con el muestreo por defecto y una escena a medida, cada
    // herramienta tiene que dar su cota.
    const cv::Mat wheel = gearWheel(20, 140.0, 110.0);
    const GearGeometry gearDefaults;
    std::printf("  Engranaje · por defecto rin=%.0f rout=%.0f rayos=%d\n",
                static_cast<double>(gearDefaults.innerRadius),
                static_cast<double>(gearDefaults.outerRadius), gearDefaults.rayCount);
    GearGeometry gear;
    gear.center = {350.0F, 350.0F};
    gear.innerRadius = 100.0F;
    gear.outerRadius = 165.0F;  // los radios los pone el trazo, no el valor por defecto
    const ToolRunResult teeth = read(wheel, gear);
    EXPECT_NEAR(teeth.measured, 20.0, 0.5) << teeth.detail;

    // Círculo: radio y banda por defecto (50 y 12) sobre un disco de radio 50.
    CircleGeometry circle;
    circle.center = {200.0F, 200.0F};
    const ToolRunResult diameter = read(disc(50.0), circle);
    std::printf("  Círculo · por defecto r=%.0f banda=%.0f rayos=%d -> Ø%.2f\n",
                static_cast<double>(circle.radius), static_cast<double>(circle.searchBand),
                circle.rayCount, diameter.measured);
    EXPECT_NEAR(diameter.measured, 100.0, 3.0) << diameter.detail;

    // Redondez: mismos valores por defecto salvo los rayos (72).
    RoundnessGeometry roundness;
    roundness.center = {200.0F, 200.0F};
    const ToolRunResult form = read(disc(50.0), roundness);
    std::printf("  Redondez · por defecto rayos=%d -> %.3f px de zona\n", roundness.rayCount,
                form.measured);
    EXPECT_TRUE(form.ok) << form.detail;
    EXPECT_LT(form.measured, 4.0) << "un disco redondo con los valores de fábrica: "
                                  << form.detail;

    // Eje torneado, Eje medio y Ranura: alcance 60 px y cortes de fábrica sobre
    // la barra de 100 px de diámetro.
    ShaftGeometry shaft;
    shaft.axisFrom = {120.0F, 250.0F};
    shaft.axisTo = {380.0F, 250.0F};
    const ToolRunResult shaftResult = read(turnedBar(), shaft);
    std::printf("  Eje · por defecto alcance=%.0f cortes=%d -> Ø%.2f\n",
                static_cast<double>(shaft.searchBand), shaft.stations,
                shaftResult.measured);
    EXPECT_NEAR(shaftResult.measured, 100.0, 2.0) << shaftResult.detail;

    MedianAxisGeometry median;
    median.axisFrom = {120.0F, 250.0F};
    median.axisTo = {380.0F, 250.0F};
    const ToolRunResult medianResult = read(turnedBar(), median);
    EXPECT_TRUE(medianResult.ok) << medianResult.detail;

    GrooveGeometry groove;
    groove.axisFrom = {100.0F, 250.0F};
    groove.axisTo = {400.0F, 250.0F};
    const ToolRunResult grooveResult = read(barWithGroove(40.0, 20.0), groove);
    std::printf("  Ranura · por defecto alcance=%.0f cortes=%d -> ancho %.2f\n",
                static_cast<double>(groove.searchBand), groove.stations,
                grooveResult.measured);
    EXPECT_NEAR(grooveResult.measured, 40.0, 4.0) << grooveResult.detail;

    // Borde liso: largo 16 px y 20 escaneos sobre un borde con una mella de 4 px.
    EdgeFlawGeometry flaw;
    flaw.p0 = {60.0F, 200.0F};
    flaw.p1 = {340.0F, 200.0F};
    const ToolRunResult flawResult = read(notchedEdge(4.0), flaw);
    std::printf("  Borde liso · por defecto largo=%.0f escaneos=%d -> desv. %.2f\n",
                static_cast<double>(flaw.scanLength), flaw.scanCount, flawResult.measured);
    EXPECT_GT(flawResult.measured, 2.0)
        << "con los valores de fábrica una mella de 4 px tiene que verse: "
        << flawResult.detail;
}

TEST(ToolOptionsDefaults, TheDefaultSearchBandOfACircleIsTiedToTheRadiusItWasDrawnWith) {
    // El fallo que este proyecto ya se comió una vez: una banda de búsqueda
    // sacada de un radio que no era el de la pieza. El lienzo la calcula ahora
    // como `min(12, radio/2)`, así que un círculo pequeño no nace con una banda
    // que se traga su propio centro. Se comprueba con la cuenta y midiendo.
    for (const double radius : {8.0, 20.0, 60.0, 150.0}) {
        const float band = std::min(12.0F, static_cast<float>(radius) / 2.0F);
        EXPECT_LE(band, radius / 2.0) << "la banda no puede llegar al centro";

        CircleGeometry g;
        g.center = {200.0F, 200.0F};
        g.radius = static_cast<float>(radius);
        g.searchBand = band;
        const ToolRunResult result = read(disc(radius), g);
        std::printf("  Círculo · r=%5.0f banda=%5.1f -> %s Ø%.2f (esperado %.0f)\n", radius,
                    static_cast<double>(band), result.ok ? "OK" : "NG", result.measured,
                    2.0 * radius);
        EXPECT_NEAR(result.measured, 2.0 * radius, std::max(3.0, 0.06 * radius))
            << result.detail;
    }
}

// ---------------------------------------------------------------------------
// Qué número vigila la tolerancia lo elige el operador, no el orden del enum
// ---------------------------------------------------------------------------

TEST(ToolOptions, EveryToolThatPublishesSeveralNumbersLetsYouPickTheOneWithTheTolerance) {
    // Cinco herramientas calculan de dos a seis cosas y solo una lleva la
    // tolerancia. El desplegable del editor estaba habilitado únicamente para la
    // Región —el panel preguntaba «¿es una Región?»— así que Ranura, Chaflán,
    // Acuerdo y Máx./mín. vigilaban SIEMPRE la primera de su enum: el ancho, el
    // ángulo, el radio y la anchura mínima. El operador veía los tres números en
    // el detalle y no tenía forma de decir cuál era la cota.
    //
    // Ahora lo responde el MODELO, igual que ya pasaba con las referencias. Este
    // test recorre TODAS las geometrías para que la siguiente herramienta con
    // varias medidas no se quede fuera en silencio.
    struct Case {
        const char* name;
        ToolGeometry geometry;
        std::size_t expected;
    };
    const std::vector<Case> cases{
        {"Región", RegionGeometry{}, 6},
        {"Ranura", GrooveGeometry{}, 3},
        {"Chaflán", ChamferGeometry{}, 3},
        {"Acuerdo", FilletGeometry{}, 2},
        {"Máx./mín.", ExtremesGeometry{}, 2},
    };

    for (const auto& c : cases) {
        const MeasureChoices choices = measureChoicesOf(c.geometry);
        std::printf("  %-11s -> %zu medidas, la puesta es la %d\n", c.name,
                    choices.options.size(), choices.current);
        EXPECT_EQ(choices.options.size(), c.expected) << c.name;
        for (const auto& option : choices.options) {
            EXPECT_FALSE(option.label.empty()) << c.name << ": una opción sin nombre";
        }

        // Y se puede CAMBIAR: un desplegable que no escribe nada sería peor que
        // no tenerlo.
        ToolGeometry mutable_ = c.geometry;
        for (const auto& option : choices.options) {
            ASSERT_TRUE(setMeasureChoice(mutable_, option.value)) << c.name;
            EXPECT_EQ(measureChoicesOf(mutable_).current, option.value) << c.name;
        }
    }

    // Una herramienta que NO elige medida lo dice con una lista vacía, y así el
    // panel puede preguntar en vez de saberlo.
    EXPECT_TRUE(measureChoicesOf(RulerGeometry{}).options.empty());
    EXPECT_TRUE(measureChoicesOf(CircleGeometry{}).options.empty());
    ToolGeometry ruler = RulerGeometry{};
    EXPECT_FALSE(setMeasureChoice(ruler, 0))
        << "no se puede elegir medida en una herramienta que no tiene";
}

TEST(ToolOptions, AnUnknownMeasureIsRefusedInsteadOfSilentlyCorrected) {
    // Un valor imposible NO se corrige en silencio. Es un pecado que esta capa
    // ya cometía en otro sitio —el eje de la Posición cae a Radial ante
    // cualquier número raro— y que hace que un fichero corrupto mida otra cosa
    // sin que nadie se entere.
    ToolGeometry groove = GrooveGeometry{};
    ASSERT_TRUE(setMeasureChoice(groove, 2));
    const int before = measureChoicesOf(groove).current;

    EXPECT_FALSE(setMeasureChoice(groove, 99));
    EXPECT_FALSE(setMeasureChoice(groove, -1));
    EXPECT_EQ(measureChoicesOf(groove).current, before)
        << "un valor desconocido movió la medida en vez de rechazarse";
}
