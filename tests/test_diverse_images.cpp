// Banco de IMÁGENES DIVERSAS: que la detección aguante escenas que no son de
// laboratorio.
//
// El resto de bancos de visión mide figuras impecables, centradas, claras sobre
// fondo negro y solas en el encuadre. Eso demuestra que la geometría está bien
// resuelta y no demuestra NADA sobre la máquina: la pieza llega descentrada,
// pegada al borde de la bandeja, con una sombra a un lado, sobre el fondo que
// haya, acompañada de otras piezas y a la resolución que dé la cámara que se
// compró. Lo que se prueba aquí es exactamente eso, y por el camino COMPLETO
// —`analyzeFrame` -> `classifyShape` -> `proposeTools`—, porque cada eslabón
// puede aguantar por su cuenta y perderse la escala o la forma en la costura.
//
// Dos decisiones de método que gobiernan el fichero entero:
//
//   1. Las figuras se dibujan SIN ANTIALIASING (`cv::LINE_8`). Con `LINE_AA`
//      OpenCV rasteriza ~1,4 px más grande de lo nominal por cada lado, y ese
//      sesgo es CONSTANTE: sobre una pieza de 600 px es un 0,5 % y sobre una de
//      60 px es un 5 %, así que hace parecer que la medida pierde precisión con
//      las piezas pequeñas cuando lo que falla es el dibujo. Sin antialiasing el
//      contorno mide lo nominal y entonces sí se puede comparar la medida contra
//      el número que se pidió dibujar.
//
//   2. Todos los umbrales de este fichero salen de MEDIR. Cada test imprime
//      primero la tabla con el número real y la cota se pone después, con
//      holgura sobre lo medido y diciendo en el comentario cuál era ese valor.
//      Un umbral elegido a ojo o pasa siempre —y entonces no prueba nada— o
//      falla el día que no toca.
//
// El arnés de figuras está COPIADO de `test_shape_measure.cpp` y
// `test_calibration_images.cpp` a propósito: son bancos independientes y no
// deben poder romperse entre sí.
#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "domain/capture_quality.h"
#include "inspection_editor/auto_measure.h"
#include "vision/pipeline.h"
#include "vision/quality_metrics.h"
#include "vision/shape_class.h"

using pci::inspection::AutoProposal;
using pci::inspection::proposeTools;
using pci::inspection::ProposeOptions;
using pci::vision::PipelineConfig;
using pci::vision::SegmentationPolarity;
using pci::vision::ShapeKind;

namespace {

// --- Arnés de escenas -------------------------------------------------------

// Los dos niveles de gris con los que se construye todo. Separados de sobra
// para que el problema nunca sea el contraste: cuando algo falle aquí, será por
// la posición, la sombra o el recuento, no porque la pieza no se vea.
constexpr int kDark = 30;
constexpr int kLight = 220;

struct Scene {
    cv::Mat gray;
    cv::Mat mask;  // la verdad de lo dibujado, para comparar contra ella
};

cv::Mat emptyMask(cv::Size size) { return cv::Mat(size, CV_8UC1, cv::Scalar(0)); }

// SIN antialiasing, ver la cabecera.
void drawDisc(cv::Mat& mask, cv::Point centre, int radius) {
    cv::circle(mask, centre, radius, cv::Scalar(255), cv::FILLED, cv::LINE_8);
}

// Polígono regular inscrito en un círculo de radio `radius`: el radio es el
// CIRCUNRADIO, así que el lado vale 2·R·sen(π/n) y los tests comparan contra esa
// fórmula en vez de contra un número apuntado a mano.
void drawRegularPolygon(cv::Mat& mask, int sides, cv::Point centre, int radius,
                        double rotationDeg = 0.0) {
    std::vector<cv::Point> points;
    points.reserve(static_cast<std::size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        const double a = rotationDeg * CV_PI / 180.0 + 2.0 * CV_PI * i / sides;
        points.emplace_back(cv::Point(static_cast<int>(centre.x + radius * std::cos(a)),
                                      static_cast<int>(centre.y + radius * std::sin(a))));
    }
    cv::fillPoly(mask, points, cv::Scalar(255), cv::LINE_8);
}

double sideOfRegularPolygon(int sides, double circumradius) {
    return 2.0 * circumradius * std::sin(CV_PI / sides);
}

// Pieza CLARA sobre fondo oscuro: el montaje a contraluz.
Scene lightOnDark(const cv::Mat& mask) {
    Scene scene;
    scene.mask = mask;
    scene.gray = cv::Mat(mask.size(), CV_8UC1, cv::Scalar(kDark));
    scene.gray.setTo(cv::Scalar(kLight), mask);
    return scene;
}

// Pieza OSCURA sobre fondo claro: la mesa blanca, que es el otro montaje que se
// ve en un taller y el que usa la imagen de muestra del repositorio.
Scene darkOnLight(const cv::Mat& mask) {
    Scene scene;
    scene.mask = mask;
    scene.gray = cv::Mat(mask.size(), CV_8UC1, cv::Scalar(kLight));
    scene.gray.setTo(cv::Scalar(kDark), mask);
    return scene;
}

// Sin tope de propuestas: varios tests buscan una propuesta concreta por su
// nombre y el recorte por defecto a doce podría dejarla fuera en una figura con
// muchas caras. El test estaría midiendo el tope y no la detección.
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

int countNamed(const std::vector<AutoProposal>& proposals, const std::string& needle) {
    return static_cast<int>(
        std::count_if(proposals.begin(), proposals.end(), [&needle](const AutoProposal& p) {
            return p.config.name.find(needle) != std::string::npos;
        }));
}

const std::vector<cv::Point>& biggestOf(const std::vector<std::vector<cv::Point>>& contours) {
    return *std::max_element(
        contours.begin(), contours.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
}

// --- El camino completo, de un tirón ---------------------------------------

// `analyzeFrame` -> `classifyShape` -> `proposeTools`, que es lo que se está
// probando. Se junta en un sitio porque la costura entre los tres es donde se
// pierden las cosas, y porque así cada test dice QUÉ escena y no CÓMO se llama a
// la biblioteca.
struct Analysed {
    bool detected = false;
    std::string error;             // el motivo, cuando NO se detecta
    pci::vision::PieceAnalysis analysis;
    pci::vision::ShapeClass shape;
    std::vector<AutoProposal> proposals;
    cv::Rect box;                  // envolvente recta del contorno, en px
    pci::domain::QualityMetrics quality;
    std::string qualityError;      // el veredicto de `validateQuality`
};

Analysed runEverything(const cv::Mat& gray, const PipelineConfig& config = {}) {
    Analysed run;
    auto analysis = pci::vision::analyzeFrame(gray, config);
    if (!analysis.isOk()) {
        run.error = analysis.error().message;
        run.quality = pci::vision::computeQualityMetrics(gray, nullptr);
        const auto verdict = pci::domain::validateQuality(run.quality);
        run.qualityError = verdict.isOk() ? std::string{} : verdict.error().message;
        return run;
    }
    run.detected = true;
    run.analysis = std::move(analysis.value());

    // El contorno que devuelve `analyzeFrame` viene con `CHAIN_APPROX_SIMPLE`,
    // o sea ya simplificado. Para clasificar se saca el contorno DENSO de la
    // máscara, que es exactamente lo que `proposeTools` hace por dentro: si aquí
    // se usara el simplificado, este banco estaría clasificando una figura
    // distinta de la que se mide.
    std::vector<std::vector<cv::Point>> dense;
    cv::findContours(run.analysis.mask, dense, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (!dense.empty()) {
        run.shape = pci::vision::classifyShape(biggestOf(dense), run.analysis.mask);
    }
    run.box = cv::boundingRect(run.analysis.contour.points);
    run.proposals =
        proposeTools(gray, run.analysis.mask, run.analysis.fixture, everything());

    run.quality = pci::vision::computeQualityMetrics(gray, &run.analysis);
    const auto verdict = pci::domain::validateQuality(run.quality);
    run.qualityError = verdict.isOk() ? std::string{} : verdict.error().message;
    return run;
}

// El valor de una propuesta por su nombre, o -1 si no se propuso. El -1 es a
// propósito: distingue «no se propuso» de «midió cero», que son dos cosas muy
// distintas y confundirlas escondería un fallo.
double measuredOf(const Analysed& run, const std::string& needle) {
    const auto* found = findNamed(run.proposals, needle);
    return found != nullptr ? found->measured : -1.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. POSICIÓN: dónde cae la pieza en el encuadre
// ---------------------------------------------------------------------------

// La pieza no se coloca con escuadra: llega donde llega. Si la medida dependiera
// de dónde cae en el encuadre, dos operarios midiendo la misma pieza obtendrían
// números distintos y no habría forma de saber cuál es el bueno.
//
// El fallo que evita: cualquier cosa que ate la detección al centro de la imagen
// —un recorte fijo, un fondo estimado solo del centro, un fixture con el origen
// mal desplazado—. Se comprueba además que el centroide caiga donde se dibujó,
// porque una medida correcta con el centro equivocado significa que el fixture
// está mintiendo y las herramientas guardadas se moverán a otro sitio.
//
// La escena es la MESA BLANCA (pieza oscura sobre fondo claro) y no el
// contraluz, y no es un capricho: con el fondo a 30 el brillo medio del frame
// sale 37 y `validateQuality` rechaza la captura entera por «Imagen demasiado
// oscura» (el mínimo son 40). Aquí hace falta una captura que el juicio de
// calidad dé por BUENA, porque si no el aviso del test siguiente —el de la pieza
// cortada— podría estar saltando siempre y no probaría nada.
TEST(DiverseImagesPosition, TheMeasurementDoesNotDependOnWhereThePieceFallsInTheFrame) {
    const cv::Size frame(640, 480);
    const int radius = 60;
    const double nominal = 2.0 * radius;

    const std::vector<std::pair<const char*, cv::Point>> places{
        {"centrada", {320, 240}},   {"arriba-izq", {100, 90}},  {"arriba-der", {540, 90}},
        {"abajo-izq", {100, 390}},  {"abajo-der", {540, 390}},
    };

    double worstDiameter = 0.0;
    double worstCentre = 0.0;
    std::printf("  [posición] sitio        centro dibujado   centroide medido    Ø px    error"
                "   brillo  nitidez\n");
    for (const auto& [name, centre] : places) {
        cv::Mat mask = emptyMask(frame);
        drawDisc(mask, centre, radius);
        const Analysed run = runEverything(darkOnLight(mask).gray);
        ASSERT_TRUE(run.detected) << name << ": " << run.error;
        EXPECT_EQ(run.shape.kind, ShapeKind::Circle) << name << ": " << run.shape.reason;

        const double diameter = measuredOf(run, "Ø");
        ASSERT_GT(diameter, 0.0) << name << ": no se propuso ni el diámetro";
        const double error = std::abs(diameter - nominal) / nominal;
        const double offCentre = cv::norm(run.analysis.contour.centroid - cv::Point2f(centre));
        worstDiameter = std::max(worstDiameter, error);
        worstCentre = std::max(worstCentre, offCentre);
        std::printf("             %-12s  (%3d,%3d)         (%6.1f,%6.1f)   %6.2f  %5.2f %%"
                    "   %6.1f  %7.1f\n",
                    name, centre.x, centre.y, static_cast<double>(run.analysis.contour.centroid.x),
                    static_cast<double>(run.analysis.contour.centroid.y), diameter, error * 100.0,
                    run.quality.meanBrightness, run.quality.sharpness);

        // La captura es buena: la pieza está entera dentro del encuadre. Sin
        // esto, el aviso de «cortada por el borde» podría estar saltando siempre
        // y el test del borde de más abajo no probaría nada.
        EXPECT_TRUE(run.qualityError.empty()) << name << ": " << run.qualityError;
        EXPECT_FALSE(run.quality.pieceTouchesBorder) << name;
    }
    std::printf("  [posición] peor error de Ø: %.3f %% | peor desvío del centroide: %.2f px\n",
                worstDiameter * 100.0, worstCentre);

    // Cotas MEDIDAS (ver la tabla que imprime el test): el error de Ø no pasa de
    // 0,12 % y el centroide cae EXACTAMENTE en el centro dibujado (0,00 px de
    // desvío en las cinco posiciones). Se dejan en 1,5 % y 2 px, que es holgura
    // de sobra sin dejar de detectar una regresión de verdad — un recorte al
    // centro daría errores de decenas de píxeles.
    EXPECT_LT(worstDiameter, 0.015);
    EXPECT_LT(worstCentre, 2.0);
}

// La pieza CORTADA por el borde del encuadre. Es el caso en el que la medida
// puede no ser recuperable, y lo que importa entonces no es medir: es NO dar un
// número equivocado con confianza.
//
// El fallo que evita, y es el peor de todos los de este fichero: una pieza a la
// que le falta un trozo mide menos, el programa no falla, devuelve un número
// creíble, y el operador rechaza una pieza buena. No hay ninguna señal en la
// medida que lo delate, así que la señal tiene que venir de otro sitio: de
// `pieceTouchesBorder` y del veredicto de `validateQuality`.
TEST(DiverseImagesPosition, APieceCutByTheFrameIsFlaggedAndNeverClaimsTheWholeDiameter) {
    const cv::Size frame(640, 480);
    const int radius = 60;
    const double nominal = 2.0 * radius;

    // Cuánto se sale la pieza por la izquierda, en píxeles.
    std::printf("  [borde] fuera  clase       Ø prop.  Largo tot.  Ancho tot.  caja px  aviso\n");
    for (int outside : {0, 10, 30, 60, 80}) {
        cv::Mat mask = emptyMask(frame);
        drawDisc(mask, {radius - outside, 240}, radius);
        const Analysed run = runEverything(darkOnLight(mask).gray);
        ASSERT_TRUE(run.detected) << outside << " px fuera: " << run.error;

        const double diameter = measuredOf(run, "Ø");
        const double longest = measuredOf(run, "Largo total");
        const double widest = measuredOf(run, "Ancho total");
        const double visible = nominal - outside;
        std::printf("          %5d  %-10s  %7.2f  %10.2f  %10.2f  %7d  %s\n", outside,
                    pci::vision::shapeKindName(run.shape.kind), diameter, longest, widest,
                    run.box.width, run.qualityError.c_str());

        // El aviso salta SIEMPRE que la pieza toca el borde, cortada o no: una
        // pieza que roza el borde es una pieza que en el frame siguiente ya está
        // cortada. Es la única señal que hay, así que se exige en todos los
        // renglones, incluido el de la pieza entera.
        EXPECT_TRUE(run.quality.pieceTouchesBorder) << outside << " px fuera";
        EXPECT_EQ(run.qualityError, "La pieza está cortada por el borde del encuadre")
            << outside << " px fuera";

        if (outside == 0) {
            // La referencia: pegada al borde pero ENTERA. Aquí sí se mide bien, y
            // sin este renglón el resto del test no demostraría que lo que cambia
            // es el recorte y no el hecho de estar pegada al borde.
            EXPECT_EQ(run.shape.kind, ShapeKind::Circle) << run.shape.reason;
            EXPECT_NEAR(diameter, nominal, nominal * 0.02);
            continue;
        }

        // A partir de aquí la pieza está CORTADA, y no hay ninguna medida
        // correcta que dar. Lo que se exige es que el programa no finja que sí:

        //  a) deja de ser un círculo, así que NO se propone ningún diámetro. Es
        //     lo que impide el peor resultado posible: un Ø ajustado por 72 rayos
        //     sobre un contorno al que le falta un trozo, que saldría con su
        //     residuo pequeño y su aire de medida exacta.
        EXPECT_NE(run.shape.kind, ShapeKind::Circle)
            << outside << " px fuera: sigue creyendo que es un disco entero";
        EXPECT_LT(diameter, 0.0)
            << outside << " px fuera: propone un Ø de " << diameter
            << " sobre una pieza a la que le falta un trozo";

        //  b) y el daño, MEDIDO, que es el motivo de que el aviso sea
        //     imprescindible: «Ancho total» cae a lo que se VE. Con 30 px fuera
        //     el programa dice 90 de una pieza de 120 —un número perfectamente
        //     creíble para una pieza que no existe— y el operador tiraría una
        //     pieza buena. No es un fallo que se pueda arreglar midiendo mejor:
        //     el trozo no está en la imagen.
        ASSERT_GT(widest, 0.0) << outside << " px fuera: no se propuso ni el ancho";
        EXPECT_NEAR(widest, visible, 4.0)
            << outside << " px fuera: el ancho propuesto no es ni el nominal ni lo visible";
        EXPECT_LT(widest, nominal * 0.95)
            << outside << " px fuera: dice medir " << widest << " de un nominal " << nominal;

        //  c) y lo que hace de esto una trampa de verdad: la OTRA cota sigue
        //     saliendo bien. El corte es por la izquierda, así que el diámetro
        //     vertical está entero dentro del encuadre y «Largo total» da sus
        //     118 px de 120 con 10, 30 y hasta 60 px de pieza fuera. O sea que
        //     la lista de medidas mezcla números correctos con números
        //     truncados, sin nada que los distinga. Por eso el aviso del
        //     encuadre no es un adorno: es lo único que separa las dos cosas.
        if (outside <= 60) {
            ASSERT_GT(longest, 0.0) << outside << " px fuera";
            EXPECT_NEAR(longest, nominal, nominal * 0.05)
                << outside << " px fuera: el largo vertical debería seguir entero";
        }

        // Y la máscara no inventa pieza fuera de la imagen: la caja se queda en
        // lo visible. Si esto creciera, la segmentación estaría rellenando el
        // trozo que falta y entonces el número SÍ sería creíble y falso.
        EXPECT_LE(run.box.width, visible + 4.0) << outside << " px fuera";
    }
}

// ---------------------------------------------------------------------------
// 2. FONDO: las dos polaridades
// ---------------------------------------------------------------------------

// La misma pieza, en positivo y en negativo, tiene que dar la MISMA medida. Es
// la prueba de que la polaridad automática existe y funciona en los dos
// sentidos, no solo en el montaje a contraluz con el que se escribieron los
// demás bancos.
//
// El fallo que evita: un `Auto` que en realidad siempre elige «pieza clara»
// pasaría todos los bancos existentes —todos dibujan claro sobre oscuro— y en la
// mesa blanca de un taller mediría el FONDO, que es el contorno que rodea a la
// pieza. Y no fallaría: devolvería un número.
TEST(DiverseImagesBackground, AutomaticPolarityWorksBothWaysAndGivesTheSameNumber) {
    const cv::Size frame(640, 480);
    const int radius = 90;
    const int hexRadius = 100;
    const double nominalDiameter = 2.0 * radius;
    const double nominalSide = sideOfRegularPolygon(6, hexRadius);

    cv::Mat discMask = emptyMask(frame);
    drawDisc(discMask, {320, 240}, radius);
    cv::Mat hexMask = emptyMask(frame);
    drawRegularPolygon(hexMask, 6, {320, 240}, hexRadius, 12.0);

    struct Row {
        const char* name;
        Scene scene;
        ShapeKind expected;
        std::string needle;
        double nominal;
    };
    const std::vector<Row> rows{
        {"disco claro/oscuro", lightOnDark(discMask), ShapeKind::Circle, "Ø", nominalDiameter},
        {"disco oscuro/claro", darkOnLight(discMask), ShapeKind::Circle, "Ø", nominalDiameter},
        {"hexágono claro/oscuro", lightOnDark(hexMask), ShapeKind::Polygon, "Lado 1", nominalSide},
        {"hexágono oscuro/claro", darkOnLight(hexMask), ShapeKind::Polygon, "Lado 1", nominalSide},
    };

    std::printf("  [polaridad] escena                  clase     lados  nominal   medido   error\n");
    double worst = 0.0;
    for (const auto& row : rows) {
        const Analysed run = runEverything(row.scene.gray);
        ASSERT_TRUE(run.detected) << row.name << ": " << run.error;
        EXPECT_EQ(run.shape.kind, row.expected) << row.name << ": " << run.shape.reason;
        if (row.expected == ShapeKind::Polygon) {
            EXPECT_EQ(run.shape.sides, 6) << row.name;
            EXPECT_EQ(countNamed(run.proposals, "Lado "), 6)
                << row.name << ": un hexágono trae seis reglas se vea como se vea";
        }
        const double measured = measuredOf(run, row.needle);
        ASSERT_GT(measured, 0.0) << row.name << ": no se propuso «" << row.needle << "»";
        const double error = std::abs(measured - row.nominal) / row.nominal;
        worst = std::max(worst, error);
        std::printf("              %-22s  %-8s  %5d  %7.1f  %7.2f  %5.2f %%\n", row.name,
                    pci::vision::shapeKindName(run.shape.kind), run.shape.sides, row.nominal,
                    measured, error * 100.0);
    }
    std::printf("  [polaridad] peor error del barrido: %.2f %%\n", worst * 100.0);
    // Cota MEDIDA: el peor error del barrido es 0,13 % (lo pone el lado del
    // hexágono; el diámetro se queda en 0,03 %). Y lo más importante de la
    // tabla: los dos montajes dan EL MISMO número hasta el último decimal
    // impreso —179,94 y 99,87 en los dos—, o sea que la polaridad no introduce
    // ningún sesgo, solo decide qué lado del umbral es la pieza. Se deja en 2 %:
    // sigue muy lejos del error que daría medir el fondo en vez de la pieza, que
    // sería de decenas por ciento.
    EXPECT_LT(worst, 0.02);
}

// Y la polaridad CONFIGURADA a mano, que es la otra mitad del enunciado: cuando
// la luz engaña al automático, el operador la fija. Se comprueba que fijarla
// bien mide igual que el automático, y —lo que de verdad importa— que fijarla
// MAL no devuelve un número: falla diciendo por qué.
//
// El fallo que evita: con la polaridad al revés, lo que queda marcado como
// «pieza» es el fondo entero. Si eso saliera adelante, el programa mediría el
// marco de la imagen y llamaría a eso una pieza de 640x480.
TEST(DiverseImagesBackground, ForcingThePolarityWorksAndForcingItWrongFailsWithAReason) {
    const cv::Size frame(640, 480);
    const int radius = 90;
    const double nominal = 2.0 * radius;
    cv::Mat mask = emptyMask(frame);
    drawDisc(mask, {320, 240}, radius);

    const Scene light = lightOnDark(mask);
    const Scene dark = darkOnLight(mask);

    auto withPolarity = [](SegmentationPolarity polarity) {
        PipelineConfig config;
        config.segmentation.polarity = polarity;
        return config;
    };

    // Fijada bien, en los dos montajes.
    const Analysed lightOk = runEverything(light.gray, withPolarity(SegmentationPolarity::LightPiece));
    const Analysed darkOk = runEverything(dark.gray, withPolarity(SegmentationPolarity::DarkPiece));
    ASSERT_TRUE(lightOk.detected) << lightOk.error;
    ASSERT_TRUE(darkOk.detected) << darkOk.error;
    EXPECT_NEAR(measuredOf(lightOk, "Ø"), nominal, nominal * 0.02);
    EXPECT_NEAR(measuredOf(darkOk, "Ø"), nominal, nominal * 0.02);
    std::printf("  [polaridad fijada] bien:  clara sobre oscuro Ø=%.2f | oscura sobre claro "
                "Ø=%.2f (nominal %.1f)\n",
                measuredOf(lightOk, "Ø"), measuredOf(darkOk, "Ø"), nominal);

    // Y fijada al revés. Lo correcto es NO medir.
    const Analysed lightWrong = runEverything(light.gray, withPolarity(SegmentationPolarity::DarkPiece));
    const Analysed darkWrong = runEverything(dark.gray, withPolarity(SegmentationPolarity::LightPiece));
    std::printf("  [polaridad fijada] al revés: «%s» | «%s»\n",
                lightWrong.detected ? "MIDIÓ ALGO" : lightWrong.error.c_str(),
                darkWrong.detected ? "MIDIÓ ALGO" : darkWrong.error.c_str());

    for (const auto& [name, run] : {std::pair<const char*, const Analysed&>{"clara marcada como oscura",
                                                                       lightWrong},
                                    std::pair<const char*, const Analysed&>{"oscura marcada como clara",
                                                                       darkWrong}}) {
        EXPECT_FALSE(run.detected)
            << name << ": dio una pieza por buena con la polaridad al revés";
        // Y el motivo tiene que decir QUÉ pasa, que es lo que permite al
        // operador arreglarlo. «La segmentación cubre casi toda la imagen» apunta
        // justo al ajuste que está mal.
        EXPECT_EQ(run.error, "La segmentación cubre casi toda la imagen (revisa fondo/iluminación)")
            << name;
    }
}

// ---------------------------------------------------------------------------
// 3. SOMBRA: el caso real que más rompe una segmentación por umbral
// ---------------------------------------------------------------------------

namespace {

// Pieza OSCURA sobre fondo claro con una sombra SUAVE pegada a su lado derecho.
//
// La sombra se construye difuminando mucho una copia de la pieza desplazada, y
// eso es lo que la hace el caso difícil: una sombra dura tiene su propio borde y
// el umbral la separa; una suave no tiene borde en ninguna parte, así que el
// umbral cae dentro del degradado y se lleva pegado al contorno todo lo que
// quede por debajo. `depth` es cuánto oscurece la sombra al fondo en su parte
// más negra: con `depth` = kLight - kDark la sombra llega a ser tan oscura como
// la propia pieza y son indistinguibles por nivel de gris.
Scene shadowScene(cv::Size size, cv::Point centre, int radius, int depth) {
    cv::Mat piece = emptyMask(size);
    drawDisc(piece, centre, radius);

    cv::Mat shadow = emptyMask(size);
    drawDisc(shadow, {centre.x + radius, centre.y}, radius);
    cv::GaussianBlur(shadow, shadow, cv::Size(61, 61), 0.0);

    cv::Mat field;
    shadow.convertTo(field, CV_32F, depth / 255.0);
    cv::Mat canvas(size, CV_32F, cv::Scalar(kLight));
    canvas -= field;
    canvas.setTo(cv::Scalar(kDark), piece);

    Scene scene;
    scene.mask = piece;
    canvas.convertTo(scene.gray, CV_8UC1);
    return scene;
}

}  // namespace

// Cuánto ENGORDA el contorno por culpa de una sombra pegada al lado. El
// enunciado pide medirlo, y medirlo es lo único honesto: esto no se arregla con
// un umbral automático, se acota y se avisa.
//
// El fallo que evita: creer que el pipeline es inmune. La sombra no hace fallar
// nada —la pieza se detecta, la clase puede seguir saliendo redonda y el número
// es creíble— simplemente sale más grande. Sin este test, el día que alguien
// cambie el suavizado o la morfología nadie sabría si el crecimiento pasó de dos
// píxeles a cuarenta.
TEST(DiverseImagesShadow, ASoftShadowFattensTheContourAndTheGrowthIsMeasured) {
    const cv::Size frame(640, 480);
    const cv::Point centre(280, 240);
    const int radius = 90;
    const double nominal = 2.0 * radius;

    std::printf("  [sombra] sombra  gris del fondo  clase       ancho px  alto px   engorde X\n");
    struct Growth {
        int depth;
        double x;
        ShapeKind kind;
    };
    std::vector<Growth> growths;
    for (int depth : {0, 40, 80, 120, 160, 190}) {
        const Scene scene = shadowScene(frame, centre, radius, depth);
        const Analysed run = runEverything(scene.gray);
        ASSERT_TRUE(run.detected) << "sombra " << depth << ": " << run.error;

        // El gris más oscuro del FONDO (fuera de la pieza): es lo que decide si
        // la sombra queda por debajo del umbral y se pega al contorno. Mirar el
        // mínimo de la imagen entera no serviría: ese siempre es la pieza.
        cv::Mat background;
        cv::bitwise_not(scene.mask, background);
        double minimum = 0.0;
        cv::minMaxLoc(scene.gray, &minimum, nullptr, nullptr, nullptr, background);
        const double grownX = (run.box.width - nominal) / nominal;
        growths.push_back({depth, grownX, run.shape.kind});
        std::printf("           %6d  %14.0f  %-10s  %8d  %7d  %+8.1f %%\n", depth, minimum,
                    pci::vision::shapeKindName(run.shape.kind), run.box.width, run.box.height,
                    grownX * 100.0);

        // La sombra está SOLO a la derecha: el alto no la ve, y tiene que
        // seguir siendo el nominal pase lo que pase. Si el alto también creciera,
        // no sería la sombra, sería que la segmentación se está descuadrando
        // entera y el número de engorde no significaría lo que dice.
        EXPECT_NEAR(static_cast<double>(run.box.height), nominal, 4.0)
            << "sombra " << depth << ": el alto no debería enterarse de la sombra";
    }

    // Sin sombra el contorno mide lo nominal: es la referencia contra la que se
    // lee todo lo demás de la tabla, y sin ella el «engorde» no sería un engorde.
    EXPECT_LT(std::abs(growths.front().x), 0.02) << "sin sombra ya sale mal";
    EXPECT_EQ(growths.front().kind, ShapeKind::Circle);

    // Y el daño, MEDIDO (tabla de arriba): la sombra no molesta mientras se
    // quede por encima del umbral de Otsu —hasta 80 de profundidad el engorde es
    // 0,0 %— y en cuanto lo cruza se pega entera al contorno de golpe: +46,7 %
    // con 120, +48,9 % con 160 y +49,4 % con 190. No es una degradación suave,
    // es un escalón, y por eso hay que vigilarlo con un número.
    for (std::size_t i = 1; i < growths.size(); ++i) {
        EXPECT_GE(growths[i].x, growths[i - 1].x - 0.02)
            << "una sombra más oscura no puede engordar MENOS (" << growths[i - 1].depth << " -> "
            << growths[i].depth << ")";
    }
    // Cota MEDIDA: el peor caso (sombra a 190, tan oscura como la pieza) engorda
    // +49,4 %. Se acota en 70 % para que una regresión que agrandara el escalón
    // —un suavizado mayor, una morfología más agresiva— saltara aquí en vez de
    // pasar desapercibida.
    EXPECT_LT(growths.back().x, 0.70)
        << "la sombra más negra engorda la pieza un " << growths.back().x * 100.0 << " %";
    // Y el aviso que sí queda: con la sombra pegada la pieza DEJA DE SER UN
    // DISCO, así que no se le propone un diámetro ajustado sobre un contorno que
    // ya no es el suyo. Es la mitad buena de esta historia: la medida se pierde,
    // pero no se sustituye por una mentira redonda.
    EXPECT_NE(growths.back().kind, ShapeKind::Circle)
        << "con la sombra pegada sigue creyendo que es un disco";
}

// La sombra no se arregla sola, pero el programa tiene el mando para arreglarla:
// un umbral manual por debajo del gris de la sombra la deja fuera. Este test
// demuestra que ese mando SIRVE, que es lo que convierte el problema anterior en
// algo que el operador puede resolver en vez de en una limitación.
//
// El fallo que evita: que `manualThreshold` exista y no haga nada, o que su
// número se interprete con la polaridad al revés. Sin este test, el control de
// umbral del diálogo sería decorativo y nadie se enteraría.
TEST(DiverseImagesShadow, AManualThresholdRecoversTheMeasurementUnderShadow) {
    const cv::Size frame(640, 480);
    const cv::Point centre(280, 240);
    const int radius = 90;
    const double nominal = 2.0 * radius;
    const int depth = 120;  // la sombra baja el fondo de 220 a 100

    const Scene scene = shadowScene(frame, centre, radius, depth);
    const Analysed automatic = runEverything(scene.gray);
    ASSERT_TRUE(automatic.detected) << automatic.error;

    // El umbral se pone entre la pieza (30) y lo más oscuro de la sombra (100):
    // así la pieza cae por debajo y la sombra entera se queda arriba, con el
    // fondo. No es un número mágico, es la media de los dos niveles que la
    // escena tiene por construcción.
    PipelineConfig config;
    config.segmentation.manualThreshold = (kDark + (kLight - depth)) / 2;
    const Analysed manual = runEverything(scene.gray, config);
    ASSERT_TRUE(manual.detected) << manual.error;

    const double autoError = (automatic.box.width - nominal) / nominal;
    const double manualError = (manual.box.width - nominal) / nominal;
    std::printf("  [sombra/umbral] automático: ancho %d px (%+.1f %%) | umbral %d: ancho %d px "
                "(%+.1f %%)\n",
                automatic.box.width, autoError * 100.0, config.segmentation.manualThreshold,
                manual.box.width, manualError * 100.0);

    // Que el caso sea el caso: sin umbral manual la sombra SÍ estorba. Sin esta
    // comprobación el test pasaría también si la sombra no hiciera nada, y
    // entonces no habría demostrado que el umbral arregle nada.
    ASSERT_GT(autoError, 0.05) << "esta sombra no engorda nada: el test no prueba lo que dice";
    // Y con el umbral puesto, la medida vuelve. Números MEDIDOS: con Otsu el
    // ancho sale 264 px (+46,7 % sobre los 180 nominales) y con el umbral a 65
    // sale 178 px (−1,1 %). O sea que el mando recupera la medida entera. La
    // cota se deja en 3 %.
    EXPECT_LT(std::abs(manualError), 0.03)
        << "con el umbral manual el ancho sigue en " << manual.box.width << " px de " << nominal;
    // Y no solo el tamaño: con la sombra fuera, la pieza vuelve a clasificarse
    // como el disco que es y recupera su diámetro. Sin esto el test pasaría con
    // una máscara del tamaño correcto pero de forma cualquiera.
    EXPECT_EQ(manual.shape.kind, ShapeKind::Circle) << manual.shape.reason;
    EXPECT_NEAR(measuredOf(manual, "Ø"), nominal, nominal * 0.04);
}

// ---------------------------------------------------------------------------
// 4. RESOLUCIÓN: la misma escena con la cámara que haya
// ---------------------------------------------------------------------------

// Las cuatro resoluciones del enunciado, con la MISMA escena en proporción: la
// pieza ocupa siempre la misma fracción del encuadre. Lo que tiene que aguantar
// es la clase (un hexágono es un hexágono) y las medidas RELATIVAS (el lado
// medido dividido por el alto del frame es el mismo número).
//
// El fallo que evita, y es uno que ya pasó de verdad en este código: cualquier
// constante en píxeles escondida en el camino —un paso de remuestreo fijo, un
// kernel de morfología, una longitud mínima de rasgo— hace que la respuesta
// dependa de la cámara. Con el paso de remuestreo fijo en 2 px, el mismo
// hexágono salía de 6 lados en grande y de «4 rectas y 2 arcos» en pequeño.
TEST(DiverseImagesResolution, TheSameSceneKeepsItsClassAndItsRelativeSizeAtEveryResolution) {
    const std::vector<cv::Size> resolutions{{320, 240}, {640, 480}, {1280, 720}, {1920, 1080}};

    std::printf("  [resolución] tamaño       R px   clase     lados   Ø/alto    lado/alto\n");
    double minDiscRatio = 1e9;
    double maxDiscRatio = 0.0;
    double minSideRatio = 1e9;
    double maxSideRatio = 0.0;
    for (const auto& size : resolutions) {
        const int radius = static_cast<int>(std::lround(size.height * 0.25));
        const cv::Point centre(size.width / 2, size.height / 2);

        cv::Mat discMask = emptyMask(size);
        drawDisc(discMask, centre, radius);
        const Analysed disc = runEverything(lightOnDark(discMask).gray);
        ASSERT_TRUE(disc.detected) << size.width << "x" << size.height << ": " << disc.error;
        EXPECT_EQ(disc.shape.kind, ShapeKind::Circle)
            << size.width << "x" << size.height << ": " << disc.shape.reason;
        const double diameter = measuredOf(disc, "Ø");
        ASSERT_GT(diameter, 0.0) << size.width << "x" << size.height;
        const double discRatio = diameter / size.height;

        cv::Mat hexMask = emptyMask(size);
        drawRegularPolygon(hexMask, 6, centre, radius, 12.0);
        const Analysed hex = runEverything(lightOnDark(hexMask).gray);
        ASSERT_TRUE(hex.detected) << size.width << "x" << size.height << ": " << hex.error;
        EXPECT_EQ(hex.shape.kind, ShapeKind::Polygon)
            << size.width << "x" << size.height << ": " << hex.shape.reason;
        EXPECT_EQ(hex.shape.sides, 6) << size.width << "x" << size.height;
        // Y las propuestas van detrás de la clase: seis reglas y seis ángulos a
        // cualquier resolución. Sin esto, la clase podría aguantar y la lista de
        // propuestas venirse abajo, que para el operador es lo mismo que fallar.
        EXPECT_EQ(countNamed(hex.proposals, "Lado "), 6) << size.width << "x" << size.height;
        const double side = measuredOf(hex, "Lado 1");
        ASSERT_GT(side, 0.0) << size.width << "x" << size.height;
        const double sideRatio = side / size.height;

        minDiscRatio = std::min(minDiscRatio, discRatio);
        maxDiscRatio = std::max(maxDiscRatio, discRatio);
        minSideRatio = std::min(minSideRatio, sideRatio);
        maxSideRatio = std::max(maxSideRatio, sideRatio);
        std::printf("               %4dx%-4d   %4d   %-8s  %5d   %.5f   %.5f\n", size.width,
                    size.height, radius, pci::vision::shapeKindName(hex.shape.kind),
                    hex.shape.sides, discRatio, sideRatio);

        // Y el valor absoluto también tiene que cuadrar con lo dibujado, no solo
        // ser constante: dos medidas igual de equivocadas a las cuatro
        // resoluciones darían dispersión cero y el test pasaría midiendo mal.
        //
        // Cotas MEDIDAS: el Ø se equivoca como mucho un 0,12 % (en 320x240, que
        // es donde el borde rasterizado pesa más) y el lado del hexágono un
        // 0,55 %. Se dejan en 1,5 % y 2,5 %.
        EXPECT_NEAR(diameter, 2.0 * radius, 2.0 * radius * 0.015)
            << size.width << "x" << size.height;
        EXPECT_NEAR(side, sideOfRegularPolygon(6, radius), sideOfRegularPolygon(6, radius) * 0.025)
            << size.width << "x" << size.height;
    }

    const double discSpread = (maxDiscRatio - minDiscRatio) / maxDiscRatio;
    const double sideSpread = (maxSideRatio - minSideRatio) / maxSideRatio;
    std::printf("  [resolución] dispersión relativa: Ø %.2f %% | lado %.2f %%\n",
                discSpread * 100.0, sideSpread * 100.0);
    // Cotas MEDIDAS (ver tabla): entre 320x240 y 1920x1080 el Ø relativo se mueve
    // un 0,12 % y el lado del hexágono un 0,70 %. Se dejan en 1 % y 2 %: lo que
    // queda es el redondeo del circunradio a un entero de píxeles y el borde
    // rasterizado, que pesa relativamente más cuanto menos píxeles ocupa la
    // pieza. Un paso de remuestreo fijo en píxeles —el fallo que este test
    // vigila— daría dispersiones de otro orden, o directamente otra clase.
    EXPECT_LT(discSpread, 0.01);
    EXPECT_LT(sideSpread, 0.02);
}

// ---------------------------------------------------------------------------
// 5. VARIAS PIEZAS en la escena
// ---------------------------------------------------------------------------

// Las dos funciones tienen contratos distintos y el reparto es la razón de que
// existan las dos: `analyzeFrame` se queda con la MAYOR —es el camino que corre
// en cada frame del vídeo y no puede pagar por analizar el resto— y
// `analyzeFrames` las devuelve TODAS, ordenadas de mayor a menor.
//
// El fallo que evita: el histórico de este repositorio dice que el recuento
// «siempre daba 1», o sea que una bandeja con cinco tornillos y otra con seis
// daban el mismo resultado. Un contador que siempre dice uno cuadra con la
// realidad justo el día que hay una pieza, que es cuando nadie mira.
TEST(DiverseImagesMultiple, TheLargestPieceWinsAndAllOfThemAreCounted) {
    const cv::Size frame(640, 480);

    struct Piece {
        cv::Point centre;
        int radius;
    };
    // Radios decrecientes a propósito: así el orden esperado de `analyzeFrames`
    // es el de la lista y se puede comprobar renglón a renglón. El menor (r=30)
    // ocupa 2827 px², bien por encima del filtro de área (0,5 % de 640x480 =
    // 1536 px²): el test prueba el recuento, no el filtro.
    const std::vector<Piece> layout{{{110, 120}, 55}, {{320, 120}, 50}, {{530, 120}, 45},
                                    {{110, 340}, 40}, {{320, 340}, 35}, {{530, 340}, 30}};

    for (int count : {2, 6}) {
        cv::Mat mask = emptyMask(frame);
        for (int i = 0; i < count; ++i) {
            drawDisc(mask, layout[static_cast<std::size_t>(i)].centre,
                     layout[static_cast<std::size_t>(i)].radius);
        }
        const Scene scene = lightOnDark(mask);

        // a) `analyzeFrame` se queda con la mayor, y hay que demostrar CUÁL:
        //    que devuelva algo no dice nada, porque devolvería algo igual si se
        //    quedara con la primera que encuentra.
        const Analysed run = runEverything(scene.gray);
        ASSERT_TRUE(run.detected) << count << " piezas: " << run.error;
        const double biggestNominal = 2.0 * layout.front().radius;
        EXPECT_EQ(run.shape.kind, ShapeKind::Circle) << run.shape.reason;
        EXPECT_NEAR(measuredOf(run, "Ø"), biggestNominal, biggestNominal * 0.03)
            << count << " piezas: `analyzeFrame` no se quedó con la mayor";
        EXPECT_NEAR(cv::norm(run.analysis.contour.centroid - cv::Point2f(layout.front().centre)),
                    0.0, 2.0)
            << count << " piezas: la pieza elegida no está donde la mayor";

        // b) `analyzeFrames` las cuenta todas y las devuelve ordenadas.
        const auto all = pci::vision::analyzeFrames(scene.gray);
        ASSERT_TRUE(all.isOk()) << count << " piezas: " << all.error().message;
        ASSERT_EQ(static_cast<int>(all.value().size()), count)
            << "esperaba " << count << " piezas y devolvió " << all.value().size();

        std::printf("  [%d piezas] ", count);
        for (std::size_t i = 0; i < all.value().size(); ++i) {
            const auto& piece = all.value()[i];
            const double nominal = 2.0 * layout[i].radius;
            // Y cada una es la suya: el orden tiene que ser de mayor a menor y
            // cada pieza tiene que estar donde se dibujó. Un vector con seis
            // copias de la misma pieza también tendría tamaño 6.
            EXPECT_NEAR(cv::norm(piece.contour.centroid - cv::Point2f(layout[i].centre)), 0.0, 2.0)
                << "pieza " << i << " no está en su sitio";
            const double diameter = 2.0 * std::sqrt(piece.contour.area / CV_PI);
            std::printf("Ø%.1f(%.0f) ", diameter, nominal);
            // Cota MEDIDA: el Ø equivalente sale sistemáticamente un pelo CORTO
            // —de 1,0 % en la pieza de 110 px a 1,8 % en la de 60— porque
            // `contourArea` integra el polígono que pasa por los centros de los
            // píxeles del borde, y ese polígono es medio píxel más pequeño que la
            // figura por cada lado. Es un sesgo del área, no del recuento, y 3 %
            // lo cubre entero.
            EXPECT_NEAR(diameter, nominal, nominal * 0.03)
                << "pieza " << i << ": Ø equivalente " << diameter << " de un nominal " << nominal;
            if (i > 0) {
                EXPECT_LE(piece.contour.area, all.value()[i - 1].contour.area)
                    << "no vienen ordenadas de mayor a menor";
            }
        }
        std::printf("\n");

        // c) `expectedPieces` no cambia la detección: es para que quien juzgue
        //    pueda decir «esperaba 6, veo 5». Se comprueba que ponerlo mal no
        //    altere el recuento, que es lo que haría de él una trampa.
        PipelineConfig lying;
        lying.expectedPieces = 99;
        const auto stillAll = pci::vision::analyzeFrames(scene.gray, lying);
        ASSERT_TRUE(stillAll.isOk());
        EXPECT_EQ(stillAll.value().size(), all.value().size())
            << "`expectedPieces` no puede cambiar cuántas piezas se ven";
    }
}

// ---------------------------------------------------------------------------
// 6. NADA: escenas sin pieza
// ---------------------------------------------------------------------------

// Una escena sin pieza tiene que fallar DICIENDO POR QUÉ, no devolver una pieza
// inventada. Es el caso que más daño hace en producción porque no se parece a un
// fallo: el programa sigue, mide, y enseña un veredicto sobre algo que no
// existe.
//
// El fallo que evita: el umbral de Otsu SIEMPRE parte la imagen en dos, tenga
// sentido o no. Sobre un fondo plano o sobre ruido puro devuelve una máscara con
// manchas, y sin el filtro de área alguna de esas manchas se convertiría en «la
// pieza». Aquí se barren los tres casos del enunciado más los degenerados que
// llegan de una cámara desconectada.
TEST(DiverseImagesNothing, AnEmptyOrFlatOrNoisySceneFailsWithAReasonInsteadOfInventingAPiece) {
    const cv::Size frame(640, 480);

    std::vector<std::pair<const char*, cv::Mat>> scenes;
    scenes.emplace_back("vacía (todo negro)", cv::Mat(frame, CV_8UC1, cv::Scalar(0)));
    scenes.emplace_back("todo blanco", cv::Mat(frame, CV_8UC1, cv::Scalar(255)));
    scenes.emplace_back("un solo color (128)", cv::Mat(frame, CV_8UC1, cv::Scalar(128)));
    scenes.emplace_back("un solo color (30)", cv::Mat(frame, CV_8UC1, cv::Scalar(kDark)));

    // Ruido con semilla FIJA: un test que a veces pasa no es un test. Se prueban
    // los dos ruidos que se ven de verdad: el uniforme de una entrada
    // desconectada y el gaussiano de un sensor a mucha ganancia.
    cv::Mat uniform(frame, CV_8UC1);
    cv::RNG(20260815).fill(uniform, cv::RNG::UNIFORM, 0, 256);
    scenes.emplace_back("ruido uniforme", uniform);

    cv::Mat gaussian(frame, CV_8UC1);
    cv::RNG(20260816).fill(gaussian, cv::RNG::NORMAL, 128, 30);
    scenes.emplace_back("ruido gaussiano", gaussian);

    std::printf("  [nada] escena                  analyzeFrame              analyzeFrames\n");
    for (const auto& [name, image] : scenes) {
        const auto one = pci::vision::analyzeFrame(image);
        const auto many = pci::vision::analyzeFrames(image);
        std::printf("         %-22s  %-24s  %s\n", name,
                    one.isOk() ? "PIEZA INVENTADA" : one.error().message.c_str(),
                    many.isOk() ? "PIEZAS INVENTADAS" : many.error().message.c_str());

        EXPECT_FALSE(one.isOk()) << name << ": se inventó una pieza donde no hay ninguna";
        EXPECT_FALSE(many.isOk()) << name << ": se inventó una lista de piezas";
        if (!one.isOk()) {
            // Un fallo sin motivo no se puede arreglar: el operador ve que no
            // mide y no sabe si es la luz, el encuadre o el ajuste.
            EXPECT_FALSE(one.error().message.empty()) << name;
        }

        // Y el juicio de calidad tiene que decir lo mismo por su cuenta: sin
        // pieza no hay captura que registrar.
        const auto quality = pci::vision::computeQualityMetrics(image, nullptr);
        EXPECT_FALSE(quality.pieceFound) << name;
        const auto verdict = pci::domain::validateQuality(quality);
        EXPECT_FALSE(verdict.isOk()) << name;
    }
}

// El caso de «nada» que SÍ se cuela, y va aparte porque el resultado no es el
// que uno querría: un DEGRADADO suave sin ninguna pieza —una pared iluminada de
// lado, una mesa con la luz a un lado, el viñeteo de un objetivo barato—.
//
// FALLO REAL DE PRODUCCIÓN, documentado aquí en vez de arreglado: `analyzeFrame`
// devuelve OK sobre un degradado lineal de 40 a 210 y da por pieza la mitad
// oscura de la imagen. La razón es estructural: Otsu SIEMPRE parte la imagen en
// dos, tenga sentido o no, y sobre un degradado la parte justo por la mitad. Las
// dos defensas que hay no lo cogen — la mitad de la imagen queda por debajo de
// `maxAreaFraction` (0,9) y muy por encima de `minAreaFraction` (0,005)— así que
// el pipeline entrega una «pieza» de 320x480 sin decir nada.
//
// Lo único que lo para es el juicio de calidad, y de rebote: como esa mancha
// llega hasta los cuatro bordes, `pieceTouchesBorder` se enciende y
// `validateQuality` rechaza la captura. Vale para registrar una pieza, pero
// `analyzeFrame` se usa en cada frame del vídeo sin pasar por ahí.
//
// Este test fija el comportamiento de HOY para que el día que se le ponga una
// medida de contraste a la segmentación —lo que de verdad haría falta— se note
// aquí y se pueda cambiar a la aserción buena.
TEST(DiverseImagesNothing, ASmoothGradientWithNoPieceIsOnlyStoppedByTheQualityCheck) {
    cv::Mat gradient(cv::Size(640, 480), CV_8UC1);
    for (int y = 0; y < gradient.rows; ++y) {
        for (int x = 0; x < gradient.cols; ++x) {
            gradient.at<unsigned char>(y, x) =
                static_cast<unsigned char>(40 + 170 * x / gradient.cols);
        }
    }

    const auto one = pci::vision::analyzeFrame(gradient);
    // Lo que pasa hoy: NO falla. Si algún día empieza a fallar, este test salta
    // y hay que venir a celebrarlo cambiando la aserción — un fallo con motivo
    // sería la respuesta correcta.
    ASSERT_TRUE(one.isOk()) << "ahora sí se niega («" << one.error().message
                            << "»): cambia este test por la aserción buena";
    const cv::Rect box = cv::boundingRect(one.value().contour.points);
    const double fraction = one.value().contour.area / (gradient.cols * 1.0 * gradient.rows);
    std::printf("  [degradado] `analyzeFrame` devuelve una «pieza» de %dx%d px (%.0f %% del "
                "frame) donde no hay ninguna\n",
                box.width, box.height, fraction * 100.0);
    // Y la «pieza» es media imagen: no es una mancha pequeña que se pueda
    // confundir con una viruta, es la mitad del encuadre.
    EXPECT_GT(fraction, 0.3) << "la mancha inventada ocupa el " << fraction * 100.0 << " %";

    // La red que sí lo coge, y es la que hay que no romper: la mancha llega al
    // borde, así que la captura queda rechazada con su motivo.
    const auto quality = pci::vision::computeQualityMetrics(gradient, &one.value());
    EXPECT_TRUE(quality.pieceTouchesBorder);
    const auto verdict = pci::domain::validateQuality(quality);
    ASSERT_FALSE(verdict.isOk()) << "nada impide registrar una captura de una pared vacía";
    EXPECT_EQ(verdict.error().message, "La pieza está cortada por el borde del encuadre");
    std::printf("  [degradado] lo único que lo para: «%s»\n", verdict.error().message.c_str());
}

// Los degenerados que llegan cuando la cámara se cae a mitad de sesión, y el
// otro extremo: una «pieza» que cubre casi toda la imagen, que no es una pieza,
// es una iluminación que ha fallado. Ninguno puede lanzar ni devolver una medida.
//
// El fallo que evita: una excepción aquí tumba el hilo de vídeo entero, y un
// `cv::Mat` vacío o de un tipo raro es exactamente lo que llega de una fuente que
// se desconecta.
TEST(DiverseImagesNothing, DegenerateFramesFailCleanlyAndNothingThrows) {
    // Imagen vacía y formatos que el pipeline no puede tratar.
    const std::vector<std::pair<const char*, cv::Mat>> broken{
        {"Mat vacío", cv::Mat()},
        {"1x1", cv::Mat(1, 1, CV_8UC1, cv::Scalar(200))},
        {"columna de 1 px", cv::Mat(480, 1, CV_8UC1, cv::Scalar(200))},
        {"4 canales", cv::Mat(cv::Size(64, 64), CV_8UC4, cv::Scalar(200, 200, 200, 255))},
    };
    for (const auto& [name, image] : broken) {
        pci::core::Result<pci::vision::PieceAnalysis> one =
            pci::core::Result<pci::vision::PieceAnalysis>::err("sin llamar");
        ASSERT_NO_THROW(one = pci::vision::analyzeFrame(image)) << name;
        EXPECT_FALSE(one.isOk()) << name << ": midió algo";
        EXPECT_FALSE(one.error().message.empty()) << name;
        ASSERT_NO_THROW((void)pci::vision::analyzeFrames(image)) << name;
        std::printf("  [degenerado] %-16s -> «%s»\n", name, one.error().message.c_str());
    }

    // Y el otro extremo: una mancha que se come el encuadre. Eso no es una
    // pieza, es una iluminación que ha fallado, y no hay ninguna medida correcta
    // que dar — lo bueno es negarse y decir dónde mirar.
    //
    // El recuadro se deja a 10 px de los bordes a propósito: si llegara hasta el
    // borde, la polaridad automática lo tomaría por FONDO —el marco exterior
    // saldría blanco y se invertiría la máscara— y el error sería «no se
    // encontró ninguna pieza», que es la respuesta correcta por el motivo
    // equivocado. Así se prueba el filtro de `maxAreaFraction`, que es lo que
    // este trozo dice probar.
    cv::Mat huge = emptyMask(cv::Size(640, 480));
    cv::rectangle(huge, cv::Point(10, 10), cv::Point(629, 469), cv::Scalar(255), cv::FILLED);
    const double hugeFraction = cv::countNonZero(huge) / (640.0 * 480.0);
    ASSERT_GT(hugeFraction, 0.9) << "la mancha del test no llega al 90 %: no prueba el filtro";
    const auto covered = pci::vision::analyzeFrame(lightOnDark(huge).gray);
    ASSERT_FALSE(covered.isOk()) << "una mancha que cubre el " << hugeFraction * 100.0
                                 << " % del frame no es una pieza";
    EXPECT_EQ(covered.error().message,
              "La segmentación cubre casi toda la imagen (revisa fondo/iluminación)");
    std::printf("  [degenerado] mancha que cubre el %.0f %% -> «%s»\n", hugeFraction * 100.0,
                covered.error().message.c_str());

    // Y `proposeTools` con una máscara vacía: no puede proponer nada sobre una
    // pieza que no existe. Devuelve la lista vacía, que es la respuesta correcta.
    const cv::Mat grayOnly(cv::Size(64, 64), CV_8UC1, cv::Scalar(120));
    EXPECT_TRUE(proposeTools(grayOnly, cv::Mat(), {}, everything()).empty());
    EXPECT_TRUE(proposeTools(cv::Mat(), grayOnly, {}, everything()).empty());
    EXPECT_TRUE(
        proposeTools(grayOnly, cv::Mat(cv::Size(64, 64), CV_8UC1, cv::Scalar(0)), {}, everything())
            .empty());
}

// ===========================================================================
// 7. Compresión JPEG: lo que trae CUALQUIER imagen que llegue de fuera
// ===========================================================================

namespace {

// Pasa la imagen por un JPEG de verdad, ida y vuelta. No se simulan los
// artefactos: se generan con el mismo codificador que usará la cámara o el
// móvil del operador, porque el daño del JPEG no es ruido blanco — son bloques
// de 8x8 y campanas alrededor de los bordes de alto contraste, que es
// exactamente donde se mide.
cv::Mat throughJpeg(const cv::Mat& gray, int quality) {
    std::vector<uchar> buffer;
    const std::vector<int> params{cv::IMWRITE_JPEG_QUALITY, quality};
    if (!cv::imencode(".jpg", gray, buffer, params)) {
        return {};
    }
    return cv::imdecode(buffer, cv::IMREAD_GRAYSCALE);
}

}  // namespace

TEST(DiverseImagesJpeg, MeasurementsSurviveTheCompressionEveryRealImageArrivesWith) {
    // Todo lo que el banco prueba hasta aquí son mapas de bits perfectos, y eso
    // no es lo que llega: una foto de cámara o de móvil viene en JPEG, y sus
    // artefactos se concentran en los bordes de alto contraste — justo donde se
    // mide. Una calibración hecha sobre un PNG y aplicada a un JPEG mediría otra
    // cosa sin que nada avisara.
    const cv::Size size(640, 480);
    const int radius = 150;
    const double nominalSide = sideOfRegularPolygon(6, radius);

    cv::Mat hexMask = emptyMask(size);
    drawRegularPolygon(hexMask, 6, {320, 240}, radius);
    const Scene hex = darkOnLight(hexMask);

    cv::Mat discMask = emptyMask(size);
    drawDisc(discMask, {320, 240}, radius);
    const Scene disc = darkOnLight(discMask);

    std::printf("\n=== compresión JPEG ===\n");
    std::printf("%-9s | %-10s %5s %9s %8s | %-10s %9s %8s\n", "calidad", "hexágono", "lados",
                "lado px", "error", "disco", "Ø px", "error");

    double worstHexError = 0.0;
    double worstDiscError = 0.0;
    for (const int quality : {100, 95, 85, 70, 50, 30, 15}) {
        const cv::Mat hexJpeg = throughJpeg(hex.gray, quality);
        const cv::Mat discJpeg = throughJpeg(disc.gray, quality);
        ASSERT_FALSE(hexJpeg.empty()) << "OpenCV no pudo codificar JPEG en esta máquina";

        const Analysed h = runEverything(hexJpeg);
        const Analysed d = runEverything(discJpeg);
        ASSERT_TRUE(h.detected) << "calidad " << quality << ": " << h.error;
        ASSERT_TRUE(d.detected) << "calidad " << quality << ": " << d.error;

        const double side = measuredOf(h, "Lado ");
        const double diameter = measuredOf(d, "Ø");
        const double hexError = side > 0.0 ? std::abs(side - nominalSide) / nominalSide : 1.0;
        const double discError = diameter > 0.0
                                     ? std::abs(diameter - 2.0 * radius) / (2.0 * radius)
                                     : 1.0;
        worstHexError = std::max(worstHexError, hexError);
        worstDiscError = std::max(worstDiscError, discError);

        std::printf("%-9d | %-10s %5d %9.2f %7.2f %% | %-10s %9.2f %7.2f %%\n", quality,
                    pci::vision::shapeKindName(h.shape.kind), h.shape.sides, side,
                    100.0 * hexError, pci::vision::shapeKindName(d.shape.kind), diameter,
                    100.0 * discError);

        // La CLASE tiene que aguantar hasta la calidad más baja: si a un
        // hexágono comprimido se le dejan de contar los lados, la medición
        // automática le propondría otra cosa distinta y el operador vería una
        // plantilla que no reconoce.
        EXPECT_EQ(h.shape.kind, pci::vision::ShapeKind::Polygon) << "calidad " << quality;
        EXPECT_EQ(h.shape.sides, 6) << "calidad " << quality << ": contó " << h.shape.sides;
        EXPECT_EQ(d.shape.kind, pci::vision::ShapeKind::Circle) << "calidad " << quality;
    }

    std::printf("  peor error: hexágono %.2f %%, disco %.2f %%\n", 100.0 * worstHexError,
                100.0 * worstDiscError);
    // Las cotas se fijan con holgura sobre lo medido. Lo que se está afirmando
    // es que el JPEG no mueve la medida más que el propio rasterizado.
    EXPECT_LT(worstHexError, 0.03) << "el JPEG mueve el lado más de un 3 %";
    EXPECT_LT(worstDiscError, 0.03) << "el JPEG mueve el diámetro más de un 3 %";
}

TEST(DiverseImagesJpeg, ALowContrastPieceIsWhereTheCompressionActuallyHurts) {
    // Con mucho contraste el JPEG casi no molesta: el borde sigue siendo un
    // escalón enorme. Donde de verdad hace daño es con POCO contraste, porque
    // ahí el escalón es del tamaño del error de cuantización y el codificador se
    // lo come. Es el caso de una pieza gris sobre una mesa gris, que es lo que
    // pasa cuando la luz no es la que debería.
    const cv::Size size(640, 480);
    const int radius = 150;
    cv::Mat mask = emptyMask(size);
    drawDisc(mask, {320, 240}, radius);

    // Pieza a 120 sobre fondo a 90: los mismos 30 niveles que ya usa el banco
    // para el caso de contraste bajo.
    cv::Mat gray(size, CV_8UC1, cv::Scalar(90));
    gray.setTo(cv::Scalar(120), mask);

    std::printf("\n=== JPEG con poco contraste (pieza 120 / fondo 90) ===\n");
    int lastGood = 100;
    for (const int quality : {95, 70, 50, 30, 15, 5}) {
        const cv::Mat jpeg = throughJpeg(gray, quality);
        ASSERT_FALSE(jpeg.empty());
        const Analysed run = runEverything(jpeg);
        const double diameter = measuredOf(run, "Ø");
        std::printf("  calidad %3d -> %s  Ø=%.2f  %s\n", quality,
                    run.detected ? pci::vision::shapeKindName(run.shape.kind) : "sin pieza",
                    diameter, run.detected ? "" : run.error.c_str());
        // «Aguanta» es que se pueda MEDIR, no solo que se reconozca la figura.
        // A calidad 5 el disco se sigue clasificando como redondo y en cambio ya
        // no se propone su diámetro: contar eso como bueno habría dejado el test
        // afirmando algo más flojo de lo que parece.
        if (run.detected && run.shape.kind == pci::vision::ShapeKind::Circle &&
            diameter > 0.0) {
            lastGood = quality;
        }
    }
    std::printf("  aguanta hasta calidad %d\n", lastGood);

    // Lo que se afirma es el lado bueno de la frontera: a calidad 70 —que es la
    // que sale de casi cualquier cámara— una pieza de poco contraste todavía se
    // mide. Por debajo no se afirma nada a propósito: la respuesta correcta ahí
    // es «no se puede medir», y fijar una degradación concreta sería atar el
    // test al codificador.
    EXPECT_LE(lastGood, 70) << "aguanta menos de lo que aguanta cualquier cámara";
}
