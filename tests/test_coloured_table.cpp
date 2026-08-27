// ¿SABE EL PROGRAMA QUE TU MESA TIENE COLOR?
//
// La clave de color de fondo nace apagada, y con razón: cambia lo que se mide.
// Pero una opción apagada que nadie sabe que existe es una opción que no existe,
// y el operador que la necesita es exactamente el que no va a ir a buscarla —
// está viendo que «no detecta bien» y no tiene por qué sospechar del color de su
// mesa.
//
// Esta sonda mide cuánto COLOR tiene el fondo de cada foto del banco, para ver
// si ese número separa las mesas de color de las blancas con holgura suficiente
// como para poder avisar sin dar la lata en las que no lo necesitan.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "vision/segmentation.h"

using namespace pci;

namespace {

cv::Mat loadColour(const char* file) {
    const std::filesystem::path path =
        std::filesystem::path("C:/Users/furro/Pictures/IMG-MC") / file;
    std::error_code ec;
    return std::filesystem::exists(path, ec) ? cv::imread(path.string(), cv::IMREAD_COLOR)
                                             : cv::Mat();
}

// Cuánto se aparta del gris el color del fondo, de 0 a 1.
//
// Es la saturación de HSV, y se calcula a mano en vez de convertir la imagen
// entera: el color del fondo ya es UN píxel, y convertir un millón para mirar
// uno sería pagar de más en la pestaña de detección.
double howColoured(const cv::Vec3b& bgr) {
    const int high = std::max({bgr[0], bgr[1], bgr[2]});
    const int low = std::min({bgr[0], bgr[1], bgr[2]});
    if (high == 0) {
        return 0.0;
    }
    return static_cast<double>(high - low) / high;
}

struct Shot {
    const char* file;
    bool coloured;  // verdad de campo, mirando la foto
};

}  // namespace

TEST(ColouredTable, TheSaturationOfTheBackgroundSeparatesColouredTablesFromWhiteOnes) {
    const Shot shots[] = {
        {"arandelas-1.png", true},   // cartón rojo
        {"engranaje-1.png", false},  // blanco
        {"tornillo-ojo-3.png", false},
        {"producto-tuercas-prueba.jpg", false},
        {"arandelas-3.jpg", false},
        {"tornillos-1.png", false},
        {"rosca-1.png", false},
    };

    double worstWhite = 0.0;
    double bestColoured = 1.0;
    int looked = 0;
    for (const auto& shot : shots) {
        const cv::Mat bgr = loadColour(shot.file);
        if (bgr.empty()) {
            continue;
        }
        ++looked;
        const cv::Vec3b background = vision::estimateBackgroundColour(bgr);
        const double colour = howColoured(background);
        std::printf("  %-30s fondo BGR(%3d,%3d,%3d)  color=%.3f  %s\n", shot.file,
                    background[0], background[1], background[2], colour,
                    shot.coloured ? "(mesa de color)" : "");
        if (shot.coloured) {
            bestColoured = std::min(bestColoured, colour);
        } else {
            worstWhite = std::max(worstWhite, colour);
        }
    }
    ASSERT_GE(looked, 5) << "no está el banco de fotos";

    std::printf("  [color de mesa] la más gris de las de color: %.3f · la más de color de "
                "las grises: %.3f\n",
                bestColoured, worstWhite);
    EXPECT_GT(bestColoured, worstWhite * 3.0)
        << "la saturación del fondo no separa las mesas de color de las blancas con "
           "holgura, así que no vale para avisar: o se calla donde hace falta o da la "
           "lata donde no.";
}
