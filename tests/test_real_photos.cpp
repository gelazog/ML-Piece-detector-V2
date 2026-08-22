// Pruebas contra FOTOGRAFÍAS REALES, no imágenes generadas aquí.
//
// Por qué existe este fichero: durante meses, todo lo que este proyecto probaba
// eran PNG que se dibujaba a sí mismo —rectángulos planos, sin ruido, sin
// reflejos, sin compresión— y por eso todos los fallos de las últimas semanas
// los encontró el operador usando la aplicación y no la batería de pruebas.
// Una foto de verdad trae, de una sola vez, todo lo que las sintéticas no
// tienen: reflejos especulares, sombras suaves, compresión JPEG, y sobre todo
// COSAS QUE NO SON LA PIEZA (una regla, una barra de escala, texto).
//
// El corpus NO está en el repositorio: son fotografías de terceros con licencia
// propia y pesan. Se descargan con `python3 testdata/fetch_real_images.py`, y
// estas pruebas SE SALTAN solas si no están, para que nadie se quede sin poder
// compilar por no tener red.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "inspection_editor/auto_measure.h"
#include "inspection_editor/execution/tool_executor.h"
#include "vision/pipeline.h"
#include "vision/shape_class.h"

namespace {

std::filesystem::path corpusDir() {
    // El binario de pruebas vive en build/<preset>/tests; el corpus, en
    // testdata/real junto a la raíz del proyecto.
    for (const auto* candidate : {"testdata/real", "../testdata/real",
                                  "../../testdata/real", "../../../testdata/real"}) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return std::filesystem::path(candidate);
        }
    }
    return {};
}

cv::Mat loadReal(const std::string& name) {
    const auto dir = corpusDir();
    if (dir.empty()) {
        return {};
    }
    const auto path = dir / name;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return {};
    }
    return cv::imread(path.string(), cv::IMREAD_COLOR);
}

// Un `SKIP` explícito y con instrucciones: una prueba que se salta en silencio
// es una prueba que nadie echa de menos cuando deja de correr para siempre.
#define REQUIRE_CORPUS(image)                                                        \
    if ((image).empty()) {                                                           \
        GTEST_SKIP() << "corpus de fotos reales no descargado. "                      \
                        "Ejecuta: python3 testdata/fetch_real_images.py";             \
    }

// La forma se clasifica APARTE del analisis y sobre la máscara CON AGUJEROS,
// igual que hace el banco `pci_probe`. Replicarlo aquí importa: con la máscara
// rellena, una arandela de verdad salía «círculo» y perdía su agujero.
pci::vision::ShapeClass shapeOf(const cv::Mat& photo,
                                const pci::vision::PieceAnalysis& piece,
                                const pci::vision::PipelineConfig& config) {
    cv::Mat gray;
    if (photo.channels() == 3) {
        cv::cvtColor(photo, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = photo;
    }
    const cv::Mat withHoles =
        pci::vision::pieceMaskWithHoles(gray, piece.mask, config.segmentation);
    return pci::vision::classifyShape(piece.contour.points, withHoles);
}

}  // namespace

// EL FALLO QUE DESTAPÓ LA PRIMERA FOTO REAL, en el primer intento.
//
// La foto es una bola oscura sobre fondo claro con una regla metálica al lado.
// La segmentación se queda con las marcas grabadas de la regla —son oscuras,
// como la bola— y el contorno resultante es largo, delgado y casi recto.
//
// Ajustar una circunferencia a un contorno casi recto da un círculo ENORME: se
// publicó un «Ø exterior: 130.901 px» sobre una imagen de 1920 px de ancho.
// Sesenta y ocho veces más ancha que la foto, con un motivo que lo explicaba
// con toda seguridad.
//
// Que la pieza detectada sea la regla y no la bola es un caso difícil y
// discutible. Que se publique un diámetro imposible no lo es.
TEST(RealPhotos, ACircleCanNeverBeWiderThanThePhotographItCameFrom) {
    const cv::Mat photo = loadReal("bola_oscura_sobre_claro_10mm.jpg");
    REQUIRE_CORPUS(photo);

    const auto analysis = pci::vision::analyzeFrame(photo, {});
    ASSERT_TRUE(analysis.isOk()) << analysis.error().message;

    const auto shape = shapeOf(photo, analysis.value(), {});
    const double frameDiagonal = std::hypot(static_cast<double>(photo.cols),
                                            static_cast<double>(photo.rows));
    std::printf("  [real] %dx%d -> clase %d, Ø %.0f px (diagonal del frame %.0f px)\n",
                photo.cols, photo.rows, static_cast<int>(shape.kind), shape.outerDiameter,
                frameDiagonal);

    EXPECT_LE(shape.outerDiameter, frameDiagonal)
        << "publica un diámetro mayor que la propia fotografía: eso no es una medida, "
           "es un ajuste numérico que nadie acotó";
    EXPECT_LE(shape.innerDiameter, frameDiagonal);
}

// Y lo mismo dicho como propiedad, sobre TODAS las fotos del corpus: ninguna
// medida de tamaño puede salirse de la imagen de la que se sacó. Es la clase de
// comprobación que no necesita saber qué hay en la foto.
TEST(RealPhotos, NoMeasurementEscapesTheImageItWasTakenFrom) {
    const auto dir = corpusDir();
    if (dir.empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }
    int checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".jpg") {
            continue;
        }
        const cv::Mat photo = cv::imread(entry.path().string(), cv::IMREAD_COLOR);
        if (photo.empty()) {
            continue;
        }
        const auto analysis = pci::vision::analyzeFrame(photo, {});
        if (!analysis.isOk()) {
            continue;  // sin pieza es una respuesta legítima, no un fallo
        }
        const double diagonal = std::hypot(static_cast<double>(photo.cols),
                                           static_cast<double>(photo.rows));
        const auto shape = shapeOf(photo, analysis.value(), {});
        EXPECT_LE(shape.outerDiameter, diagonal) << entry.path().filename().string();
        EXPECT_LE(analysis.value().contour.area,
                  static_cast<double>(photo.cols) * photo.rows)
            << entry.path().filename().string() << ": mide más área que la imagen entera";
        ++checked;
    }
    std::printf("  [real] %d fotos comprobadas\n", checked);
    EXPECT_GT(checked, 0) << "el corpus está pero no se pudo leer ninguna foto";
}

// Una pieza OSCURA sobre fondo CLARO, que es la polaridad contraria a todas las
// pruebas sintéticas de este proyecto —siempre fueron claro sobre oscuro—, y
// con VERDAD DE CAMPO: la bola mide 10 mm.
//
// Se mide como lo haría el operador: rodeándola con una zona de trabajo, que es
// exactamente para lo que sirve esa herramienta cuando en la foto hay algo más.
TEST(RealPhotos, ADarkBallOnALightBackgroundMeasuresLikeTheCircleItIs) {
    const cv::Mat photo = loadReal("bola_oscura_sobre_claro_10mm.jpg");
    REQUIRE_CORPUS(photo);

    pci::vision::PipelineConfig config;
    config.roi = cv::Rect(560, 720, 420, 420);  // alrededor de la bola

    const auto analysis = pci::vision::analyzeFrame(photo, config);
    ASSERT_TRUE(analysis.isOk()) << analysis.error().message;
    const auto& value = analysis.value();
    const auto shape = shapeOf(photo, value, config);

    std::printf("  [real] bola de 10 mm: clase %d, Ø %.1f px, area %.0f px2, redondez %.2f px\n",
                static_cast<int>(shape.kind), shape.outerDiameter, value.contour.area,
                shape.roundness);

    EXPECT_EQ(shape.kind, pci::vision::ShapeKind::Circle)
        << "una esfera fotografiada es un círculo; si sale otra cosa, la clasificación "
           "no sobrevive a un reflejo especular";
    ASSERT_GT(shape.outerDiameter, 0.0);

    // La comprobación fuerte no es el número, es su COHERENCIA: el área que se
    // publica y el diámetro que se publica tienen que hablar de la misma pieza.
    // Dos cifras sacadas de sitios distintos que no cuadran son dos medidas de
    // las cuales al menos una es mentira.
    const double radius = shape.outerDiameter / 2.0;
    const double areaFromDiameter = CV_PI * radius * radius;
    const double mismatch =
        std::abs(value.contour.area - areaFromDiameter) / areaFromDiameter;
    std::printf("  [real] area medida %.0f px2 vs la del Ø publicado %.0f px2 (%.2f %%)\n",
                value.contour.area, areaFromDiameter, 100.0 * mismatch);
    EXPECT_LT(mismatch, 0.05)
        << "el área y el diámetro publicados no describen la misma pieza";

    // Y la escala sale creíble: 10 mm reales sobre unos 250 px son ~25 px/mm,
    // que es lo que cabe esperar de una foto de 1920 px de una escena de ~8 cm.
    const double pxPerMm = shape.outerDiameter / 10.0;
    std::printf("  [real] con 10 mm nominales: %.1f px/mm\n", pxPerMm);
    EXPECT_GT(pxPerMm, 5.0);
    EXPECT_LT(pxPerMm, 100.0);
}

// VARIAS PIEZAS en una foto de verdad, que es lo que se pidió probar.
//
// La foto trae tres bolas sobre negro y, además, dos cosas que NO son piezas:
// una barra de escala y el texto «10mm». Medido aparte con OpenCV, un umbral de
// Otsu deja ocho componentes: las tres bolas (una partida en dos por su propio
// reflejo especular), la barra y tres letras.
//
// Lo que se le exige al recuento no es que acierte tres —eso depende de filtros
// que el operador ajusta— sino que el filtro de área HAGA algo: que la barra de
// escala y las letras no cuenten como piezas.
TEST(RealPhotos, TextAndAScaleBarAreNotPieces) {
    const cv::Mat photo = loadReal("bolas_tres_sobre_negro.jpg");
    REQUIRE_CORPUS(photo);

    const auto all = pci::vision::analyzeFrames(photo, {});
    ASSERT_TRUE(all.isOk()) << all.error().message;
    const auto& pieces = all.value();

    std::printf("  [real] tres bolas + barra de escala + texto -> %zu piezas contadas\n",
                pieces.size());
    for (std::size_t i = 0; i < pieces.size() && i < 8; ++i) {
        const cv::Rect box = cv::boundingRect(pieces[i].contour.points);
        std::printf("      %zu: area %8.0f px2  caja %dx%d\n", i + 1,
                    pieces[i].contour.area, box.width, box.height);
    }

    ASSERT_FALSE(pieces.empty());
    // Las letras miden ~690 px2 y la barra ~4.300 px2, frente a las bolas que
    // pasan de 48.000. Si alguna de esas apareciera como pieza, el recuento que
    // se le enseña al operador sería mentira.
    const double smallest = pieces.back().contour.area;
    EXPECT_GT(smallest, 10000.0)
        << "algo diminuto se coló como pieza: con 690 px2 son las letras del rótulo";

    // Y ninguna pieza puede ser más ancha que la foto.
    for (const auto& piece : pieces) {
        const cv::Rect box = cv::boundingRect(piece.contour.points);
        EXPECT_LE(box.width, photo.cols);
        EXPECT_LE(box.height, photo.rows);
    }
}

// Una foto real se mide IGUAL las dos veces. Parece obvio y no lo es: sobre
// material sintético cualquier cosa es determinista, y es al meter ruido,
// reflejos y bordes de compresión cuando un empate mal resuelto —dos contornos
// del mismo tamaño, un umbral justo en el filo— se vuelve un resultado que
// baila entre ejecuciones.
TEST(RealPhotos, MeasuringTheSamePhotographTwiceGivesTheSameNumbers) {
    const cv::Mat photo = loadReal("bola_oscura_sobre_claro_20mm.jpg");
    REQUIRE_CORPUS(photo);

    const auto first = pci::vision::analyzeFrame(photo, {});
    const auto second = pci::vision::analyzeFrame(photo, {});
    ASSERT_TRUE(first.isOk());
    ASSERT_TRUE(second.isOk());

    EXPECT_DOUBLE_EQ(first.value().contour.area, second.value().contour.area);
    EXPECT_DOUBLE_EQ(first.value().contour.perimeter, second.value().contour.perimeter);
    EXPECT_DOUBLE_EQ(shapeOf(photo, first.value(), {}).outerDiameter,
                     shapeOf(photo, second.value(), {}).outerDiameter);
    std::printf("  [real] dos pasadas sobre la misma foto: area %.1f px2 las dos veces\n",
                first.value().contour.area);
}

// ---------------------------------------------------------------------------
// LAS HERRAMIENTAS DE MEDIDA sobre una fotografía real
// ---------------------------------------------------------------------------
//
// Hasta aquí, el material real había servido para la segmentación y la
// clasificación de forma. Las herramientas —lo que de verdad da los números que
// el operador lee— seguían probándose solo contra discos y polígonos dibujados
// por el propio test.

// La bola de 10 mm trae VERDAD DE CAMPO doble: se sabe cuánto mide de verdad, y
// el clasificador de forma ya dijo su diámetro por otro camino. Si la
// herramienta y el clasificador no coinciden, uno de los dos miente — y el
// operador no tiene forma de saber cuál.
//
// Dos trampas que este test se comió antes de quedar bien, y que valen más que
// el test mismo:
//
//   1. El FIXTURE tiene que ser el mismo al proponer y al ejecutar. La geometría
//      de una herramienta vive en coordenadas de PIEZA; proponer con uno y
//      ejecutar con otro deja a la herramienta buscando el borde donde no está,
//      y entonces NADA mide. Pasó, y parecía un fallo del programa.
//   2. `runTool` devuelve un `Result`, y que ese `Result` sea válido sólo dice
//      que la herramienta CORRIÓ. Si midió o no lo dice `value().ok`. Confundir
//      los dos hace pasar por buena una medida de cero.
TEST(RealPhotoTools, TheOutsideDimensionAgreesWithTheShapeClassifierOnARealBall) {
    const cv::Mat photo = loadReal("bola_oscura_sobre_claro_10mm.jpg");
    REQUIRE_CORPUS(photo);

    pci::vision::PipelineConfig config;
    config.roi = cv::Rect(560, 720, 420, 420);
    const auto analysis = pci::vision::analyzeFrame(photo, config);
    ASSERT_TRUE(analysis.isOk()) << analysis.error().message;
    const auto& fixture = analysis.value().fixture;

    cv::Mat gray;
    cv::cvtColor(photo, gray, cv::COLOR_BGR2GRAY);
    const cv::Mat mask =
        pci::vision::pieceMaskWithHoles(gray, analysis.value().mask, config.segmentation);
    const auto shape = pci::vision::classifyShape(analysis.value().contour.points, mask);
    ASSERT_GT(shape.outerDiameter, 0.0) << "el clasificador no dio diámetro";

    const auto proposals = pci::inspection::proposeTools(gray, mask, fixture, {});
    ASSERT_FALSE(proposals.empty()) << "no propuso ninguna medida sobre una foto real";

    // En una bola, «Largo total» y «Ancho total» son las dos el diámetro: es una
    // pieza sin lado largo. Que las dos coincidan con el clasificador es más
    // fuerte que acertar una sola cifra.
    int compared = 0;
    for (const auto& proposal : proposals) {
        const auto& name = proposal.config.name;
        if (name != "Largo total" && name != "Ancho total") {
            continue;
        }
        const auto run = pci::inspection::runTool(gray, fixture, proposal.config);
        ASSERT_TRUE(run.isOk()) << run.error().message;
        ASSERT_TRUE(run.value().ok)
            << name << " se propuso y no consigue medir: " << run.value().detail;

        const double byTool = run.value().measured;
        const double gap = std::abs(byTool - shape.outerDiameter) / shape.outerDiameter;
        std::printf("  [real] %-12s %.1f px vs Ø %.1f del clasificador (%.2f %%)  "
                    "-> %.2f mm con 10 mm nominales\n",
                    name.c_str(), byTool, shape.outerDiameter, 100.0 * gap,
                    10.0 * byTool / shape.outerDiameter);
        EXPECT_LT(gap, 0.05)
            << name << " y el clasificador dan tamaños distintos de la misma bola: "
            << "uno de los dos miente y el operador no puede saber cuál";
        ++compared;
    }
    EXPECT_EQ(compared, 2)
        << "sobre una bola tienen que proponerse el largo y el ancho, y son el mismo "
           "diámetro";
}

// Y la propiedad que no depende de saber qué hay en la foto: NINGUNA herramienta
// puede publicar un número imposible.
//
// Es la misma clase de fallo que la circunferencia de Ø 130.901 px, y por eso se
// comprueba sobre TODAS las propuestas de TODAS las fotos del corpus: un ajuste
// numérico sin cota superior encuentra siempre la manera de dar un absurdo, y
// cuando lo da, viene con su explicación puesta.
TEST(RealPhotoTools, NoProposedToolPublishesAnImpossibleNumber) {
    const auto dir = corpusDir();
    if (dir.empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }

    int photos = 0;
    int toolsRun = 0;
    int suspicious = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".jpg") {
            continue;
        }
        const cv::Mat photo = cv::imread(entry.path().string(), cv::IMREAD_COLOR);
        if (photo.empty()) {
            continue;
        }
        const auto analysis = pci::vision::analyzeFrame(photo, {});
        if (!analysis.isOk()) {
            continue;
        }
        cv::Mat gray;
        cv::cvtColor(photo, gray, cv::COLOR_BGR2GRAY);
        const cv::Mat mask = pci::vision::pieceMaskWithHoles(gray, analysis.value().mask, {});

        // CON LOS AJUSTES POR DEFECTO, que es lo que ve el operador: el panel
        // de "Medir automáticamente" no muestra cien propuestas, muestra las
        // primeras. Medir con el tope subido diría cuántas propone el motor;
        // lo que hace falta saber es cuántas de las que SE ENSEÑAN sirven.
        const auto proposals =
            pci::inspection::proposeTools(gray, mask, analysis.value().fixture, {});
        ++photos;

        const double diagonal = std::hypot(static_cast<double>(photo.cols),
                                           static_cast<double>(photo.rows));
        for (const auto& proposal : proposals) {
            const auto run =
                pci::inspection::runTool(gray, analysis.value().fixture, proposal.config);
            if (!run.isOk()) {
                continue;  // no medir es una respuesta honesta
            }
            ++toolsRun;
            const double value = run.value().measured;
            if (!std::isfinite(value)) {
                ++suspicious;
                ADD_FAILURE() << entry.path().filename().string() << " / "
                              << proposal.config.name << ": publica un valor no finito";
                continue;
            }
            // Un ángulo va en grados y un área en px2, así que la cota de
            // "no puede pasar de la diagonal" sólo aplica a las longitudes.
            if (run.value().kind == pci::inspection::MeasuredKind::Length &&
                std::abs(value) > diagonal) {
                ++suspicious;
                ADD_FAILURE() << entry.path().filename().string() << " / "
                              << proposal.config.name << ": mide " << value
                              << " px en una foto cuya diagonal son " << diagonal << " px";
            }
        }
    }

    std::printf("  [real] %d fotos, %d herramientas, %d imposibles\n",
                photos, toolsRun, suspicious);
    EXPECT_GT(photos, 0);
    EXPECT_GT(toolsRun, 0) << "ninguna herramienta llegó a medir sobre el corpus real";
    EXPECT_EQ(suspicious, 0);
}

// Una herramienta propuesta sobre una foto real tiene que medir LA PIEZA, no el
// ruido: si se ejecuta dos veces sobre la misma imagen da lo mismo, y si se
// ejecuta sobre una versión de la foto con MÁS compresión JPEG, el número tiene
// que moverse poco. Un valor que baila con el ruido de compresión no es una
// cota, es una casualidad.
TEST(RealPhotoTools, MeasurementsSurviveHeavierJpegCompression) {
    const cv::Mat photo = loadReal("bola_oscura_sobre_claro_10mm.jpg");
    REQUIRE_CORPUS(photo);

    pci::vision::PipelineConfig config;
    config.roi = cv::Rect(560, 720, 420, 420);

    const auto measureOn = [&config](const cv::Mat& image) {
        const auto analysis = pci::vision::analyzeFrame(image, config);
        if (!analysis.isOk()) {
            return -1.0;
        }
        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        const cv::Mat mask =
            pci::vision::pieceMaskWithHoles(gray, analysis.value().mask, config.segmentation);
        return pci::vision::classifyShape(analysis.value().contour.points, mask).outerDiameter;
    };

    const double clean = measureOn(photo);
    ASSERT_GT(clean, 0.0);

    // Se recomprime al 40 % de calidad, que es lo que hace una cámara barata o
    // una imagen que ha pasado por WhatsApp.
    std::vector<unsigned char> buffer;
    ASSERT_TRUE(cv::imencode(".jpg", photo, buffer, {cv::IMWRITE_JPEG_QUALITY, 40}));
    const cv::Mat degraded = cv::imdecode(buffer, cv::IMREAD_COLOR);
    ASSERT_FALSE(degraded.empty());
    const double noisy = measureOn(degraded);
    ASSERT_GT(noisy, 0.0) << "con más compresión ya no encuentra la pieza";

    const double drift = std::abs(noisy - clean) / clean;
    std::printf("  [real] Ø %.1f px original -> %.1f px con JPEG al 40 %% (%.2f %% de deriva)\n",
                clean, noisy, 100.0 * drift);
    EXPECT_LT(drift, 0.02)
        << "el diámetro se mueve con el ruido de compresión: eso no es una cota, es "
           "una casualidad";
}
