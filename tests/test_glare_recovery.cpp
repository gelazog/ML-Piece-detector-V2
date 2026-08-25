// RECUPERAR LO QUE EL BRILLO SE LLEVA.
//
// Petición de uso: «tengo una tuerca, con reflejos, brillo, sombras, y eso
// afecta a la medición y la forma en que toma los bordes».
//
// Era literal y estaba medido: tres tornillos cincados salían como CINCO manchas
// y un tornillo galvanizado como DOS, porque el brillo de su propia cara sube
// hasta el nivel del fondo y el corte lo deja fuera.
//
// Se corta dos veces: el corte de siempre da las semillas, un corte aflojado da
// hasta dónde podría llegar, y se conserva lo aflojado que TOQUE una semilla.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <filesystem>
#include <vector>

#include "vision/segmentation.h"

using namespace pci;

namespace {

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

struct Scene { const char* file; int truth; const char* what; };

const std::vector<Scene>& scenes() {
    static const std::vector<Scene> all{
        {"tornillos-1.png", 3, "tres tornillos cincados, brillo fuerte"},
        {"tornillo-2.png", 1, "galvanizado con marca oscura estampada"},
        {"tornillo-1.png", 1, "un tornillo"},
        {"Producto_Tuerca_Liv_02.jpg", 2, "tuercas con reflejo"},
        {"producto-tuercas-prueba.jpg", 100, "bandeja de cien tuercas"},
        {"engranaje-1.png", 1, "un engranaje"},
        {"engranajes-1.jpg", 2, "dos engranajes engranados (se tocan)"},
    };
    return all;
}

cv::Mat load(const char* file) {
    const std::filesystem::path path =
        std::filesystem::path("C:/Users/furro/Pictures/IMG-MC") / file;
    std::error_code ec;
    return std::filesystem::exists(path, ec)
               ? cv::imread(path.string(), cv::IMREAD_GRAYSCALE)
               : cv::Mat();
}

}  // namespace

TEST(GlareRecovery, ItPutsBackThePieceTheGlareTookAndLetsNoBackgroundIn) {
    int looked = 0;
    int errorBefore = 0;
    int errorAfter = 0;
    for (const auto& scene : scenes()) {
        const cv::Mat gray = load(scene.file);
        if (gray.empty()) continue;
        ++looked;
        vision::SegmentationOptions plain;
        vision::SegmentationOptions recovering;
        recovering.recoverHighlightsBy = 12;

        const auto before = vision::segmentPiece(gray, plain);
        const auto after = vision::segmentPiece(gray, recovering);
        ASSERT_TRUE(before.isOk());
        ASSERT_TRUE(after.isOk());
        const int piecesBefore = countPieces(before.value());
        const int piecesAfter = countPieces(after.value());
        errorBefore += std::abs(piecesBefore - scene.truth);
        errorAfter += std::abs(piecesAfter - scene.truth);
        std::printf("  [brillo] %-28s verdad %3d   antes %3d -> con esto %3d   (%s)\n",
                    scene.file, scene.truth, piecesBefore, piecesAfter, scene.what);
    }
    if (looked == 0) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }
    std::printf("  [brillo] error total de recuento: %d -> %d\n", errorBefore, errorAfter);
    EXPECT_LT(errorAfter, errorBefore)
        << "recuperar el brillo no acerca el recuento a la verdad: no sirve";
}

TEST(GlareRecovery, TheThreeScrewsStopBeingFivePieces) {
    const cv::Mat gray = load("tornillos-1.png");
    if (gray.empty()) GTEST_SKIP() << "no está la imagen";
    vision::SegmentationOptions plain;
    vision::SegmentationOptions recovering;
    recovering.recoverHighlightsBy = 12;
    const int before = countPieces(vision::segmentPiece(gray, plain).value());
    const int after = countPieces(vision::segmentPiece(gray, recovering).value());
    std::printf("  [brillo] tres tornillos: %d -> %d\n", before, after);
    EXPECT_GT(before, 3) << "el corte de siempre ya no los parte: este caso dejó de "
                            "probar lo que dice probar";
    EXPECT_EQ(after, 3) << "siguen sin salir tres tornillos";
}

TEST(GlareRecovery, TheTrayOfAHundredNutsDoesNotMeltTogether) {
    // El riesgo de aflojar un umbral es que el fondo entre y funda las piezas.
    // Cien tuercas separadas por huecos estrechos es la escena que lo detecta.
    const cv::Mat gray = load("producto-tuercas-prueba.jpg");
    if (gray.empty()) GTEST_SKIP() << "no está la imagen";
    vision::SegmentationOptions recovering;
    recovering.recoverHighlightsBy = 12;
    const int after = countPieces(vision::segmentPiece(gray, recovering).value());
    std::printf("  [brillo] bandeja de cien: %d\n", after);
    EXPECT_GE(after, 95) << "aflojar el corte ha fundido las tuercas entre sí";
}

TEST(GlareRecovery, OffByDefaultBecauseItChangesWhatIsMeasured) {
    EXPECT_EQ(vision::SegmentationOptions{}.recoverHighlightsBy, 0)
        << "nace encendida: cambiaría las medidas de todo el mundo sin avisar";
}
