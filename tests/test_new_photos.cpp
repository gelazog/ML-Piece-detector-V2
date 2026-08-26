// LAS FOTOS NUEVAS: ARANDELAS Y TORNILLOS DE OJO.
//
// El operador las añadió para que se midiera con ellas. Son justo las piezas que
// hacían falta: TODAS tienen exactamente UN agujero, contado mirándolas, y son
// metálicas con reflejos fuertes sobre fondo claro.
//
// Verdad de campo, de mirar las fotos una por una:
//
//   tornillo-ojo-3.png   1 pieza,  1 agujero   (un cáncamo, brillo fuerte)
//   tornillo-ojo-4.png   2 piezas, 1 agujero cada una (se tocan)
//   tornillo-ojo-5.png   5 piezas, 1 agujero cada una (separadas)
//   arandelas-1.png      ~20 arandelas sobre fondo ROJO, con barra de 20 mm
//
// El corpus no tenía ninguna pieza con un agujero de verdad y verificado. Esto
// es lo que faltaba para poder decidir el conteo de agujeros con más de un caso.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "vision/geometry_features.h"
#include "vision/segmentation.h"

using namespace pci;

namespace {

cv::Mat load(const char* file) {
    const std::filesystem::path path =
        std::filesystem::path("C:/Users/furro/Pictures/IMG-MC") / file;
    std::error_code ec;
    return std::filesystem::exists(path, ec)
               ? cv::imread(path.string(), cv::IMREAD_GRAYSCALE)
               : cv::Mat();
}

int countPieces(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    int found = 0;
    for (const auto& contour : contours) {
        if (cv::contourArea(contour) >= 0.001 * static_cast<double>(mask.total())) {
            ++found;
        }
    }
    return found;
}

struct Shot {
    const char* file;
    int pieces;  // -1 = no se fija verdad, solo se publica el número
    const char* what;
};

const std::vector<Shot>& shots() {
    // LAS QUE SE PUEDEN CONTAR, Y LAS QUE NO.
    //
    // Los cáncamos se cuentan de un vistazo: uno, dos, cinco. La foto de las
    // arandelas tiene cerca de veinte piezas de tamaños muy distintos y NO se
    // puede contar con fiabilidad a simple vista — así que no se le fija verdad
    // y solo se publica lo que sale. Poner «20» a ojo y medir contra eso es
    // inventarse la vara, y en este proyecto ya pasó una vez.
    static const std::vector<Shot> all{
        {"tornillo-ojo-3.png", 1, "un cáncamo, brillo fuerte"},
        {"tornillo-ojo-4.png", 2, "dos cáncamos que se tocan"},
        {"tornillo-ojo-5.png", 5, "cinco cáncamos separados"},
        {"arandelas-1.png", -1, "arandelas sobre fondo rojo (+ barra de escala)"},
    };
    return all;
}

}  // namespace

TEST(NewPhotos, HowManyPiecesEachMethodFinds) {
    int looked = 0;
    int errorPlain = 0;
    int errorGlare = 0;
    for (const auto& shot : shots()) {
        const cv::Mat gray = load(shot.file);
        if (gray.empty()) {
            continue;
        }
        ++looked;
        vision::SegmentationOptions plain;
        vision::SegmentationOptions glare;
        glare.recoverHighlightsBy = 12;
        vision::SegmentationOptions split;
        split.recoverHighlightsBy = 12;
        split.splitTouchingPieces = true;

        const auto a = vision::segmentPiece(gray, plain);
        const auto b = vision::segmentPiece(gray, glare);
        const auto c = vision::segmentPiece(gray, split);
        const int byPlain = a.isOk() ? countPieces(a.value()) : -1;
        const int byGlare = b.isOk() ? countPieces(b.value()) : -1;
        const int bySplit = c.isOk() ? countPieces(c.value()) : -1;
        if (shot.pieces >= 0) {
            errorPlain += std::abs(byPlain - shot.pieces);
            errorGlare += std::abs(byGlare - shot.pieces);
        }
        char truth[8];
        std::snprintf(truth, sizeof(truth), shot.pieces >= 0 ? "%2d" : " ?", shot.pieces);
        std::printf("  [nuevas] %-22s verdad %2s   normal %2d   +brillo %2d   +separar %2d   (%s)\n",
                    shot.file, truth, byPlain, byGlare, bySplit, shot.what);
    }
    if (looked == 0) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }
    std::printf("  [nuevas] error de recuento: normal %d, con brillo %d\n", errorPlain,
                errorGlare);
    // Sobre las TRES contables, recuperar el brillo arregla los dos cáncamos que
    // se tocan (3 -> 2) y no toca las otras dos. Es el mismo efecto que ya se
    // midió sobre los tornillos cincados.
    EXPECT_LE(errorGlare, errorPlain)
        << "recuperar el brillo empeora el recuento en las fotos nuevas contables";
}

// EL CONTEO DE AGUJEROS, CON PIEZAS QUE DE VERDAD TIENEN UNO.
//
// Esto es lo que llevaba dos sesiones bloqueado: el único caso verificado era
// una moneda, y elegir un umbral con un caso es fabricar la respuesta. Un
// cáncamo y una arandela tienen exactamente un agujero, se ve mirándolos, y
// ahora hay cuatro fotos de ellos.
TEST(NewPhotos, HowTheHolesOfAPieceWithOneHoleAreCounted) {
    struct Case {
        const char* file;
        const char* what;
    };
    const Case cases[] = {
        {"tornillo-ojo-3.png", "un cáncamo: el ojo"},
        {"tornillo-ojo-5.png", "cinco cáncamos: un ojo cada uno"},
        {"arandelas-1.png", "arandelas: un agujero cada una"},
    };
    int looked = 0;
    for (const auto& one : cases) {
        const cv::Mat gray = load(one.file);
        if (gray.empty()) {
            continue;
        }
        ++looked;
        const auto mask = vision::segmentPiece(gray, {});
        if (!mask.isOk()) {
            continue;
        }
        const auto report = vision::describeContour(mask.value());

        // El reparto de tamaños de los «agujeros» que encuentra: es lo que dice
        // si un mínimo por tamaño los separa o no.
        std::vector<double> sizes;
        for (const auto& hole : report.holes) {
            sizes.push_back(cv::contourArea(hole));
        }
        std::sort(sizes.begin(), sizes.end(), std::greater<double>());
        std::printf("  [agujeros] %-22s la pieza mide %.0f, encuentra %zu\n", one.file,
                    report.area, report.holes.size());
        if (!sizes.empty()) {
            std::printf("             mayores: ");
            for (std::size_t i = 0; i < sizes.size() && i < 5; ++i) {
                std::printf("%.2f%% ", 100.0 * sizes[i] / std::max(report.area, 1.0));
            }
            std::printf("(%s)\n", one.what);
        }
    }
    if (looked == 0) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }
    EXPECT_GT(looked, 1);
}
