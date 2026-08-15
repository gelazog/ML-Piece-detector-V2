// Banco de la CALIBRACIÓN DE ESCALA: que los milímetros que enseña el programa
// sean los milímetros de la pieza.
//
// `tests/test_domain.cpp` ya comprueba la ARITMÉTICA de `calibration.h` —que
// 200 px con 50 mm dan 0,25 mm/px— y eso no demuestra nada sobre la máquina:
// una escala mal aplicada da exactamente el mismo 0,25 y luego mide la pieza
// equivocada. Lo que faltaba, y es lo que hay aquí, es la IDA Y VUELTA: dibujar
// una figura de un tamaño conocido EN MILÍMETROS a una escala conocida, pasarla
// por el pipeline y por la medición automática con esa escala, y exigir que los
// milímetros vuelvan.
//
// Una calibración mala no falla ni lanza: devuelve números creíbles y
// equivocados, y el operador acepta piezas malas o tira piezas buenas sin
// enterarse. Por eso todos los umbrales de este fichero salen de imprimir
// primero el valor real y acotarlo después, no de elegir un número bonito.
//
// El arnés de figuras sintéticas está COPIADO de `test_shape_measure.cpp` a
// propósito: son bancos independientes y no deben poder romperse entre sí.
#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "domain/calibration.h"
#include "inspection_editor/auto_measure.h"
#include "vision/pipeline.h"
#include "vision/plane_scale.h"

using pci::domain::calibrationFromCameraDistance;
using pci::domain::calibrationFromKnownLength;
using pci::domain::estimateCameraDistanceMm;
using pci::domain::ScaleCalibration;
using pci::inspection::AutoProposal;
using pci::inspection::proposeTools;
using pci::inspection::ProposeOptions;

namespace {

// --- Arnés de figuras sintéticas (copiado de test_shape_measure.cpp) --------

struct Scene {
    cv::Mat gray;
    cv::Mat mask;
};

// Pieza clara sobre fondo oscuro: el montaje a contraluz que estas medidas
// piden.
Scene sceneFrom(const cv::Mat& mask) {
    Scene scene;
    scene.mask = mask;
    scene.gray = cv::Mat(mask.size(), CV_8UC1, cv::Scalar(30));
    scene.gray.setTo(cv::Scalar(220), mask);
    return scene;
}

cv::Mat disc(int size = 500, int radius = 150) {
    cv::Mat mask(size, size, CV_8UC1, cv::Scalar(0));
    // SIN antialiasing, y es la decisión que hace utilizable este banco entero.
    //
    // Con `LINE_AA`, OpenCV rasteriza el disco ~1,4 px más grande de lo nominal
    // POR CADA LADO: medido, un Ø de 200 px sale con un contorno de 202,87. Ese
    // sesgo de +2,7 px es constante —no escala con la pieza— así que sobre una
    // pieza de 800 px es un 0,3 % y sobre una de 80 px es un 3,3 %, y hacía
    // parecer que la calibración perdía precisión con el tamaño cuando lo que
    // perdía precisión era el DIBUJO.
    //
    // Sin antialiasing el contorno mide exactamente lo nominal, y entonces sí se
    // puede comparar la medida contra el número que se pidió dibujar. Medido con
    // esta forma: la herramienta se equivoca 0,06 px sobre un Ø de 600.
    cv::circle(mask, {size / 2, size / 2}, radius, cv::Scalar(255), cv::FILLED, cv::LINE_8);
    return mask;
}

// Polígono regular inscrito en un círculo de radio `radius`. El radio es el
// CIRCUNRADIO, así que el lado vale 2·R·sen(π/n).
cv::Mat regularPolygon(int sides, int size = 500, int radius = 160, double rotationDeg = 0.0) {
    cv::Mat mask(size, size, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point> points;
    for (int i = 0; i < sides; ++i) {
        const double a = rotationDeg * CV_PI / 180.0 + 2.0 * CV_PI * i / sides;
        points.emplace_back(cv::Point(static_cast<int>(size / 2 + radius * std::cos(a)),
                                      static_cast<int>(size / 2 + radius * std::sin(a))));
    }
    cv::fillPoly(mask, points, cv::Scalar(255), cv::LINE_8);
    return mask;
}

double sideOfRegularPolygon(int sides, double circumradius) {
    return 2.0 * circumradius * std::sin(CV_PI / sides);
}

// Sin tope: aquí se busca UNA propuesta concreta por su nombre, y el recorte
// por defecto a doce podría dejarla fuera en una figura con muchas caras.
ProposeOptions everything() {
    ProposeOptions options;
    options.maxProposals = 100;
    return options;
}

const AutoProposal* findNamed(const std::vector<AutoProposal>& proposals,
                              const std::string& needle) {
    for (const auto& p : proposals) {
        if (p.config.name.find(needle) != std::string::npos) {
            return &p;
        }
    }
    return nullptr;
}

// --- La cadena completa: imagen -> pipeline -> propuestas -------------------

struct Reading {
    bool ran = false;
    double px = 0.0;       // lo que mide la herramienta, siempre en píxeles
    std::string detail;    // la lectura que LEE el operador, ya con unidades
};

// Pasa la escena por `analyzeFrame` (no por la máscara dibujada: el pipeline es
// parte de lo que se está probando) y devuelve la propuesta cuyo nombre
// contiene `needle`, medida con la escala dada.
Reading readThroughPipeline(const Scene& scene, const std::string& needle, double mmPerPixel) {
    Reading reading;
    const auto analysis = pci::vision::analyzeFrame(scene.gray);
    if (!analysis.isOk()) {
        return reading;
    }
    const auto proposals = proposeTools(scene.gray, analysis.value().mask,
                                        analysis.value().fixture, everything(), mmPerPixel);
    const auto* found = findNamed(proposals, needle);
    if (found == nullptr) {
        return reading;
    }
    reading.ran = true;
    reading.px = found->measured;
    reading.detail = found->detail;
    return reading;
}

// Un disco de `diameterMm` visto a `mmPerPixel`. El lienzo se dimensiona con la
// pieza para que la fracción de área no dependa de la escala: si no, el filtro
// de área del pipeline descartaría las piezas pequeñas y el barrido estaría
// midiendo ese filtro en vez de la calibración.
Scene discOfMillimetres(double diameterMm, double mmPerPixel) {
    const double diameterPx = diameterMm / mmPerPixel;
    const int radius = static_cast<int>(std::lround(diameterPx / 2.0));
    const int size = std::max(200, static_cast<int>(std::lround(diameterPx * 2.5)));
    return sceneFrom(disc(size, radius));
}

std::filesystem::path sampleImage(const char* name) {
    // PCI_MODELS_DIR es "<raíz>/models"; su padre es la raíz del repositorio.
    // Se deriva de ahí para no tener que tocar `tests/CMakeLists.txt` más allá
    // de registrar este fichero.
    return std::filesystem::path(PCI_MODELS_DIR).parent_path() / "sample_images" / name;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. La ida y vuelta: milímetros -> píxeles -> medida -> milímetros
// ---------------------------------------------------------------------------

// LA prueba que faltaba. Todo lo demás de este fichero es aritmética o
// defensa; esto es lo único que demuestra que calibrar sirve para algo.
//
// Una pieza de 40 mm a 0,25 mm/px son 160 px: se dibuja así, se mide por la
// cadena entera (segmentación, contorno, fixture, clasificación de forma,
// ejecución de la herramienta) y se exige que al multiplicar por la escala
// vuelvan los 40 mm. Si el pipeline perdiera o ganara escala por el camino
// —una máscara erosionada, un recorte remuestreado, un radio en diámetro— el
// número volvería distinto y nadie se enteraría, porque en píxeles todo cuadra.
TEST(CalibrationRoundTrip, ADiscOfKnownMillimetresMeasuresBackAsThoseMillimetres) {
    struct Row {
        double mmPerPixel;
        double diameterMm;
    };
    // Escalas del enunciado (0,05 a 1,0 mm/px) por varios tamaños de pieza. Se
    // dejan fuera las combinaciones de menos de ~48 px de diámetro: por debajo
    // de eso lo que limita es el rasterizado del borde, no la calibración, y el
    // test estaría midiendo otra cosa.
    const std::vector<Row> rows{
        {0.05, 10.0},  {0.05, 25.0}, {0.05, 40.0},  {0.10, 10.0}, {0.10, 25.0},
        {0.10, 40.0},  {0.10, 80.0}, {0.25, 25.0},  {0.25, 40.0}, {0.25, 80.0},
        {0.50, 40.0},  {0.50, 80.0}, {0.50, 120.0}, {1.00, 80.0}, {1.00, 160.0},
    };

    double worst = 0.0;
    std::printf("  [ida y vuelta] mm/px   Ø nominal   Ø px    Ø medido px   Ø mm    error\n");
    for (const auto& row : rows) {
        const ScaleCalibration calibration =
            calibrationFromKnownLength(row.diameterMm / row.mmPerPixel, row.diameterMm, 640, 60.0);
        ASSERT_TRUE(calibration.valid()) << row.mmPerPixel << " mm/px";
        // La escala reconstruida tiene que ser la pedida, o el resto del test
        // estaría midiendo con una regla distinta de la que cree.
        ASSERT_NEAR(calibration.mmPerPixel, row.mmPerPixel, 1e-12);

        const Scene scene = discOfMillimetres(row.diameterMm, row.mmPerPixel);
        const Reading reading = readThroughPipeline(scene, "Ø", calibration.mmPerPixel);
        ASSERT_TRUE(reading.ran) << "a " << row.mmPerPixel << " mm/px y " << row.diameterMm
                                 << " mm no se propuso ni el diámetro";

        const double measuredMm = calibration.toMm(reading.px);
        const double error = std::abs(measuredMm - row.diameterMm) / row.diameterMm;
        worst = std::max(worst, error);
        std::printf("                 %5.2f   %7.1f mm  %6.1f  %10.2f  %7.2f  %5.2f %%\n",
                    row.mmPerPixel, row.diameterMm, row.diameterMm / row.mmPerPixel, reading.px,
                    measuredMm, error * 100.0);
        // Cota por fila: 2 % del nominal. Medido: el peor caso del barrido se
        // imprime abajo.
        EXPECT_LT(error, 0.02) << row.diameterMm << " mm a " << row.mmPerPixel << " mm/px: salió "
                               << measuredMm << " mm";
    }
    std::printf("  [ida y vuelta] peor error del barrido: %.3f %%\n", worst * 100.0);
}

// La ida y vuelta sobre una cota que NO es el tamaño global de la pieza: el
// lado de un hexágono, que sale de descomponer el contorno y de ejecutar una
// regla, no de la envolvente. Si la escala se aplicara solo a las medidas
// «grandes» —el caso fácil— esto lo destaparía.
TEST(CalibrationRoundTrip, AHexagonSideComesBackInMillimetres) {
    struct Row {
        double mmPerPixel;
        double sideMm;
    };
    const std::vector<Row> rows{
        {0.05, 8.0}, {0.10, 8.0}, {0.10, 20.0}, {0.25, 20.0}, {0.25, 40.0}, {0.50, 40.0},
    };

    std::printf("  [lado hexágono] mm/px  lado nominal  lado px  lado medido mm   error\n");
    for (const auto& row : rows) {
        // En un hexágono regular el lado ES el circunradio, pero se deja la
        // fórmula general para que el número no dependa de esa coincidencia.
        const double sidePx = row.sideMm / row.mmPerPixel;
        const int radiusPx = static_cast<int>(std::lround(sidePx / (2.0 * std::sin(CV_PI / 6.0))));
        const int size = std::max(240, radiusPx * 3);
        const Scene scene = sceneFrom(regularPolygon(6, size, radiusPx));

        const ScaleCalibration calibration =
            calibrationFromKnownLength(1.0 / row.mmPerPixel, 1.0, 640, 60.0);
        ASSERT_TRUE(calibration.valid());

        const Reading reading = readThroughPipeline(scene, "Lado 1", calibration.mmPerPixel);
        ASSERT_TRUE(reading.ran) << row.sideMm << " mm a " << row.mmPerPixel << " mm/px";

        const double expectedMm = sideOfRegularPolygon(6, radiusPx) * row.mmPerPixel;
        const double measuredMm = calibration.toMm(reading.px);
        const double error = std::abs(measuredMm - expectedMm) / expectedMm;
        std::printf("                  %5.2f  %9.2f mm  %7.1f  %11.2f mm  %5.2f %%\n",
                    row.mmPerPixel, expectedMm, sidePx, measuredMm, error * 100.0);
        // 5 %: el lado de un polígono lo fija la descomposición del contorno,
        // que recorta los extremos de cada tramo recto — es intrínsecamente
        // menos exacto que un diámetro ajustado por 72 rayos.
        EXPECT_LT(error, 0.05) << row.sideMm << " mm a " << row.mmPerPixel << " mm/px";
    }
}

// La misma pieza física vista con más o menos aumento tiene que dar los MISMOS
// milímetros. Es la invariancia que justifica que exista la calibración: si el
// número en mm dependiera de lo cerca que esté la cámara, calibrar no serviría
// para nada.
//
// Se dibuja un disco de 30 mm a cinco escalas distintas y se compara la
// dispersión de las lecturas en mm, que en píxeles van de 30 a 600.
TEST(CalibrationRoundTrip, TheSamePieceGivesTheSameMillimetresAtEveryMagnification) {
    const double diameterMm = 30.0;
    double minMm = std::numeric_limits<double>::max();
    double maxMm = 0.0;
    std::printf("  [invariancia] Ø 30 mm:");
    for (double mmPerPixel : {0.05, 0.10, 0.20, 0.25, 0.50}) {
        const Scene scene = discOfMillimetres(diameterMm, mmPerPixel);
        const Reading reading = readThroughPipeline(scene, "Ø", mmPerPixel);
        ASSERT_TRUE(reading.ran) << mmPerPixel << " mm/px";
        const double mm = reading.px * mmPerPixel;
        minMm = std::min(minMm, mm);
        maxMm = std::max(maxMm, mm);
        std::printf("  %.2f mm/px -> %.2f mm", mmPerPixel, mm);
    }
    const double spread = (maxMm - minMm) / diameterMm;
    std::printf("\n  [invariancia] dispersión %.3f mm (%.2f %% del nominal)\n", maxMm - minMm,
                spread * 100.0);
    // 3 % de dispersión entre la lectura más grande y la más pequeña. Lo que
    // queda es el borde rasterizado, que pesa relativamente más cuanto menos
    // píxeles ocupa la pieza.
    EXPECT_LT(spread, 0.03) << "la misma pieza mide entre " << minMm << " y " << maxMm << " mm";
}

// Los milímetros no valen de nada si no llegan al operador. `measured` sigue en
// píxeles a propósito (las tolerancias son en px), así que la única vía por la
// que el operador ve milímetros es el texto de detalle: si ahí no aparecen,
// calibrar no cambia nada de lo que se lee en pantalla.
TEST(CalibrationRoundTrip, TheReadingShowsMillimetresOnlyWhenCalibrated) {
    const Scene scene = discOfMillimetres(40.0, 0.25);

    const Reading uncalibrated = readThroughPipeline(scene, "Ø", 0.0);
    ASSERT_TRUE(uncalibrated.ran);
    EXPECT_EQ(uncalibrated.detail.find("mm"), std::string::npos)
        << "sin calibrar no puede aparecer un milímetro: «" << uncalibrated.detail << "»";
    EXPECT_NE(uncalibrated.detail.find("px"), std::string::npos) << uncalibrated.detail;

    const Reading calibrated = readThroughPipeline(scene, "Ø", 0.25);
    ASSERT_TRUE(calibrated.ran);
    EXPECT_NE(calibrated.detail.find("mm"), std::string::npos)
        << "calibrado, la lectura tiene que traer los milímetros: «" << calibrated.detail << "»";
    // Y que el número escrito sea el de la medida, no otro: se busca el valor
    // en mm con dos decimales dentro del texto.
    char expected[32];
    std::snprintf(expected, sizeof(expected), "%.2fmm", calibrated.px * 0.25);
    EXPECT_NE(calibrated.detail.find(expected), std::string::npos)
        << "esperaba «" << expected << "» dentro de «" << calibrated.detail << "»";
    std::printf("  [lectura] sin calibrar: «%s»\n            calibrada:  «%s»\n",
                uncalibrated.detail.c_str(), calibrated.detail.c_str());

    // Y la misma medida por `formatLength`, que es lo que usa el resto de la
    // interfaz: 160 px a 0,25 mm/px son 40 mm.
    ScaleCalibration calibration;
    calibration.mmPerPixel = 0.25;
    EXPECT_NE(calibration.formatLength(160.0).find("40.00 mm"), std::string::npos);
}

// ---------------------------------------------------------------------------
// 2. Los dos métodos de calibración, sobre el mismo montaje
// ---------------------------------------------------------------------------

// Si con un objeto de referencia sale una escala y con distancia+FOV sale otra
// sobre el MISMO montaje, uno de los dos miente y el operador no tiene forma de
// saber cuál.
//
// El acuerdo no se comprueba con la misma fórmula por los dos lados (eso sería
// tautológico): el objeto de referencia se RENDERIZA proyectándolo con el
// modelo pinhole en su forma de distancia focal —f = (W/2)/tan(FOV/2) px, y un
// objeto de L mm a Z mm ocupa L·f/Z px—, se mide por el pipeline entero y la
// escala del método A sale de esa medida. El método B no toca la imagen. Lo que
// queda entre los dos es el error de la cadena de medida, y es lo que se acota.
TEST(CalibrationMethods, ReferenceObjectAndCameraDistanceAgreeOnTheSameSetup) {
    struct Setup {
        double cameraDistanceMm;
        double fovDeg;
        int widthPx;
        double referenceMm;  // diámetro real del disco de referencia
    };
    const std::vector<Setup> setups{
        {300.0, 60.0, 640, 40.0},   {300.0, 60.0, 1280, 40.0}, {500.0, 45.0, 1280, 60.0},
        {1000.0, 30.0, 1920, 120.0}, {150.0, 90.0, 800, 25.0},
    };

    double worst = 0.0;
    std::printf("  [dos métodos]   Z mm   FOV   W px   ref mm   A mm/px    B mm/px   dif\n");
    for (const auto& s : setups) {
        // Proyección pinhole por distancia focal, independiente de la fórmula
        // del ancho visible que usa `calibrationFromCameraDistance`.
        const double focalPx = (s.widthPx / 2.0) / std::tan(s.fovDeg * CV_PI / 360.0);
        const double referencePx = s.referenceMm * focalPx / s.cameraDistanceMm;

        // El objeto de referencia, visto por la cámara y medido por el programa.
        const int radius = static_cast<int>(std::lround(referencePx / 2.0));
        const int size = std::max(200, static_cast<int>(std::lround(referencePx * 2.5)));
        const Scene scene = sceneFrom(disc(size, radius));
        const Reading reading = readThroughPipeline(scene, "Ø", 0.0);
        ASSERT_TRUE(reading.ran) << "Z=" << s.cameraDistanceMm << " FOV=" << s.fovDeg;

        const ScaleCalibration fromReference =
            calibrationFromKnownLength(reading.px, s.referenceMm, s.widthPx, s.fovDeg);
        const ScaleCalibration fromDistance =
            calibrationFromCameraDistance(s.cameraDistanceMm, s.fovDeg, s.widthPx);
        ASSERT_TRUE(fromReference.valid());
        ASSERT_TRUE(fromDistance.valid());

        const double diff = std::abs(fromReference.mmPerPixel - fromDistance.mmPerPixel) /
                            fromDistance.mmPerPixel;
        worst = std::max(worst, diff);
        std::printf("               %6.0f  %4.0f  %5d  %6.1f  %8.5f  %8.5f  %5.2f %%\n",
                    s.cameraDistanceMm, s.fovDeg, s.widthPx, s.referenceMm,
                    fromReference.mmPerPixel, fromDistance.mmPerPixel, diff * 100.0);

        // Y la distancia que el método A deduce tiene que ser la real: es lo
        // que la interfaz enseña como «cámara a ≈ X mm» y lo que permitiría
        // recalibrar sin volver a poner el patrón delante.
        const double distanceError =
            std::abs(fromReference.cameraDistanceMm - s.cameraDistanceMm) / s.cameraDistanceMm;
        EXPECT_LT(distanceError, 0.02)
            << "el método A estima la cámara a " << fromReference.cameraDistanceMm
            << " mm y estaba a " << s.cameraDistanceMm;
    }
    std::printf("  [dos métodos] peor desacuerdo: %.3f %%\n", worst * 100.0);
    // 2 %: el desacuerdo NO es de la calibración —la aritmética de los dos
    // métodos es exacta— sino del disco rasterizado con el que se calibra el
    // método A. El peor caso del barrido se imprime arriba.
    EXPECT_LT(worst, 0.02);
}

// La vuelta cerrada entre los dos métodos, ya sin imágenes: la distancia que
// deduce A, metida en B, tiene que reproducir la escala de A, para cualquier
// FOV y cualquier ancho. Es barato y cierra el círculo que el test anterior
// solo comprueba en cinco montajes.
TEST(CalibrationMethods, TheDistanceEstimatedByOneMethodReproducesTheOther) {
    double worst = 0.0;
    for (double fov : {20.0, 30.0, 45.0, 60.0, 90.0, 120.0}) {
        for (int width : {320, 640, 1280, 1920, 4096}) {
            for (double mmPerPixel : {0.005, 0.05, 0.25, 1.0, 5.0}) {
                const ScaleCalibration a =
                    calibrationFromKnownLength(1000.0, 1000.0 * mmPerPixel, width, fov);
                ASSERT_TRUE(a.valid());
                const ScaleCalibration b =
                    calibrationFromCameraDistance(a.cameraDistanceMm, fov, width);
                ASSERT_TRUE(b.valid()) << "FOV " << fov << ", ancho " << width;
                worst = std::max(worst, std::abs(b.mmPerPixel - a.mmPerPixel) / a.mmPerPixel);
            }
        }
    }
    std::printf("  [vuelta A->B] peor desviación relativa: %.3e\n", worst);
    // Medido: 4,4e-16, o sea el redondeo de dobles. La cota se deja en 1e-12
    // por si cambia el orden de las operaciones.
    EXPECT_LT(worst, 1e-12);
}

// ---------------------------------------------------------------------------
// 3. El aviso de resolución
// ---------------------------------------------------------------------------

// `matchesResolution` existe para que calibrar a un tamaño y medir a otro no dé
// milímetros equivocados en silencio. Este test enseña PRIMERO el daño con un
// número —la misma pieza, medida a doble resolución con la escala vieja, sale
// del doble de grande— y después comprueba que el aviso salta justo ahí.
TEST(CalibrationResolution, MeasuringAtAnotherResolutionDoublesTheMillimetresAndTheGuardSaysSo) {
    // Un disco de 40 mm a 0,25 mm/px sobre un lienzo de 400 px de ancho.
    const double diameterMm = 40.0;
    const double mmPerPixel = 0.25;
    const Scene small = discOfMillimetres(diameterMm, mmPerPixel);
    Scene big;
    cv::resize(small.gray, big.gray, cv::Size(), 2.0, 2.0, cv::INTER_LINEAR);
    cv::resize(small.mask, big.mask, cv::Size(), 2.0, 2.0, cv::INTER_NEAREST);

    const Reading atCalibrated = readThroughPipeline(small, "Ø", mmPerPixel);
    const Reading atOther = readThroughPipeline(big, "Ø", mmPerPixel);
    ASSERT_TRUE(atCalibrated.ran);
    ASSERT_TRUE(atOther.ran);

    const double goodMm = atCalibrated.px * mmPerPixel;
    const double wrongMm = atOther.px * mmPerPixel;
    std::printf("  [resolución] %dx%d -> %.2f mm | %dx%d con la MISMA escala -> %.2f mm\n",
                small.gray.cols, small.gray.rows, goodMm, big.gray.cols, big.gray.rows, wrongMm);
    EXPECT_NEAR(goodMm, diameterMm, 1.0);
    // El daño, medido: la pieza de 40 mm pasa a leerse como 80. Un número
    // perfectamente creíble para una pieza que no existe.
    EXPECT_GT(wrongMm, diameterMm * 1.8);

    ScaleCalibration calibration;
    calibration.mmPerPixel = mmPerPixel;
    calibration.calibratedWidth = small.gray.cols;
    calibration.calibratedHeight = small.gray.rows;
    EXPECT_TRUE(calibration.matchesResolution(small.gray.cols, small.gray.rows));
    EXPECT_FALSE(calibration.matchesResolution(big.gray.cols, big.gray.rows))
        << "el aviso tiene que saltar justo en el caso que acaba de dar 80 mm por 40";
}

// Un aviso que salta siempre se ignora, y entonces no avisa de nada. Una
// calibración heredada (guardada antes de que se sellara la resolución) tiene
// 0x0 y NO tiene por qué ser sospechosa: no se sabe nada de ella, y no saber no
// es lo mismo que estar mal.
TEST(CalibrationResolution, AnUnknownCalibratedResolutionNeverRaisesTheAlarm) {
    ScaleCalibration inherited;
    inherited.mmPerPixel = 0.1;  // calibrada, pero sin resolución sellada
    EXPECT_FALSE(inherited.resolutionKnown());
    for (const auto& size : {cv::Size(640, 480), cv::Size(1920, 1080), cv::Size(1, 1),
                             cv::Size(0, 0), cv::Size(-4, -4)}) {
        EXPECT_TRUE(inherited.matchesResolution(size.width, size.height))
            << size.width << "x" << size.height << ": no hay nada que contradecir";
    }

    // Media resolución tampoco cuenta como conocida: sin el par completo no se
    // puede comparar, y comparar solo el ancho dejaría pasar 640x480 contra
    // 640x360, que es otra cámara con otra escala vertical.
    ScaleCalibration halfSealed;
    halfSealed.mmPerPixel = 0.1;
    halfSealed.calibratedWidth = 1280;
    EXPECT_FALSE(halfSealed.resolutionKnown());
    EXPECT_TRUE(halfSealed.matchesResolution(640, 480));

    // Y con el par sellado, cualquier diferencia —aunque sea de un píxel— es
    // una resolución distinta.
    ScaleCalibration sealed;
    sealed.mmPerPixel = 0.1;
    sealed.calibratedWidth = 1280;
    sealed.calibratedHeight = 720;
    EXPECT_TRUE(sealed.resolutionKnown());
    EXPECT_TRUE(sealed.matchesResolution(1280, 720));
    EXPECT_FALSE(sealed.matchesResolution(1281, 720));
    EXPECT_FALSE(sealed.matchesResolution(1280, 719));
    EXPECT_FALSE(sealed.matchesResolution(720, 1280)) << "girada no es la misma";
}

// ---------------------------------------------------------------------------
// 4. Lo degenerado
// ---------------------------------------------------------------------------

// Una calibración imposible tiene que quedar SIN CALIBRAR, que deja el programa
// en píxeles y a la vista. Lo que no puede pasar es que devuelva una escala
// «válida»: eso no falla, mide, y los números que da son creíbles y falsos.
TEST(CalibrationDegenerate, ImpossibleInputsLeaveTheProgramUncalibrated) {
    const std::vector<std::pair<const char*, ScaleCalibration>> cases{
        {"0 px", calibrationFromKnownLength(0.0, 50.0, 640, 60.0)},
        {"px negativos", calibrationFromKnownLength(-200.0, 50.0, 640, 60.0)},
        {"0 mm", calibrationFromKnownLength(200.0, 0.0, 640, 60.0)},
        {"mm negativos", calibrationFromKnownLength(200.0, -50.0, 640, 60.0)},
        {"px y mm a 0", calibrationFromKnownLength(0.0, 0.0, 640, 60.0)},
        {"px NaN", calibrationFromKnownLength(std::numeric_limits<double>::quiet_NaN(), 50.0,
                                              640, 60.0)},
        {"mm NaN", calibrationFromKnownLength(200.0,
                                              std::numeric_limits<double>::quiet_NaN(), 640,
                                              60.0)},
        {"distancia 0", calibrationFromCameraDistance(0.0, 60.0, 640)},
        {"distancia negativa", calibrationFromCameraDistance(-300.0, 60.0, 640)},
        {"FOV 0", calibrationFromCameraDistance(300.0, 0.0, 640)},
        {"FOV negativo", calibrationFromCameraDistance(300.0, -60.0, 640)},
        {"ancho 0", calibrationFromCameraDistance(300.0, 60.0, 0)},
        {"ancho negativo", calibrationFromCameraDistance(300.0, 60.0, -640)},
        {"distancia NaN", calibrationFromCameraDistance(
                              std::numeric_limits<double>::quiet_NaN(), 60.0, 640)},
    };
    for (const auto& [name, calibration] : cases) {
        EXPECT_FALSE(calibration.valid()) << name << ": aceptó " << calibration.mmPerPixel
                                          << " mm/px como escala buena";
        // Y sin calibrar, todo sigue en píxeles: ni un milímetro en pantalla.
        EXPECT_EQ(calibration.formatLength(42.0).find("mm"), std::string::npos) << name;
    }

    // La distancia estimada tampoco puede inventarse nada.
    EXPECT_DOUBLE_EQ(estimateCameraDistanceMm(0.0, 60.0, 640), 0.0);
    EXPECT_DOUBLE_EQ(estimateCameraDistanceMm(-0.25, 60.0, 640), 0.0);
    EXPECT_DOUBLE_EQ(estimateCameraDistanceMm(0.25, 0.0, 640), 0.0);
    EXPECT_DOUBLE_EQ(estimateCameraDistanceMm(0.25, -60.0, 640), 0.0);
    EXPECT_DOUBLE_EQ(estimateCameraDistanceMm(0.25, 60.0, 0), 0.0);
}

// Ninguna entrada, por absurda que sea, puede lanzar: estas funciones corren
// detrás de un diálogo y de valores leídos de la base de datos, y una excepción
// aquí tumba la calibración entera. Se barren todos los cruces de una lista de
// valores hostiles.
TEST(CalibrationDegenerate, NothingThrowsAndTheScaleIsNeverANonNumber) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const std::vector<double> hostileDoubles{0.0,   -0.0,  1e-300, -1e-300, 1.0,
                                             1e300, -1e300, nan,   inf,     -inf};
    const std::vector<int> hostileWidths{0, -1, 1, 640, std::numeric_limits<int>::max()};

    for (double a : hostileDoubles) {
        for (double b : hostileDoubles) {
            for (int w : hostileWidths) {
                ScaleCalibration byLength;
                ASSERT_NO_THROW(byLength = calibrationFromKnownLength(a, b, w, 60.0));
                ScaleCalibration byDistance;
                ASSERT_NO_THROW(byDistance = calibrationFromCameraDistance(a, b, w));
                ASSERT_NO_THROW((void)estimateCameraDistanceMm(a, b, w));
                // Si la da por buena, tiene que ser un número de verdad: una
                // escala NaN convierte en NaN toda medida que toque, y un NaN
                // no está fuera de tolerancia — pasa como OK.
                if (byLength.valid()) {
                    EXPECT_TRUE(std::isfinite(byLength.mmPerPixel))
                        << "px=" << a << " mm=" << b;
                }
                if (byDistance.valid()) {
                    EXPECT_TRUE(std::isfinite(byDistance.mmPerPixel))
                        << "Z=" << a << " FOV=" << b;
                }
            }
        }
    }
}

// Un FOV >= 180° no describe ninguna cámara: a 180° el plano visible es
// infinito y por encima la geometría se da la vuelta. Hoy no se rechaza.
//
// FALLO REAL, por eso el test está DESACTIVADO (no es que la expectativa sea
// discutible: es que el código devuelve una escala que da por buena):
//   - `calibrationFromCameraDistance(300, 180, 640)` -> tan(90°) en doble vale
//     1,6e16, así que sale mmPerPixel ≈ 1,5e16 y `valid()` dice que SÍ. Con esa
//     escala, una pieza de 100 px se lee como 1,5e15 mm.
//   - `calibrationFromKnownLength(200, 50, 640, 200)` -> la escala es correcta
//     (0,25 mm/px), pero `cameraDistanceMm` sale NEGATIVA porque
//     `estimateCameraDistanceMm` solo comprueba que el FOV sea > 0. La interfaz
//     enseña esa distancia como «cámara a ≈ X mm».
// El diálogo limita hoy el FOV a 20–120°, pero el valor se relee de la base de
// datos sin validar (`calib_fov_deg` en main_window.cpp), así que la puerta
// está abierta. Arreglo: exigir 0 < FOV < 180 en las tres funciones.
TEST(CalibrationDegenerate, AnImpossibleFieldOfViewIsRejected) {
    for (double fov : {180.0, 200.0, 270.0, 360.0, 1e6}) {
        const ScaleCalibration byDistance = calibrationFromCameraDistance(300.0, fov, 640);
        EXPECT_FALSE(byDistance.valid())
            << "FOV " << fov << "° dio " << byDistance.mmPerPixel << " mm/px por buena";
        EXPECT_DOUBLE_EQ(estimateCameraDistanceMm(0.25, fov, 640), 0.0) << "FOV " << fov;

        const ScaleCalibration byLength = calibrationFromKnownLength(200.0, 50.0, 640, fov);
        EXPECT_GE(byLength.cameraDistanceMm, 0.0)
            << "FOV " << fov << "° sitúa la cámara a " << byLength.cameraDistanceMm << " mm";
    }
}

// ---------------------------------------------------------------------------
// 5. formatLength: que no invente unidades
// ---------------------------------------------------------------------------

TEST(CalibrationFormat, WithoutCalibrationEverythingStaysInPixels) {
    const ScaleCalibration none;
    ASSERT_FALSE(none.valid());
    for (double px : {0.0, 0.4, 41.8, 1234.5, -7.0}) {
        const std::string text = none.formatLength(px);
        EXPECT_NE(text.find("px"), std::string::npos) << text;
        EXPECT_EQ(text.find("mm"), std::string::npos) << text;
        EXPECT_EQ(text.find("cm"), std::string::npos) << text;
    }
    EXPECT_EQ(none.formatLength(41.8), "41.80 px");
}

// La unidad cambia a cm a partir de 10 cm (README): el salto tiene que estar
// exactamente ahí, y solo puede aparecer UNA unidad de longitud. Un texto que
// dijera «9.99 cm» donde el programa juzga 99,9 mm es una lectura que el
// operador copia mal al informe.
TEST(CalibrationFormat, MillimetresBelowTenCentimetresAndCentimetresAboveIt) {
    ScaleCalibration calibration;
    calibration.mmPerPixel = 1.0;  // 1 px = 1 mm, para leer el umbral directo

    struct Row {
        double px;
        const char* unit;
        const char* other;
        const char* value;
    };
    const std::vector<Row> rows{
        {0.0, "mm", "cm", "0.00 mm"},      {1.0, "mm", "cm", "1.00 mm"},
        {99.99, "mm", "cm", "99.99 mm"},   {100.0, "cm", "mm", "10.00 cm"},
        {100.01, "cm", "mm", "10.00 cm"},  {1234.0, "cm", "mm", "123.40 cm"},
    };
    for (const auto& row : rows) {
        const std::string text = calibration.formatLength(row.px);
        EXPECT_NE(text.find(row.value), std::string::npos)
            << row.px << " px dio «" << text << "»";
        EXPECT_NE(text.find(row.unit), std::string::npos) << text;
        EXPECT_EQ(text.find(row.other), std::string::npos)
            << "aparecen dos unidades a la vez: «" << text << "»";
        // Y siempre los píxeles entre paréntesis: es lo que permite al operador
        // atar la lectura con la tolerancia, que va en px.
        EXPECT_NE(text.find("px"), std::string::npos) << text;
    }
    std::printf("  [formato] 99.99 px -> «%s» | 100 px -> «%s»\n",
                calibration.formatLength(99.99).c_str(),
                calibration.formatLength(100.0).c_str());
}

// El texto tiene que decir la verdad para cualquier escala, no solo para la
// cómoda de 1 mm/px: el número en mm se reconstruye aparte y se busca dentro
// del texto. Sin esto, una escala aplicada al revés (px/mm en vez de mm/px)
// pasaría desapercibida en las pruebas de formato.
TEST(CalibrationFormat, TheNumberInTheTextIsTheMeasurementTimesTheScale) {
    for (double mmPerPixel : {0.01, 0.05, 0.25, 0.5, 2.0}) {
        ScaleCalibration calibration;
        calibration.mmPerPixel = mmPerPixel;
        for (double px : {10.0, 160.0, 999.0}) {
            const double mm = px * mmPerPixel;
            char expected[32];
            std::snprintf(expected, sizeof(expected), mm >= 100.0 ? "%.2f cm" : "%.2f mm",
                          mm >= 100.0 ? mm / 10.0 : mm);
            const std::string text = calibration.formatLength(px);
            EXPECT_NE(text.find(expected), std::string::npos)
                << px << " px a " << mmPerPixel << " mm/px: esperaba «" << expected
                << "» y salió «" << text << "»";
        }
    }
}

// ---------------------------------------------------------------------------
// 6. Con imágenes de verdad
// ---------------------------------------------------------------------------

// El ArUco del repositorio es un patrón de lado CONOCIDO: es el único objeto de
// estas pruebas cuya medida real no depende de lo que dibuje el test. Se
// calibra con él por dos vías —la homografía del marcador y el objeto de
// referencia medido por el pipeline— y se exige que las dos digan lo mismo.
TEST(CalibrationOnRealImages, TheArucoMarkerCalibratesAndItsOwnSideMeasuresBack) {
    const cv::Mat image = cv::imread(sampleImage("aruco_4x4_id0.png").string(),
                                     cv::IMREAD_GRAYSCALE);
    ASSERT_FALSE(image.empty()) << "falta sample_images/aruco_4x4_id0.png";

    // Lado real supuesto del marcador impreso: 50 mm (el orden de magnitud del
    // que el README manda medir con una regla).
    const double markerSideMm = 50.0;
    const auto marker = pci::vision::detectMarkerScale(image, markerSideMm);
    ASSERT_TRUE(marker.has_value()) << "no se detecta el marcador en su propia imagen";
    ASSERT_GT(marker->mmPerPixel, 0.0);
    // La imagen es un marcador de frente y sin perspectiva: la calidad tiene
    // que salir prácticamente perfecta, o el indicador no significa nada.
    EXPECT_GT(marker->quality, 0.98) << "calidad " << marker->quality;

    // Segunda vía: el cuadrado negro del marcador, medido por el pipeline como
    // se mediría cualquier pieza, y usado como objeto de referencia.
    const auto analysis = pci::vision::analyzeFrame(image);
    ASSERT_TRUE(analysis.isOk()) << analysis.error().message;
    const cv::RotatedRect box = analysis.value().contour.rotatedRect;
    const double sidePx = (box.size.width + box.size.height) / 2.0;
    const ScaleCalibration fromReference =
        calibrationFromKnownLength(sidePx, markerSideMm, image.cols, 60.0);
    ASSERT_TRUE(fromReference.valid());

    const double diff =
        std::abs(fromReference.mmPerPixel - marker->mmPerPixel) / marker->mmPerPixel;
    std::printf("  [aruco] %dx%d, lado %.1f px: homografía %.5f mm/px, referencia %.5f mm/px "
                "(dif %.2f %%), calidad %.3f\n",
                image.cols, image.rows, sidePx, marker->mmPerPixel, fromReference.mmPerPixel,
                diff * 100.0, marker->quality);
    // 2 %: las dos vías miden bordes distintos del mismo cuadrado —las esquinas
    // refinadas del detector frente al contorno segmentado— así que un pequeño
    // desacuerdo es esperable; uno grande significaría que una de las dos está
    // midiendo otra cosa.
    EXPECT_LT(diff, 0.02);

    // Y la ida y vuelta sobre la imagen real: el lado del marcador, medido en
    // píxeles y convertido con la escala del marcador, tiene que dar los 50 mm
    // de los que se partió.
    const double sideMm = marker->mmPerPixel * sidePx;
    std::printf("  [aruco] el lado vuelve como %.2f mm (real %.2f mm)\n", sideMm, markerSideMm);
    EXPECT_NEAR(sideMm, markerSideMm, markerSideMm * 0.02);

    // La distancia de cámara equivalente, por cerrar con el método B: con ese
    // FOV y ese ancho, la escala del marcador corresponde a una distancia, y
    // esa distancia tiene que devolver la misma escala.
    const double distance = estimateCameraDistanceMm(marker->mmPerPixel, 60.0, image.cols);
    ASSERT_GT(distance, 0.0);
    const ScaleCalibration fromDistance = calibrationFromCameraDistance(distance, 60.0,
                                                                       image.cols);
    ASSERT_TRUE(fromDistance.valid());
    EXPECT_NEAR(fromDistance.mmPerPixel, marker->mmPerPixel, marker->mmPerPixel * 1e-9);
}

// La pieza de demostración, medida a su resolución y al doble, con la escala
// corregida en consecuencia: los milímetros tienen que ser los mismos. Es la
// versión con una imagen real de la invariancia que justifica la calibración, y
// el motivo por el que `matchesResolution` tiene que avisar cuando la
// resolución cambia sin recalibrar.
TEST(CalibrationOnRealImages, TheDemoPieceMeasuresTheSameMillimetresAtTwoResolutions) {
    const cv::Mat image = cv::imread(sampleImage("pieza_demo.png").string(),
                                     cv::IMREAD_GRAYSCALE);
    ASSERT_FALSE(image.empty()) << "falta sample_images/pieza_demo.png";

    // Se declara que la pieza mide 60 mm de largo: el valor absoluto es una
    // convención, lo que se comprueba es que NO cambie al cambiar la
    // resolución.
    const double lengthMm = 60.0;

    const auto analysis = pci::vision::analyzeFrame(image);
    ASSERT_TRUE(analysis.isOk()) << analysis.error().message;
    const auto proposals = proposeTools(image, analysis.value().mask, analysis.value().fixture,
                                        everything(), 0.0);
    const auto* longest = findNamed(proposals, "Largo total");
    ASSERT_NE(longest, nullptr) << "a la pieza de demostración no se le propone ni el largo";
    const ScaleCalibration base =
        calibrationFromKnownLength(longest->measured, lengthMm, image.cols, 60.0);
    ASSERT_TRUE(base.valid());

    std::printf("  [pieza_demo] %dx%d: largo %.1f px -> %.5f mm/px\n", image.cols, image.rows,
                longest->measured, base.mmPerPixel);

    double worst = 0.0;
    for (double factor : {0.5, 2.0, 3.0}) {
        cv::Mat scaled;
        cv::resize(image, scaled, cv::Size(), factor, factor,
                   factor < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);
        const auto scaledAnalysis = pci::vision::analyzeFrame(scaled);
        ASSERT_TRUE(scaledAnalysis.isOk()) << "x" << factor << ": " << scaledAnalysis.error().message;
        const auto scaledProposals =
            proposeTools(scaled, scaledAnalysis.value().mask, scaledAnalysis.value().fixture,
                         everything(), 0.0);
        const auto* scaledLongest = findNamed(scaledProposals, "Largo total");
        ASSERT_NE(scaledLongest, nullptr) << "x" << factor;

        // La escala a esa resolución: la misma cámara vista con el doble de
        // píxeles cubre el mismo plano, así que mm/px se divide por el factor.
        const double mmPerPixel = base.mmPerPixel / factor;
        const double mm = scaledLongest->measured * mmPerPixel;
        const double error = std::abs(mm - lengthMm) / lengthMm;
        worst = std::max(worst, error);
        std::printf("  [pieza_demo] x%.1f (%dx%d): largo %.1f px -> %.2f mm (error %.2f %%)\n",
                    factor, scaled.cols, scaled.rows, scaledLongest->measured, mm, error * 100.0);

        // Y el aviso, que es lo que impediría en la máquina real usar la escala
        // de una resolución en otra.
        ScaleCalibration sealed = base;
        sealed.calibratedWidth = image.cols;
        sealed.calibratedHeight = image.rows;
        EXPECT_FALSE(sealed.matchesResolution(scaled.cols, scaled.rows))
            << "x" << factor << ": la escala de " << image.cols << " px no vale aquí";
    }
    std::printf("  [pieza_demo] peor error entre resoluciones: %.2f %%\n", worst * 100.0);
    // 2 %: lo que queda es el remuestreo del borde de la pieza al cambiar de
    // resolución, no la calibración.
    EXPECT_LT(worst, 0.02);
}
