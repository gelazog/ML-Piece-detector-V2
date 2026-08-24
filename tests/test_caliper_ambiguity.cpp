// LA MISMA COTA DANDO 22,61 Y 227,81 PX, LAS DOS MARCADAS OK.
//
// El calibre elige entre pares de bordes de polaridad opuesta por su FUERZA,
// sin mirar dónde están. Cuando la línea cruza dos rasgos —la silueta de la
// pieza y un taladro dentro— hay dos pares válidos, y gana el que tenga el
// borde débil más fuerte. Si los dos andan parejos, cuál gana lo decide el
// ruido.
//
// Medido sobre una tuerca real, desplazando la imagen fracciones de píxel, que
// es lo que hace cualquier cámara por vibración o deriva térmica:
//
//     0,00 px -> 22,61      0,25 px -> 227,81
//     0,50 px -> 22,65      0,75 px -> 227,78
//
// No es deriva: es un biestable que salta un FACTOR DIEZ con cuarto de píxel, y
// las cuatro lecturas salían con `ok = true`. En producción esa cota alternaría
// entre dos valores en fotogramas consecutivos, y el operador vería una pieza
// buena dar NG cada dos ciclos sin nada que lo explique.
//
// La herramienta no puede saber cuál de los dos quería el operador: la silueta
// y el taladro son cotas legítimas y sólo él lo sabe. Pero SÍ puede saber que su
// lectura no se sostiene, y eso es lo que se comprueba aquí.
//
// LA INESTABILIDAD SE MIDE, NO SE INFIERE. La primera versión la deducía —«si
// dos pares de bordes puntean parecido, esto es inestable»— y medido sobre las
// fotos reales rechazaba el 53 % de lo que antes se aceptaba: 61 falsas alarmas
// de 136, y encima se le escapaban 3 lecturas que SÍ saltaban. Peor en las dos
// direcciones.
//
// Repetir la medida con la línea corrida un tercio de píxel y mirar si salta
// marca 14 de 136 (el 10 %), y son exactamente las que de verdad saltarían.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cstdio>

#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"
#include <filesystem>
#include "vision/pipeline.h"
#include "synthetic_scenes.h"

using namespace pci;
using namespace pci::testing_support;

namespace {

// Un calibre que cruza el ancho completo de una escena, centrado.
inspection::ToolConfig acrossTheMiddle(float halfLength) {
    inspection::ToolConfig config;
    config.type = inspection::ToolType::Caliper;
    config.name = "ancho";
    config.geometryJson = inspection::toJson(inspection::ToolGeometry(
        inspection::CaliperGeometry{{-halfLength, 0.0F}, {halfLength, 0.0F}, 12.0F}));
    return config;
}

inspection::ToolRunResult measure(const cv::Mat& gray, const cv::Point2f& centre,
                                  float halfLength) {
    const vision::Fixture fixture{{centre.x, centre.y}, 0.0};
    const auto results =
        inspection::runTools(gray, fixture, {acrossTheMiddle(halfLength)}, 0.0,
                             inspection::LengthUnit::Pixels);
    return results.empty() ? inspection::ToolRunResult{} : results.front();
}

}  // namespace

TEST(CaliperAmbiguity, ASingleFeatureIsMeasuredAndAcceptedAsBefore) {
    // LA MITAD QUE MÁS IMPORTA: que el aviso no salte con lo que ya funcionaba.
    // Una barra sola, un solo par de bordes, ninguna ambigüedad posible. Si
    // esto empezara a dar «ambiguo», el aviso sería inservible y se apagaría.
    cv::Mat scene(300, 500, CV_8UC1, cv::Scalar(kSceneBackground));
    cv::rectangle(scene, cv::Rect(180, 80, 140, 140), cv::Scalar(kScenePiece), cv::FILLED,
                  cv::LINE_8);

    const auto result = measure(scene, {250.0F, 150.0F}, 200.0F);
    std::printf("  [calibre] barra sola: %.2f px | %s | %s\n", result.measured,
                result.ok ? "OK" : "no", result.detail.c_str());
    EXPECT_NEAR(result.measured, 140.0, 3.0) << "no mide el ancho de la barra";
    EXPECT_TRUE(result.ok)
        << "avisa de ambigüedad con un solo rasgo: el aviso sería inservible. "
        << result.detail;
}

// LA MITAD «que el aviso SÍ salte» VA SOBRE LA FOTO REAL, y no sobre una figura
// dibujada. Por la misma razón que en la comprobación de umbral: una escena de
// rellenos planos NO puede reproducir el fallo.
//
// Se intentó: una barra con un hueco dentro, cuatro bordes, dos pares válidos.
// Con el criterio deducido saltaba; con el medido no, **y con razón** — en esa
// imagen la lectura da 85,00 px y no se mueve al correr la línea, porque los
// bordes son escalones perfectos. No hay nada inestable que detectar.
//
// La inestabilidad necesita el continuo de grises que tiene una foto y no un
// `fillRect`: es justo lo que hace que el ganador cambie con medio píxel.
// Forzarla en una figura dibujada sería ajustar la escena hasta que la prueba
// pase, que es fabricar la respuesta.

TEST(CaliperAmbiguity, TheRealNutThatFlippedIsCaught) {
    const std::filesystem::path dir("C:/Users/furro/Pictures/IMG-MC");
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }
    const cv::Mat gray =
        cv::imread((dir / "Producto_Tuerca_Liv_02.jpg").string(), cv::IMREAD_GRAYSCALE);
    if (gray.empty()) {
        GTEST_SKIP() << "no se pudo leer la tuerca";
    }
    auto all = vision::analyzeFrames(gray, {});
    ASSERT_TRUE(all.isOk());
    ASSERT_FALSE(all.value().empty());
    const auto& piece = all.value().front();
    const cv::Rect box = cv::boundingRect(piece.contour.points);

    // La colocación exacta que destapó el biestable: una línea que cruza la
    // tuerca por su centro, y por tanto corta las caras exteriores Y el taladro.
    const float half = static_cast<float>(box.width) * 0.6F;
    inspection::ToolConfig config;
    config.type = inspection::ToolType::Caliper;
    config.name = "ancho";
    config.geometryJson = inspection::toJson(inspection::ToolGeometry(
        inspection::CaliperGeometry{{-half, 0.0F}, {half, 0.0F}, 12.0F}));

    const auto results = inspection::runTools(gray, piece.fixture, {config}, 0.0,
                                              inspection::LengthUnit::Pixels);
    ASSERT_FALSE(results.empty());
    std::printf("  [calibre] tuerca real: %.2f px | %s\n",
                results.front().measured, results.front().detail.c_str());

    EXPECT_FALSE(results.front().ok)
        << "da por buena la lectura que salta entre 22,61 y 227,81 px con cuarto de "
           "píxel de desplazamiento";
    EXPECT_NE(results.front().detail.find("INESTABLE"), std::string::npos)
        << "la rechaza sin decir por qué: " << results.front().detail;
    // Y dice ENTRE QUÉ VALORES oscila, que es lo que permite reconocer los dos
    // rasgos y decidir cuál se quería.
    EXPECT_NE(results.front().detail.find(" y "), std::string::npos)
        << "no dice el rango: " << results.front().detail;
}

TEST(CaliperAmbiguity, StableReadingsAreStillAccepted) {
    // El contrapeso de la prueba de arriba, y la que decide si la herramienta
    // sigue siendo usable: medido sobre las fotos reales con 136 colocaciones,
    // el criterio rechaza el 10 %. El criterio deducido que había antes
    // rechazaba el 53 %, y un aviso que salta una de cada dos veces se apaga.
    //
    // Una barra sola no puede ser inestable: un solo par de bordes.
    cv::Mat scene(300, 500, CV_8UC1, cv::Scalar(kSceneBackground));
    cv::rectangle(scene, cv::Rect(180, 80, 140, 140), cv::Scalar(kScenePiece), cv::FILLED,
                  cv::LINE_8);
    for (float y : {-40.0F, -20.0F, 0.0F, 20.0F, 40.0F}) {
        const vision::Fixture fixture{{250.0F, 150.0F}, 0.0};
        inspection::ToolConfig config;
        config.type = inspection::ToolType::Caliper;
        config.name = "ancho";
        config.geometryJson = inspection::toJson(inspection::ToolGeometry(
            inspection::CaliperGeometry{{-200.0F, y}, {200.0F, y}, 10.0F}));
        const auto r = inspection::runTools(scene, fixture, {config}, 0.0,
                                            inspection::LengthUnit::Pixels);
        ASSERT_FALSE(r.empty());
        EXPECT_TRUE(r.front().ok)
            << "avisa a la altura " << y << " sobre una barra sola: " << r.front().detail;
        EXPECT_NEAR(r.front().measured, 140.0, 3.0);
    }
}
