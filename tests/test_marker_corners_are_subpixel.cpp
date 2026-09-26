// EL MARCADOR ArUco SALÍA PEQUEÑO Y LA ESCALA mm/px, GRANDE.
//
// `detectMarkerScale` usaba el detector de OpenCV con sus parámetros por
// defecto, y por defecto NO refina las esquinas: entrega los vértices del
// polígono aproximado sobre la imagen umbralizada, que caen hacia dentro del
// cuadrado negro. El marcador mide menos píxeles de los que tiene, y como la
// escala es «lado en mm / lado en px», todas las cotas convertidas con ella
// salían largas. Y siempre del mismo lado: no era ruido que se promedia, era
// un sesgo.
//
// Medido antes de tocar nada, en escenas sintéticas renderizadas con
// superresolución (200 poses con giro y desplazamiento subpíxel, borde
// suavizado, ruido gaussiano σ=6 niveles de gris):
//
//     lado del marcador     sin refinar     CORNER_REFINE_SUBPIX
//          60 px              +1,23 %            +0,50 %
//         120 px              +0,60 %            +0,18 %
//         240 px              +0,30 %            +0,09 %
//     error de esquina        0,70 px          0,18-0,24 px
//
// Y la repetibilidad —la misma escena, cien fotogramas con ruido distinto—:
// con 120 px la desviación típica de la escala bajaba de 0,090 % a 0,022 %;
// con 60 px, de 0,072 % a 0,064 %. Sin refinar, la esquina salta de píxel en
// píxel; refinada, se mueve con el borde.
//
// Coste: nada medible (6,1 ms sin refinar y 5,9 ms refinando, 1280x960).
// CORNER_REFINE_CONTOUR también se probó y era PEOR que no refinar
// (+1,70 % con 60 px), así que no es «cualquier refinado vale».
//
// Las fotos del banco (C:\Users\furro\Pictures\IMG-MC) no llevan marcador —
// ninguna de las 17 lo tiene—, así que la cifra sale de escenas sintéticas con
// la verdad conocida. Y con la imagen de marcador del repositorio
// (`sample_images/aruco_4x4_id0.png`, cuadrado de 600 px exactos): sin refinar
// se leía de 599,0 px; refinando, de 599,9. Esta prueba FALLA sin el refinado:
// los umbrales están entre las dos columnas de la tabla.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <vector>

#include "vision/plane_scale.h"

using namespace pci;

namespace {

constexpr double kMarkerMm = 30.0;

// Marcador de `sidePx` píxeles, girado `angleDeg` y desplazado una fracción de
// píxel, dibujado ocho veces más grande y reducido por área: así el borde es
// un degradado de verdad, como el de una cámara, y la posición de la esquina
// se conoce con exactitud.
cv::Mat markerScene(double sidePx, double angleDeg, double dx, double dy) {
    constexpr int kSuper = 8;
    constexpr int kW = 320;
    constexpr int kH = 240;
    static const cv::aruco::Dictionary dictionary =
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
    cv::Mat marker;
    cv::aruco::generateImageMarker(dictionary, 7, 600, marker, 1);

    const double scale = sidePx / 600.0;
    const double a = angleDeg * CV_PI / 180.0;
    const double cx = kW / 2.0 + dx;
    const double cy = kH / 2.0 + dy;
    // El borde del marcador está en -0,5 y 599,5: los centros de píxel son
    // enteros.
    const std::array<cv::Point2f, 4> src = {
        cv::Point2f(-0.5F, -0.5F), cv::Point2f(599.5F, -0.5F),
        cv::Point2f(599.5F, 599.5F), cv::Point2f(-0.5F, 599.5F)};
    std::array<cv::Point2f, 4> dst{};
    for (std::size_t i = 0; i < 4; ++i) {
        const double u = (src[i].x - 299.5) * scale;
        const double v = (src[i].y - 299.5) * scale;
        const double x = cx + u * std::cos(a) - v * std::sin(a);
        const double y = cy + u * std::sin(a) + v * std::cos(a);
        // Del píxel pequeño al grande: centro de píxel con centro de píxel.
        dst[i] = cv::Point2f(static_cast<float>((x + 0.5) * kSuper - 0.5),
                             static_cast<float>((y + 0.5) * kSuper - 0.5));
    }
    cv::Mat big;
    cv::warpPerspective(marker, big, cv::getPerspectiveTransform(src.data(), dst.data()),
                        cv::Size(kW * kSuper, kH * kSuper), cv::INTER_LINEAR,
                        cv::BORDER_CONSTANT, cv::Scalar(255));
    cv::Mat scene;
    cv::resize(big, scene, cv::Size(kW, kH), 0, 0, cv::INTER_AREA);
    // Contraste de taller: el negro no es 0 ni el blanco 255.
    scene.convertTo(scene, CV_8U, 190.0 / 255.0, 30.0);
    cv::GaussianBlur(scene, scene, cv::Size(0, 0), 0.7);
    return scene;
}

cv::Mat withNoise(const cv::Mat& scene, double sigma, cv::RNG& rng) {
    cv::Mat noisy;
    scene.convertTo(noisy, CV_32F);
    cv::Mat noise(scene.size(), CV_32F);
    rng.fill(noise, cv::RNG::NORMAL, 0.0, sigma);
    noisy += noise;
    cv::Mat out;
    noisy.convertTo(out, CV_8U);
    return out;
}

// Sesgo medio de la escala, en %, sobre poses repartidas.
double meanScaleErrorPercent(double sidePx) {
    const double truth = kMarkerMm / sidePx;
    cv::RNG rng(1234);
    double sum = 0.0;
    int found = 0;
    for (int i = 0; i < 24; ++i) {
        const double angle = 3.7 * i;
        const double dx = 0.13 * (i % 7) - 0.4;
        const double dy = 0.17 * (i % 5) - 0.35;
        const cv::Mat scene = withNoise(markerScene(sidePx, angle, dx, dy), 6.0, rng);
        const auto marker = vision::detectMarkerScale(scene, kMarkerMm);
        if (!marker) {
            continue;
        }
        sum += (marker->mmPerPixel - truth) / truth * 100.0;
        ++found;
    }
    EXPECT_EQ(found, 24) << "el marcador tiene que verse en todas las poses";
    return found > 0 ? sum / found : 1e9;
}

}  // namespace

TEST(MarkerCorners, TheScaleIsNotBiasedByCornersThatFallInside) {
    // Sin refinar: +1,23 % y +0,60 %. Refinando: +0,50 % y +0,18 %.
    const double small = meanScaleErrorPercent(60.0);
    const double medium = meanScaleErrorPercent(120.0);
    EXPECT_LT(std::abs(small), 0.85) << "marcador de 60 px: sesgo " << small << " %";
    EXPECT_LT(std::abs(medium), 0.40) << "marcador de 120 px: sesgo " << medium << " %";
}

TEST(MarkerCorners, TheScaleRepeatsFromFrameToFrame) {
    // La misma escena, cuarenta fotogramas con ruido distinto. Sin refinar la
    // desviación era 0,090 %; refinando, 0,022 %.
    const cv::Mat base = markerScene(120.0, 17.0, 0.3, -0.2);
    cv::RNG rng(99);
    std::vector<double> scales;
    for (int i = 0; i < 40; ++i) {
        const auto marker = vision::detectMarkerScale(withNoise(base, 6.0, rng), kMarkerMm);
        ASSERT_TRUE(marker.has_value());
        scales.push_back(marker->mmPerPixel);
    }
    double mean = 0.0;
    for (const double s : scales) {
        mean += s;
    }
    mean /= static_cast<double>(scales.size());
    double var = 0.0;
    for (const double s : scales) {
        var += (s - mean) * (s - mean);
    }
    const double sdPercent =
        std::sqrt(var / static_cast<double>(scales.size() - 1)) / mean * 100.0;
    EXPECT_LT(sdPercent, 0.045) << "desviación entre fotogramas " << sdPercent << " %";
}

// La única imagen de marcador del repositorio: su cuadrado negro ocupa de la
// columna 80 a la 679, o sea 600 px exactos de borde a borde. Sin refinar, el
// detector lo leía de 599,0 px —las esquinas en los centros de los píxeles del
// borde, medio píxel hacia dentro por cada lado—; refinando, de 599,9.
TEST(MarkerCorners, TheRepositoryMarkerMeasuresItsSixHundredPixels) {
    const auto path = std::filesystem::path(PCI_MODELS_DIR).parent_path() / "sample_images" /
                      "aruco_4x4_id0.png";
    const cv::Mat image = cv::imread(path.string(), cv::IMREAD_GRAYSCALE);
    ASSERT_FALSE(image.empty()) << "falta " << path.string();
    const auto marker = vision::detectMarkerScale(image, kMarkerMm);
    ASSERT_TRUE(marker.has_value());
    const double sidePx = kMarkerMm / marker->mmPerPixel;
    EXPECT_NEAR(sidePx, 600.0, 0.4) << "lado leído " << sidePx << " px";
}
