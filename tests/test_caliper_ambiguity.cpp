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
// y el taladro son cotas legítimas y sólo él lo sabe. Pero SÍ puede saber que
// su propia elección fue una moneda al aire, y eso es lo que se comprueba aquí.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cstdio>

#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"
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

TEST(CaliperAmbiguity, TwoFeaturesOfSimilarStrengthAreNotSilentlyResolved) {
    // Una barra con un hueco dentro: la línea cruza la silueta Y el hueco, y
    // los cuatro bordes marcan parecido porque el contraste es el mismo.
    //
    // Cuál de los dos pares gane no lo puede decidir la herramienta —las dos
    // cotas son legítimas— pero tampoco puede fingir que lo ha decidido.
    cv::Mat scene(300, 500, CV_8UC1, cv::Scalar(kSceneBackground));
    cv::rectangle(scene, cv::Rect(140, 80, 220, 140), cv::Scalar(kScenePiece), cv::FILLED,
                  cv::LINE_8);
    // El hueco, del color del fondo: mismos saltos de gris que la silueta.
    cv::rectangle(scene, cv::Rect(225, 120, 50, 60), cv::Scalar(kSceneBackground),
                  cv::FILLED, cv::LINE_8);

    const auto result = measure(scene, {250.0F, 150.0F}, 200.0F);
    std::printf("  [calibre] barra con hueco: %.2f px | %s | %s\n", result.measured,
                result.ok ? "OK" : "no", result.detail.c_str());

    // LO QUE HAY QUE EXIGIR ES QUE NO LO DÉ POR BUENO, no un número concreto.
    //
    // La primera versión de esta prueba exigía que midiera 220 (la silueta) o 50
    // (el hueco), y salió 85. No era un fallo del código: con cuatro bordes hay
    // MÁS de dos pares válidos, y 85 es el borde izquierdo de la barra con el
    // izquierdo del hueco — subida y bajada, par legítimo. Mi premisa de «hay
    // dos rasgos» era demasiado simple.
    //
    // Y ahí está el punto: **cuál de todos esos pares gana no lo puede decidir
    // la herramienta**, porque todos son geométricamente válidos y sólo el
    // operador sabe cuál quiso trazar. Lo único exigible es que no finja
    // haberlo decidido.
    EXPECT_FALSE(result.ok)
        << "da por buena una lectura entre varios pares de bordes que puntean "
           "parecido: ese número salta de un rasgo a otro con medio píxel. Midió "
        << result.measured << " px";
    EXPECT_NE(result.detail.find("AMBIGUO"), std::string::npos)
        << "lo rechaza sin decir por qué: " << result.detail;
    // Y el aviso nombra LAS DOS candidatas, que es lo que permite al operador
    // saber cuál quería y acortar la línea en consecuencia.
    EXPECT_NE(result.detail.find("px y "), std::string::npos)
        << "el aviso no dice entre qué dos cotas está dudando: " << result.detail;
}

TEST(CaliperAmbiguity, TheWarningNamesWhatToDoAboutIt) {
    // Un aviso que dice «ambiguo» y nada más deja al operador igual de atascado.
    cv::Mat scene(300, 500, CV_8UC1, cv::Scalar(kSceneBackground));
    cv::rectangle(scene, cv::Rect(140, 80, 220, 140), cv::Scalar(kScenePiece), cv::FILLED,
                  cv::LINE_8);
    cv::rectangle(scene, cv::Rect(225, 120, 50, 60), cv::Scalar(kSceneBackground),
                  cv::FILLED, cv::LINE_8);
    const auto result = measure(scene, {250.0F, 150.0F}, 200.0F);
    if (result.ok) {
        GTEST_SKIP() << "esta escena no resulta ambigua; el texto se comprueba en la "
                        "prueba de la imagen real";
    }
    EXPECT_NE(result.detail.find("Acorta"), std::string::npos)
        << "el aviso no dice qué hacer: " << result.detail;
}
