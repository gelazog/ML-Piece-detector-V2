// EL FALLO QUE NO FALLA: MEDIR CORTO Y NO DECIR NADA.
//
// Es el peor que puede tener una aplicación de medida. No lanza, no avisa, y
// devuelve un número creíble. Medido sobre las fotos reales del usuario: el
// umbral automático se come la cabeza cromada de un tornillo y le quita el 36 %
// del área. El aviso de contorno sucio no salta —salta a 3,0 y esa pieza mide
// 2,14— porque un contorno recortado es perfectamente limpio. No hay nada
// sucio: hay pieza que falta.
//
// La señal es aflojar el umbral hacia el fondo y mirar cuánta pieza aparece.
// Lo que la hace utilizable en producción es que NO NECESITA LA VERDAD: no se
// compara con el área buena, que nadie conoce; se compara la imagen consigo
// misma.
//
// Aquí se comprueban las dos mitades que tiene que cumplir cualquier aviso:
// que salte cuando hay que saltar, y —igual de importante— que NO salte cuando
// no. Un aviso que salta con las buenas se desactiva a la semana.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <filesystem>
#include <string>

#include "synthetic_scenes.h"
#include "vision/edge_segmentation.h"

using namespace pci;
using namespace pci::testing_support;

namespace {

std::filesystem::path ownImages() {
    const std::filesystem::path dir("C:/Users/furro/Pictures/IMG-MC");
    std::error_code ec;
    return std::filesystem::exists(dir, ec) ? dir : std::filesystem::path();
}

}  // namespace

TEST(ThresholdClipping, APieceWellSeparatedFromTheTableIsNotFlagged) {
    // Los tornillos dibujados: gris 225 sobre fondo 30. Entre la pieza y la
    // mesa hay casi doscientos niveles de desierto, así que aflojar el umbral
    // doce no puede encontrar nada nuevo.
    const ScrewsScene scene = screws();
    const auto check = vision::checkThresholdClipping(scene.gray);
    std::printf("  [corte] tornillos dibujados: vuelco %+.1f %% | %s\n",
                100.0 * check.swing, check.thresholdCutsThePiece ? "AVISA" : "no avisa");
    EXPECT_FALSE(check.thresholdCutsThePiece)
        << "avisa con una pieza perfectamente separada del fondo: un aviso que salta "
           "con las buenas se desactiva a la semana. " << check.summary;
}

// LA OTRA MITAD —que el aviso SÍ salte cuando hay que saltar— se comprueba
// abajo, sobre las fotos reales, y NO con una escena dibujada. Merece la pena
// explicar por qué, porque se intentó tres veces:
//
//  1. Cabeza plana a 232 sobre mesa a 240, puesta a ojo → vuelco de −100 %. No
//     reproducía un recorte: reproducía una escena rota. Y la aserción que
//     tenía entonces —que el resumen no estuviera vacío— la daba por buena.
//  2. Cabeza plana con el nivel buscado ITERANDO contra el propio Otsu → 0,0 %.
//     Con un histograma de dos picos y nada en medio, Otsu cae SOBRE uno de los
//     picos y no existe ningún nivel que quede «a doce del corte».
//  3. Cabeza con degradado de brillo, que es lo que tiene una superficie curva
//     y pulida → +5,3 %. Ya va en la dirección correcta, pero no llega al 10 %.
//
// Para que llegara habría que agrandar la cabeza hasta que el área recuperada
// pesara lo bastante — y eso ya es **ajustar la escena hasta que la prueba
// pase**, que es fabricar la respuesta. Una prueba así no demuestra que el
// aviso funcione: demuestra que se le encontró una entrada a medida.
//
// Las fotos reales no tienen ese problema: en ellas el recorte está medido de
// forma independiente —32 % y 36 % de área perdida— y el aviso salta o no salta
// contra esa verdad. Es una prueba más débil en portabilidad (se salta en una
// máquina sin esas imágenes) y mucho más fuerte en lo que afirma.

TEST(ThresholdClipping, TheWarningSaysWhatToDoAndNotJustThatSomethingIsWrong) {
    const ScrewsScene scene = screws();
    const auto check = vision::checkThresholdClipping(scene.gray);
    ASSERT_FALSE(check.summary.empty());
    std::printf("  [corte] texto: %s\n", check.summary.c_str());
    // Aunque no avise, el texto tiene que traer el número medido: «no pasa
    // nada» sin cifra no se puede comprobar ni discutir.
    EXPECT_NE(check.summary.find('%'), std::string::npos)
        << "el resumen no dice cuánto se movió: " << check.summary;
    EXPECT_GT(check.loosenedBy, 0) << "no dice cuánto aflojó";
}

TEST(ThresholdClipping, AnEmptyImageDoesNotCrashAndSaysSo) {
    const auto check = vision::checkThresholdClipping(cv::Mat());
    EXPECT_FALSE(check.thresholdCutsThePiece);
    EXPECT_FALSE(check.summary.empty()) << "se calla ante una imagen vacía";
}

// LA VERDAD DE CAMPO ESTÁ EN LAS FOTOS REALES, y por eso esta prueba vale.
//
// Las dos primeras comprueban el comportamiento sobre escenas construidas, que
// es donde se puede razonar. Pero el umbral del 10 % salió de MEDIR estas siete
// imágenes, y si alguien lo mueve sin volver a medirlas, esto lo caza.
TEST(ThresholdClipping, TheMeasuredImagesFallOnTheSideTheyWereMeasuredOn) {
    if (ownImages().empty()) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }
    struct Expected {
        const char* file;
        bool shouldWarn;
        const char* why;
    };
    // De la medición: todo lo correcto por debajo del 6 %, todo lo cortado por
    // encima del 15 %. El umbral está en el 10 %, en medio del hueco.
    const Expected cases[] = {
        {"engranaje-1.png", false, "nivel correcto (+5,7 %)"},
        {"engranajes-1.jpg", false, "nivel correcto; su problema es que se tocan"},
        {"tornillo-1.png", false, "nivel correcto (+2,5 %)"},
        {"Producto_Tuerca_Liv_02.jpg", false, "nivel correcto (+3,9 %)"},
        {"producto-tuercas-prueba.jpg", false, "nivel correcto (+4,6 %)"},
        {"tornillo-2.png", true, "corta la rosca: le falta el 32 % del área"},
        {"tornillos-1.png", true, "corta las cabezas: le falta el 36 % del área"},
    };

    int checked = 0;
    for (const auto& one : cases) {
        const cv::Mat image =
            cv::imread((ownImages() / one.file).string(), cv::IMREAD_GRAYSCALE);
        if (image.empty()) {
            continue;
        }
        ++checked;
        const auto check = vision::checkThresholdClipping(image);
        std::printf("  [corte] %-30s vuelco %+6.1f %% | %-9s | esperado %-9s | %s\n",
                    one.file, 100.0 * check.swing,
                    check.thresholdCutsThePiece ? "AVISA" : "no avisa",
                    one.shouldWarn ? "AVISA" : "no avisa", one.why);
        EXPECT_EQ(check.thresholdCutsThePiece, one.shouldWarn)
            << one.file << ": " << one.why << " — " << check.summary;
    }
    ASSERT_GE(checked, 5) << "no se pudo leer casi ninguna imagen: la comprobación no "
                             "estaría comprobando nada";
}
