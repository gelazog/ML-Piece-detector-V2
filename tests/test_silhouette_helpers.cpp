// LOS AYUDANTES DE SILUETA TIENEN QUE DAR LO MISMO QUE LAS COPIAS QUE SUSTITUYEN.
//
// `vision/silhouette.h` sustituye diez copias de «Otsu con la polaridad de la
// herramienta» (todas en `tool_executor.cpp`) y ocho de «findContours exterior
// y quedarse con el de mayor área» (Filete, Chaflán, Extremos, Perfil,
// Polígono, `proposeTools`, `proposeWithRecipe` y `pieceInsideOutline`). Era
// una limpieza, no un arreglo: ningún número de la aplicación debía moverse.
//
// Por qué hace falta una prueba para algo que «es lo mismo»: las variantes que
// se parecen NO dan lo mismo, y es fácil colar una al unificar. Con un cuadrado
// macizo de 100 × 100 px, CHAIN_APPROX_NONE devuelve 396 puntos y
// CHAIN_APPROX_SIMPLE devuelve 4: el perímetro digital, el Polígono y el Perfil
// cambiarían de número sin que nada fallara. Y con dos figuras de la misma
// área, `std::max_element` se queda con la PRIMERA; un bucle con `>=` se
// quedaría con la última y la herramienta mediría la otra pieza.
//
// Así que aquí se ejecuta, lado a lado, el código de antes copiado literal y el
// ayudante, sobre escenas sintéticas pensadas para los casos raros (empate,
// imagen plana, recorte no contiguo, figura tocando el borde) y sobre las fotos
// reales del banco de imágenes. La exigencia es igualdad EXACTA: los mismos
// píxeles en la máscara y los mismos puntos, en el mismo orden, en el contorno.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "vision/gray.h"
#include "vision/silhouette.h"

using namespace pci;

namespace {

// ---- El código de antes, tal cual estaba en cada sitio ----------------------

cv::Mat oldOtsu(const cv::Mat& gray, bool darkPiece) {
    cv::Mat binary;
    cv::threshold(gray, binary, 0.0, 255.0,
                  (darkPiece ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);
    return binary;
}

// Devuelve vacío si no hay contornos, que es lo que el código de antes
// comprobaba con `contours.empty()` antes de desreferenciar.
std::vector<cv::Point> oldLargest(const cv::Mat& binary) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        return {};
    }
    const auto& outer = *std::max_element(
        contours.begin(), contours.end(), [](const auto& a, const auto& b) {
            return cv::contourArea(a) < cv::contourArea(b);
        });
    return outer;
}

bool sameMask(const cv::Mat& a, const cv::Mat& b) {
    if (a.size() != b.size() || a.type() != b.type()) {
        return false;
    }
    cv::Mat diff;
    cv::compare(a, b, diff, cv::CMP_NE);
    return cv::countNonZero(diff) == 0;
}

// Compara las dos versiones sobre un gris y un recorte, con las dos
// polaridades. Devuelve cuántos contornos no vacíos comparó, para que la
// prueba pueda exigir que se comparó algo de verdad.
int expectSameOn(const cv::Mat& gray, const cv::Rect& bounds, const std::string& what) {
    int compared = 0;
    for (const bool dark : {false, true}) {
        const std::string label = what + (dark ? " [pieza oscura]" : " [pieza clara]");
        const cv::Mat before = oldOtsu(gray(bounds), dark);
        const cv::Mat after = vision::otsuMask(gray(bounds), dark);
        EXPECT_TRUE(sameMask(before, after)) << label;

        const std::vector<cv::Point> oldContour = oldLargest(before);
        const std::vector<cv::Point> newContour = vision::largestOuterContour(after);
        EXPECT_EQ(oldContour, newContour) << label;
        if (!newContour.empty()) {
            ++compared;
        }
    }
    return compared;
}

cv::Rect whole(const cv::Mat& m) { return {0, 0, m.cols, m.rows}; }

}  // namespace

TEST(SilhouetteHelpers, SyntheticScenesGiveExactlyWhatTheOldCodeGave) {
    int compared = 0;

    // Disco oscuro sobre fondo claro, con antialias en el borde.
    cv::Mat disc(300, 400, CV_8UC1, cv::Scalar(220));
    cv::circle(disc, {200, 150}, 90, cv::Scalar(40), cv::FILLED, cv::LINE_AA);
    compared += expectSameOn(disc, whole(disc), "disco");

    // Hexágono claro con un agujero, sobre fondo oscuro y con ruido.
    cv::Mat hexagon(320, 320, CV_8UC1, cv::Scalar(30));
    std::vector<cv::Point> hex;
    for (int i = 0; i < 6; ++i) {
        const double a = CV_PI / 3.0 * i;
        hex.emplace_back(160 + static_cast<int>(110 * std::cos(a)),
                         160 + static_cast<int>(110 * std::sin(a)));
    }
    cv::fillPoly(hexagon, std::vector<std::vector<cv::Point>>{hex}, cv::Scalar(200));
    cv::circle(hexagon, {160, 160}, 30, cv::Scalar(30), cv::FILLED);
    cv::Mat noise(hexagon.size(), CV_8UC1);
    cv::randn(noise, 0, 12);
    cv::add(hexagon, noise, hexagon);
    compared += expectSameOn(hexagon, whole(hexagon), "hexágono con ruido");

    // EMPATE: dos cuadrados de la misma área. Gana el primero que devuelve
    // findContours, y el ayudante tiene que devolver ese mismo.
    cv::Mat twins(200, 400, CV_8UC1, cv::Scalar(230));
    cv::rectangle(twins, cv::Rect(40, 50, 100, 100), cv::Scalar(20), cv::FILLED);
    cv::rectangle(twins, cv::Rect(260, 50, 100, 100), cv::Scalar(20), cv::FILLED);
    compared += expectSameOn(twins, whole(twins), "dos cuadrados iguales");

    // Figura que toca el borde del recorte, y RECORTE NO CONTIGUO: así es como
    // lo llaman las herramientas, con `gray(bounds)`.
    cv::Mat big(400, 500, CV_8UC1, cv::Scalar(210));
    cv::ellipse(big, {250, 200}, {160, 90}, 25.0, 0.0, 360.0, cv::Scalar(60), cv::FILLED);
    compared += expectSameOn(big, cv::Rect(120, 80, 200, 150), "elipse recortada");

    // Imagen plana: Otsu no tiene nada que separar. Lo que salga, que salga igual.
    cv::Mat flat(100, 100, CV_8UC1, cv::Scalar(128));
    expectSameOn(flat, whole(flat), "imagen plana");

    EXPECT_GE(compared, 6) << "las escenas sintéticas deberían dar contorno casi siempre";
}

TEST(SilhouetteHelpers, AnEmptyMaskHasNoContour) {
    const cv::Mat empty = cv::Mat::zeros(50, 50, CV_8UC1);
    EXPECT_TRUE(oldLargest(empty).empty());
    EXPECT_TRUE(vision::largestOuterContour(empty).empty());
}

// Lo que hace que la unificación importe: la variante SIMPLE, que parece la
// misma, da otro contorno. Si alguien «simplifica» el ayudante a SIMPLE, esta
// cifra es la que cambia.
TEST(SilhouetteHelpers, TheHelperKeepsEveryPixelOfTheBorder) {
    cv::Mat mask = cv::Mat::zeros(200, 200, CV_8UC1);
    cv::rectangle(mask, cv::Rect(50, 50, 100, 100), cv::Scalar(255), cv::FILLED);
    EXPECT_EQ(vision::largestOuterContour(mask).size(), 396U);

    std::vector<std::vector<cv::Point>> simple;
    cv::findContours(mask, simple, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    ASSERT_EQ(simple.size(), 1U);
    EXPECT_EQ(simple[0].size(), 4U);
}

TEST(SilhouetteHelpers, RealPhotosGiveExactlyWhatTheOldCodeGave) {
    const std::filesystem::path bank("C:/Users/furro/Pictures/IMG-MC");
    if (!std::filesystem::is_directory(bank)) {
        GTEST_SKIP() << "No está el banco de fotos " << bank.string();
    }
    int photos = 0;
    int compared = 0;
    for (const auto& entry : std::filesystem::directory_iterator(bank)) {
        const std::string ext = entry.path().extension().string();
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") {
            continue;
        }
        const cv::Mat image = cv::imread(entry.path().string(), cv::IMREAD_COLOR);
        if (image.empty()) {
            continue;
        }
        const cv::Mat gray = vision::toGray(image);
        ASSERT_FALSE(gray.empty());
        ++photos;
        const std::string name = entry.path().filename().string();
        compared += expectSameOn(gray, whole(gray), name + " entera");
        // El tercio central, como recorte no contiguo: es la forma en que las
        // herramientas llaman a Otsu.
        const cv::Rect centre(gray.cols / 3, gray.rows / 3, gray.cols / 3, gray.rows / 3);
        compared += expectSameOn(gray, centre, name + " centro");
    }
    if (photos == 0) {
        GTEST_SKIP() << "El banco de fotos está vacío";
    }
    EXPECT_GT(compared, 0);
}
