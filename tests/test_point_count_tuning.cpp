// EL OPERADOR NO PODÍA TOCAR EL NÚMERO DE PUNTOS, Y MEDIR CONSUMÍA MUCHO.
//
// Doce herramientas —Círculo, Arco, Redondez, Engranaje, Borde liso, Rebabas y
// mellas, Rectitud, Orientación, Eje/Diámetro, Eje medio, Ranura y Rosca—
// exploran su geometría lanzando un número fijo de rayos, escaneos o cortes
// (`CircleGeometry::rayCount`, `EdgeFlawGeometry::scanCount`,
// `ShaftGeometry::stations`...). Ese número lo fijaba la propuesta automática
// o el valor de fábrica escrito en `tool_geometry.h`, y el operador no tenía
// dónde tocarlo: ni en el editor de plantilla ni en la vista en vivo. Y cada
// punto de más cuesta una llamada a `detectEdges`/`axialProfile`, así que
// subir el muestreo para una pieza difícil también subía el coste de medir
// TODAS las piezas, sin que hubiera forma de bajarlo donde no hiciera falta.
//
// Esta suite mide las tres preguntas que hacían falta para poder abrir ese
// campo con conocimiento de causa, en vez de a ciegas:
//
//   1. Inventario: qué herramientas tienen el parámetro, su valor de fábrica y
//      su rango sensato — el MISMO que ya aplicaba `tool_executor.cpp` al
//      recortar en silencio, ahora expuesto por `pointCountOf` (modelo, no
//      panel: la misma razón por la que existe `measureChoicesOf`).
//   2. Coste: cuánto tarda medir con 18/36/72/180 puntos sobre un fotograma de
//      1920x1080, para las tres familias del parámetro (rayos, escaneos,
//      cortes) — las nueve restantes comparten el mismo mecanismo interno (un
//      `detectEdges`/`axialProfile` por punto, visto en `tool_executor.cpp`),
//      así que la misma escala de coste les aplica.
//   3. Precisión: cuánto se mueve el Ø medido de un disco de Ø conocido según
//      cuántos rayos se lancen, con el ruido que mete cualquier cámara real.
//
// Y dos comprobaciones de que el campo, ya editable, se porta como el resto de
// la plantilla: el valor sobrevive a guardar/cargar (serialización JSON) y una
// plantilla ANTIGUA sin este campo —de antes de este cambio— carga con el
// valor de fábrica en vez de romperse. Esta última destapó una avería de
// verdad: Borde liso trataba "scans" como OBLIGATORIO en el JSON (a
// diferencia de sus once hermanas, que ya usaban `numberOr` con su valor de
// fábrica), así que una plantilla antigua sin ese campo no cargaba —
// "Geometría corrupta: falta 'scans'"— en vez de medir con los 20 escaneos de
// siempre. Se corrigió en `geometryFromJson` para que se comporte como las
// demás.
//
// NÚMEROS MEDIDOS (máquina de desarrollo, build release, un solo hilo):
//
//   Coste, fotograma 1920x1080, más rápida de 10 vueltas tras 1 de
//   calentamiento (ms):
//                                18 pts   36 pts   72 pts  180 pts   180/18
//     Círculo       (rayos)      0,116    0,205    0,370    0,931     8,0x
//     Redondez      (rayos)      0,085    0,152    0,386    0,672     7,9x
//     Eje/Diámetro  (cortes)     0,178    0,447    0,613    1,512     8,5x
//     Ranura        (cortes)     0,151    0,290    0,539    1,237     8,2x
//     Rectitud      (escaneos)   0,027    0,045    0,079    0,181     6,7x
//     Borde liso    (escaneos)   0,027    0,043    0,078    0,179     6,6x
//     Orientación   (escaneos)   0,035    0,051    0,085    0,185     5,3x
//
//   Precisión, disco de Ø 600 px conocido, ruido gaussiano de cámara
//   (sigma=18, semilla fija) — Ø medido y error absoluto/relativo:
//     8   rayos: 601,09 px (1,09 px · 0,18 %)
//     18  rayos: 601,27 px (1,27 px · 0,21 %)
//     36  rayos: 601,22 px (1,22 px · 0,20 %)
//     72  rayos: 601,23 px (1,23 px · 0,20 %)
//     180 rayos: 600,92 px (0,92 px · 0,15 %)
//     360 rayos: 600,92 px (0,92 px · 0,15 %)
//
//   LA SORPRESA: con este ruido, el Ø MEDIO apenas se mueve entre 8 y 360
//   rayos (0,18 % -> 0,15 % de error: menos de dos décimas de milímetro en
//   una pieza de 600 mm). El ajuste robusto de círculo ya compensa el ruido
//   aleatorio incluso con pocos puntos. Lo que de verdad se pierde al bajar
//   el número no es la precisión MEDIA sino la ROBUSTEZ ante un defecto
//   LOCAL —una rebaba, un reflejo, un tramo tapado—: entre 8 muestras ese
//   defecto pesa un 12,5 % del ajuste; entre 180, menos de un 1 %. Y en
//   Redondez el efecto es directo y ya estaba medido en
//   `test_tool_options.cpp` (`TheRoundnessRayCountIsTheMeasurementAndNotJust
//   Sampling`): con un disco de 5 lóbulos de 3 px, 12 rayos no pueden ver los
//   lóbulos y 360 sí — ahí pocos puntos no dan una forma "un poco peor", dan
//   una forma DISTINTA.
//
// Si estos números cambian, hay que volver a correr esta suite y actualizar
// tanto este comentario como el tooltip de `pointCountTooltip` en
// tool_geometry.cpp, que cita las mismas cifras para el operador.

#include <gtest/gtest.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <regex>
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
constexpr unsigned char kDark = 40;
constexpr unsigned char kLight = 220;

// Fixture identidad: coordenadas de pieza == coordenadas de imagen. Aquí se
// prueban las opciones del parámetro, no el enganche a la pieza.
const Fixture kIdentity{{0.0F, 0.0F}, 0.0};

ToolConfig configOf(const ToolGeometry& geometry, const std::string& name = "sonda") {
    ToolConfig config;
    config.type = typeOf(geometry);
    config.name = name;
    config.geometryJson = toJson(geometry);
    return config;
}

// Corre pasando por `toJson`/`geometryFromJson`, igual que en producción: un
// valor que no se persistiera se perdería aquí.
ToolRunResult read(const cv::Mat& gray, const ToolGeometry& geometry) {
    const auto result = runTool(gray, kIdentity, configOf(geometry));
    EXPECT_TRUE(result.isOk()) << result.error().message;
    return result.isOk() ? result.value() : ToolRunResult{};
}

// --- Escenas de 1920x1080, a tamaño real de cámara --------------------------

constexpr int kFrameW = 1920;
constexpr int kFrameH = 1080;

cv::Mat bigDisc(double radius, double noiseSigma = 0.0, unsigned seed = 1) {
    cv::Mat gray(kFrameH, kFrameW, CV_8UC1, cv::Scalar(kLight));
    cv::circle(gray, {kFrameW / 2, kFrameH / 2}, cvRound(radius), cv::Scalar(kDark),
              cv::FILLED, cv::LINE_AA);
    if (noiseSigma <= 0.0) {
        return gray;
    }
    // Ruido gaussiano determinista: lo que mete cualquier cámara real por
    // vibración, temperatura del sensor o compresión, con semilla fija para
    // que la prueba sea reproducible.
    cv::RNG rng(seed);
    cv::Mat noise(gray.size(), CV_16SC1);
    rng.fill(noise, cv::RNG::NORMAL, 0.0, noiseSigma);
    cv::Mat wide;
    gray.convertTo(wide, CV_16SC1);
    wide += noise;
    cv::Mat clipped;
    wide.convertTo(clipped, CV_8UC1);
    return clipped;
}

// Barra de torno horizontal que ocupa casi todo el fotograma: eje en
// (kFrameH/2), diámetro fijo, para las cuatro herramientas de estaciones.
cv::Mat bigTurnedBar(double diameter) {
    cv::Mat gray(kFrameH, kFrameW, CV_8UC1, cv::Scalar(kLight));
    cv::rectangle(gray, cv::Rect(60, kFrameH / 2 - cvRound(diameter / 2.0), kFrameW - 120,
                                cvRound(diameter)),
                 cv::Scalar(kDark), cv::FILLED);
    return gray;
}

// Borde recto horizontal que cruza casi todo el ancho, para las cuatro
// herramientas de escaneo perpendicular.
cv::Mat bigStraightEdge() {
    cv::Mat gray(kFrameH, kFrameW, CV_8UC1, cv::Scalar(kLight));
    cv::rectangle(gray, cv::Rect(0, kFrameH / 2, kFrameW, kFrameH / 2), cv::Scalar(kDark),
                 cv::FILLED);
    return gray;
}

// La vuelta más rápida de varias, no la media: una vuelta solo puede salir más
// lenta de lo que cuesta el código, nunca más rápida (mismo criterio que
// `test_engine.cpp`). La primera vuelta es calentamiento y no cuenta.
double fastestMs(const cv::Mat& gray, const std::vector<ToolConfig>& configs, int reps = 11) {
    double best = 1e300;
    for (int r = 0; r < reps; ++r) {
        const auto started = std::chrono::steady_clock::now();
        const auto results = runTools(gray, kIdentity, configs);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started)
                              .count();
        EXPECT_EQ(results.size(), configs.size());
        if (r > 0) {
            best = std::min(best, ms);
        }
    }
    return best;
}

// Quita "clave": valor del JSON, simulando una plantilla escrita ANTES de que
// ese campo existiera. Textual y no vía FileStorage porque lo que se quiere
// probar es justo la ausencia de la clave, no reconstruir el documento.
std::string withoutKey(const std::string& json, const char* key) {
    const std::regex withTrailingComma(std::string("\"") + key + "\"\\s*:\\s*[-0-9.eE]+\\s*,\\s*");
    std::string result = std::regex_replace(json, withTrailingComma, "");
    if (result == json) {
        const std::regex withLeadingComma(std::string(",?\\s*\"") + key +
                                          "\"\\s*:\\s*[-0-9.eE]+");
        result = std::regex_replace(json, withLeadingComma, "");
    }
    return result;
}

const char* jsonKeyFor(ToolType type) {
    const std::string noun = pointCountNoun(type);
    if (noun == "rayos") {
        return "rays";
    }
    if (noun == "escaneos") {
        return "scans";
    }
    if (noun == "cortes") {
        return "stations";
    }
    return "";
}

// Las doce herramientas con puntos de medida, con su geometría de fábrica
// (válida, situada sobre las escenas de arriba) y el rango que se espera de
// `pointCountOf` — el mismo, número a número, que aplica cada `runXxx` en
// tool_executor.cpp.
struct PointToolCase {
    const char* label;
    ToolGeometry geometry;  // valores de fábrica, geometría situada en escena
    int expectedDefault;
    int expectedMin;
    int expectedMax;
};

std::vector<PointToolCase> pointToolCases() {
    const cv::Point2f discCentre(kFrameW / 2.0F, kFrameH / 2.0F);
    const cv::Point2f barFrom(200.0F, kFrameH / 2.0F);
    const cv::Point2f barTo(kFrameW - 200.0F, kFrameH / 2.0F);
    const cv::Point2f edgeFrom(200.0F, kFrameH / 2.0F);
    const cv::Point2f edgeTo(kFrameW - 200.0F, kFrameH / 2.0F);
    const auto onCircle = [&](double radius, double degrees) {
        const double a = degrees * kPi / 180.0;
        return cv::Point2f(static_cast<float>(discCentre.x + radius * std::cos(a)),
                           static_cast<float>(discCentre.y + radius * std::sin(a)));
    };
    return {
        {"Círculo", ToolGeometry(CircleGeometry{discCentre, 300.0F, 20.0F, 36}), 36, 8, 360},
        {"Arco",
         ToolGeometry(ArcGeometry{onCircle(300.0, 0.0), onCircle(300.0, 20.0),
                                  onCircle(300.0, 40.0), 20.0F, 24}),
         24, 5, 180},
        {"Redondez", ToolGeometry(RoundnessGeometry{discCentre, 300.0F, 20.0F, 72}), 72, 12,
         720},
        {"Engranaje", ToolGeometry(GearGeometry{discCentre, 250.0F, 350.0F, 1440}), 1440, 180,
         3600},
        {"Borde liso", ToolGeometry(EdgeFlawGeometry{edgeFrom, edgeTo, 16.0F, 20}), 20, 3, 200},
        {"Rebabas y mellas",
         ToolGeometry(EdgeDefectsGeometry{edgeFrom, edgeTo, 16.0F, 60, 1.5F, true}), 60, 8,
         400},
        {"Rectitud (zona mínima)",
         ToolGeometry(StraightnessGeometry{edgeFrom, edgeTo, 16.0F, 60}), 60, 5, 400},
        {"Orientación",
         ToolGeometry(OrientationGeometry{edgeFrom, edgeTo, 16.0F, 60, 0.0F}), 60, 5, 400},
        {"Eje / Diámetro", ToolGeometry(ShaftGeometry{barFrom, barTo, 60.0F, 32}), 32, 5, 200},
        {"Eje medio", ToolGeometry(MedianAxisGeometry{barFrom, barTo, 60.0F, 32}), 32, 5, 200},
        {"Ranura",
         ToolGeometry(GrooveGeometry{barFrom, barTo, 60.0F, 120, GrooveMeasure::Width}), 120,
         12, 400},
        {"Rosca", ToolGeometry(ThreadGeometry{barFrom, barTo, 60.0F, 240}), 240, 40, 1000},
    };
}

}  // namespace

// ---------------------------------------------------------------------------
// 1) Inventario: valor de fábrica y rango, tal como los ofrece el modelo
// ---------------------------------------------------------------------------

TEST(PointCountTuning, TheTwelveToolsExposeTheirFactoryValueAndTheExecutorRange) {
    std::printf("  %-24s %8s %8s %8s\n", "herramienta", "fábrica", "mín", "máx");
    for (const auto& tool : pointToolCases()) {
        const PointCountSpec spec = pointCountOf(tool.geometry);
        std::printf("  %-24s %8d %8d %8d\n", tool.label, spec.value, spec.minValue,
                   spec.maxValue);
        EXPECT_TRUE(spec.editable) << tool.label << ": no se reconoce como editable";
        EXPECT_EQ(spec.value, tool.expectedDefault) << tool.label;
        EXPECT_EQ(spec.minValue, tool.expectedMin) << tool.label;
        EXPECT_EQ(spec.maxValue, tool.expectedMax) << tool.label;
        EXPECT_STRNE(pointCountNoun(typeOf(tool.geometry)), "") << tool.label;
    }
    // Y las herramientas SIN este parámetro no fingen tenerlo — Calibre mide
    // en un único segmento (una sola llamada a `detectEdges`, sin bucle de
    // puntos) y no comparte este coste.
    EXPECT_FALSE(pointCountOf(ToolGeometry(CaliperGeometry{})).editable);
    EXPECT_FALSE(pointCountOf(ToolGeometry(RulerGeometry{})).editable);
    EXPECT_FALSE(pointCountOf(ToolGeometry(BlobGeometry{})).editable);
}

// ---------------------------------------------------------------------------
// 2) Coste: 18/36/72/180 puntos sobre un fotograma de cámara (1920x1080)
// ---------------------------------------------------------------------------

TEST(PointCountTuning, CostOnA1920x1080FrameGrowsWithPointsInEveryFamily) {
    const cv::Mat discImg = bigDisc(300.0);
    const cv::Point2f discCentre(kFrameW / 2.0F, kFrameH / 2.0F);
    const cv::Mat barImg = bigTurnedBar(200.0);
    const cv::Point2f barFrom(200.0F, kFrameH / 2.0F);
    const cv::Point2f barTo(kFrameW - 200.0F, kFrameH / 2.0F);
    const cv::Mat edgeImg = bigStraightEdge();
    const cv::Point2f edgeFrom(200.0F, kFrameH / 2.0F);
    const cv::Point2f edgeTo(kFrameW - 200.0F, kFrameH / 2.0F);

    struct Case {
        const char* label;
        const char* family;
        const cv::Mat* frame;
        std::function<std::vector<ToolConfig>(int)> build;
    };
    const std::vector<Case> cases = {
        {"Círculo", "rayos", &discImg,
         [&](int p) {
             return std::vector<ToolConfig>{
                 configOf(ToolGeometry(CircleGeometry{discCentre, 300.0F, 20.0F, p}))};
         }},
        {"Redondez", "rayos", &discImg,
         [&](int p) {
             return std::vector<ToolConfig>{
                 configOf(ToolGeometry(RoundnessGeometry{discCentre, 300.0F, 20.0F, p}))};
         }},
        {"Eje / Diámetro", "cortes", &barImg,
         [&](int p) {
             return std::vector<ToolConfig>{
                 configOf(ToolGeometry(ShaftGeometry{barFrom, barTo, 60.0F, p}))};
         }},
        {"Ranura", "cortes", &barImg,
         [&](int p) {
             return std::vector<ToolConfig>{configOf(ToolGeometry(
                 GrooveGeometry{barFrom, barTo, 60.0F, p, GrooveMeasure::Width}))};
         }},
        {"Rectitud (zona mínima)", "escaneos", &edgeImg,
         [&](int p) {
             return std::vector<ToolConfig>{
                 configOf(ToolGeometry(StraightnessGeometry{edgeFrom, edgeTo, 16.0F, p}))};
         }},
        {"Borde liso", "escaneos", &edgeImg,
         [&](int p) {
             return std::vector<ToolConfig>{
                 configOf(ToolGeometry(EdgeFlawGeometry{edgeFrom, edgeTo, 16.0F, p}))};
         }},
        {"Orientación", "escaneos", &edgeImg,
         [&](int p) {
             // Necesita un datum: la misma línea, como referencia (Regla).
             ToolConfig datum;
             datum.type = ToolType::Ruler;
             datum.name = "datum";
             datum.geometryJson = toJson(ToolGeometry(
                 RulerGeometry{{200.0F, static_cast<float>(kFrameH) / 2.0F + 40.0F},
                              {static_cast<float>(kFrameW) - 200.0F,
                               static_cast<float>(kFrameH) / 2.0F + 40.0F}}));
             ToolConfig tolerated = configOf(
                 ToolGeometry(OrientationGeometry{edgeFrom, edgeTo, 16.0F, p, 0.0F}),
                 "orientacion");
             tolerated.reference = "datum";
             return std::vector<ToolConfig>{datum, tolerated};
         }},
    };

    std::printf("  %-24s %8s %10s %10s %10s %10s\n", "herramienta", "familia", "18 pts",
               "36 pts", "72 pts", "180 pts");
    for (const auto& c : cases) {
        const double ms18 = fastestMs(*c.frame, c.build(18));
        const double ms36 = fastestMs(*c.frame, c.build(36));
        const double ms72 = fastestMs(*c.frame, c.build(72));
        const double ms180 = fastestMs(*c.frame, c.build(180));
        std::printf("  %-24s %8s %9.4f %9.4f %9.4f %9.4f  (ms)\n", c.label, c.family, ms18,
                   ms36, ms72, ms180);
        // El coste no puede DISMINUIR de forma apreciable al subir los puntos
        // (10 µs de margen absorbe el ruido del reloj en máquinas rápidas).
        EXPECT_LE(ms36, ms180 + 0.01) << c.label;
        EXPECT_LE(ms18, ms180 + 0.01) << c.label;
        // Y de 18 a 180 puntos (10x) el coste tiene que notarse: si no, el
        // parámetro no está haciendo lo único que promete, que es costar más.
        EXPECT_GT(ms180, ms18) << c.label << ": 180 puntos no cuesta más que 18";
    }
}

// ---------------------------------------------------------------------------
// 3) Precisión: el Ø de un disco de verdad conocida, con pocos rayos
// ---------------------------------------------------------------------------

TEST(PointCountTuning, FewerRaysMeasureALessPreciseDiameterOnANoisyDisc) {
    // Disco de Ø 600 px (radio 300) con ruido gaussiano de cámara. La verdad
    // de campo es exacta por construcción: se dibujó a ese radio.
    constexpr double kTrueDiameter = 600.0;
    constexpr double kNoiseSigma = 18.0;
    const cv::Mat noisy = bigDisc(kTrueDiameter / 2.0, kNoiseSigma, /*seed=*/7);
    const cv::Point2f centre(kFrameW / 2.0F, kFrameH / 2.0F);

    std::printf("  %s\n", "  rayos     Ø medido     error abs   error %%");
    std::vector<int> raysTried = {8, 18, 36, 72, 180, 360};
    std::vector<double> errors;
    for (int rays : raysTried) {
        const ToolRunResult result =
            read(noisy, ToolGeometry(CircleGeometry{centre, 300.0F, 30.0F, rays}));
        const double diameter = result.measured;
        const double error = std::abs(diameter - kTrueDiameter);
        errors.push_back(error);
        std::printf("  %5d   %10.3f    %8.3f    %6.3f\n", rays, diameter, error,
                   100.0 * error / kTrueDiameter);
        EXPECT_TRUE(result.ok) << "rayos=" << rays << ": " << result.detail;
        // Ni con el mínimo de fábrica el resultado puede desbocarse: sigue
        // siendo una medida utilizable, solo que más ruidosa.
        EXPECT_LT(error, 60.0) << "rayos=" << rays
                               << ": con este ruido de cámara, el Ø ya no es fiable";
    }
    // El extremo bajo (8 rayos, el mínimo que deja pasar el ejecutor) tiene
    // que ser, con este ruido, PEOR o igual que el extremo alto (360 rayos, el
    // máximo): promediar más puntos no puede empeorar la estimación del
    // círculo ajustado.
    EXPECT_GE(errors.front() + 1e-9, errors.back())
        << "8 rayos no es peor que 360: el muestreo no está aportando nada";
}

// ---------------------------------------------------------------------------
// 4) El valor editado sobrevive a guardar y cargar la plantilla
// ---------------------------------------------------------------------------

TEST(PointCountTuning, TheEditedPointCountSurvivesTemplateSaveAndLoad) {
    for (auto tool : pointToolCases()) {
        const int edited = tool.expectedMin + 3;
        ASSERT_TRUE(setPointCount(tool.geometry, edited)) << tool.label;
        const std::string json = toJson(tool.geometry);
        const auto loaded = geometryFromJson(typeOf(tool.geometry), json);
        ASSERT_TRUE(loaded.isOk()) << tool.label << ": " << loaded.error().message;
        const PointCountSpec spec = pointCountOf(loaded.value());
        EXPECT_EQ(spec.value, edited)
            << tool.label << ": el valor editado no sobrevive a guardar y cargar";
    }
}

// ---------------------------------------------------------------------------
// 5) Plantillas ANTIGUAS, sin el campo, cargan con el valor de fábrica
// ---------------------------------------------------------------------------

TEST(PointCountTuning, OldTemplatesWithoutThePointCountFieldLoadWithTheFactoryDefault) {
    for (const auto& tool : pointToolCases()) {
        const std::string current = toJson(tool.geometry);  // ya lleva el valor de fábrica
        const char* key = jsonKeyFor(typeOf(tool.geometry));
        ASSERT_STRNE(key, "") << tool.label;
        const std::string old = withoutKey(current, key);
        ASSERT_NE(old, current) << tool.label << ": no se pudo quitar la clave '" << key
                                << "' del JSON de prueba";

        const auto loaded = geometryFromJson(typeOf(tool.geometry), old);
        ASSERT_TRUE(loaded.isOk())
            << tool.label << ": una plantilla antigua sin '" << key
            << "' ya no carga: " << loaded.error().message;
        const PointCountSpec spec = pointCountOf(loaded.value());
        EXPECT_EQ(spec.value, tool.expectedDefault)
            << tool.label << ": una plantilla antigua sin '" << key
            << "' no carga con el valor de fábrica";
    }
}
