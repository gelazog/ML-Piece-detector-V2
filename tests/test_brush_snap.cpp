// CEÑIR LA PINCELADA AL BORDE.
//
// La queja que trae esto es de uso real: «es muy dispareja la línea que da como
// resultado». Y era verdad por construcción. El pincel pintaba una banda de
// ancho constante por donde pasaba la mano, así que el borde corregido salía con
// la forma del PULSO del operador en vez de con la forma de la pieza: ancho
// uniforme, camino tembloroso.
//
// Lo que se comprueba aquí no es que la función haga algo, sino las tres cosas
// que tienen que ser ciertas para que ceñir sea una ayuda y no una ruleta:
//
//   1. Que se quede con la mitad correcta —la del punto donde empezó el trazo—.
//   2. Que el resultado siga el BORDE DE VERDAD y no la banda, que es la razón
//      entera de que esto exista.
//   3. Que cuando NO hay borde que seguir, devuelva la banda entera en vez de
//      inventarse uno. Un pincel que a veces no pinta nada es peor que un pincel
//      tonto: el operador no sabe si falló él o falló el programa.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>

#include "vision/brush_snap.h"

using pci::vision::kMinBandContrast;
using pci::vision::seedIntensity;
using pci::vision::snapBrushBand;

namespace {

// Una escena con un borde vertical nítido en x = 100: oscuro a la izquierda,
// claro a la derecha.
cv::Mat verticalEdge(int width = 200, int height = 200, int at = 100) {
    cv::Mat gray(height, width, CV_8UC1, cv::Scalar(40));
    gray(cv::Rect(at, 0, width - at, height)).setTo(cv::Scalar(210));
    return gray;
}

// La banda que deja una pincelada horizontal que cruza el borde.
cv::Mat horizontalBand(const cv::Size& size, int y, int halfHeight, int fromX, int toX) {
    cv::Mat band = cv::Mat::zeros(size, CV_8UC1);
    band(cv::Rect(fromX, y - halfHeight, toX - fromX, 2 * halfHeight)).setTo(cv::Scalar(255));
    return band;
}

}  // namespace

TEST(BrushSnap, KeepsTheHalfThatLooksLikeWhereTheStrokeStarted) {
    const cv::Mat gray = verticalEdge();
    const cv::Rect area(60, 90, 80, 20);
    const cv::Mat band = horizontalBand(gray.size(), 100, 10, 60, 140);

    // Empezando el trazo en la parte OSCURA (x = 65).
    const double darkSeed = seedIntensity(gray, {65, 100}, 4);
    const auto dark = snapBrushBand(gray, band, area, darkSeed);
    ASSERT_TRUE(dark.snapped) << "no se ciñó a un borde que salta de 40 a 210";
    std::printf("  [ceñir] semilla oscura %.0f -> se queda %d de %d px, contraste %.1f\n",
                darkSeed, dark.keptPixels, dark.bandPixels, dark.contrast);

    // Y lo que se queda está TODO a la izquierda del borde.
    for (int y = 0; y < dark.kept.rows; ++y) {
        for (int x = 0; x < dark.kept.cols; ++x) {
            if (dark.kept.at<unsigned char>(y, x) != 0) {
                ASSERT_LT(area.x + x, 100)
                    << "se quedó con un píxel del lado claro empezando en el oscuro";
            }
        }
    }

    // Empezando en la parte CLARA (x = 135), la decisión se invierte entera.
    const double lightSeed = seedIntensity(gray, {135, 100}, 4);
    const auto light = snapBrushBand(gray, band, area, lightSeed);
    ASSERT_TRUE(light.snapped);
    for (int y = 0; y < light.kept.rows; ++y) {
        for (int x = 0; x < light.kept.cols; ++x) {
            if (light.kept.at<unsigned char>(y, x) != 0) {
                ASSERT_GE(area.x + x, 100)
                    << "se quedó con un píxel del lado oscuro empezando en el claro";
            }
        }
    }

    // Las dos mitades suman la banda: no se pierde ni se duplica ningún píxel.
    EXPECT_EQ(dark.keptPixels + light.keptPixels, dark.bandPixels)
        << "las dos mitades no reparten la banda exactamente";
}

// LA RAZÓN DE QUE ESTO EXISTA: el resultado deja de tener el ancho del pincel y
// pasa a tener la forma del borde.
//
// Con un borde INCLINADO, una banda recta y ceñir apagado dejan un corte recto;
// con ceñir encendido el corte tiene que seguir la diagonal. Se mide la
// distancia de cada píxel que se queda a la recta del borde real.
TEST(BrushSnap, TheResultFollowsTheRealEdgeAndNotTheBrush) {
    // Borde a 30 grados: oscuro debajo de la recta y = 60 + 0,577*x.
    cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(210));
    const double slope = std::tan(30.0 * CV_PI / 180.0);
    for (int y = 0; y < gray.rows; ++y) {
        for (int x = 0; x < gray.cols; ++x) {
            if (y > 60.0 + slope * x) {
                gray.at<unsigned char>(y, x) = 40;
            }
        }
    }

    // Una banda ANCHA y recta que cruza el borde de lado a lado: es justo la
    // pincelada torpe que produce un resultado uniforme.
    const cv::Rect area(40, 60, 120, 90);
    cv::Mat band = cv::Mat::zeros(gray.size(), CV_8UC1);
    band(area).setTo(cv::Scalar(255));

    const double seed = seedIntensity(gray, {60, 140}, 5);  // bien dentro de lo oscuro
    const auto snapped = snapBrushBand(gray, band, area, seed);
    ASSERT_TRUE(snapped.snapped);

    // Cada píxel que se queda tiene que estar del lado oscuro de la recta. Se
    // admite 1,5 px de holgura: la recta pasa por en medio de píxeles enteros.
    double worst = 0.0;
    int wrongSide = 0;
    for (int y = 0; y < snapped.kept.rows; ++y) {
        for (int x = 0; x < snapped.kept.cols; ++x) {
            if (snapped.kept.at<unsigned char>(y, x) == 0) {
                continue;
            }
            const double imgX = area.x + x;
            const double imgY = area.y + y;
            const double over = (60.0 + slope * imgX) - imgY;  // >0 = lado claro
            if (over > 0.0) {
                ++wrongSide;
                worst = std::max(worst, over);
            }
        }
    }
    std::printf("  [ceñir] borde a 30°: %d de %d px, %d del lado equivocado "
                "(el peor a %.1f px)\n",
                snapped.keptPixels, snapped.bandPixels, wrongSide, worst);
    EXPECT_LE(worst, 1.5) << "lo que se queda se sale del lado oscuro: el resultado no "
                             "está siguiendo el borde, está siguiendo la banda";

    // Y comparado con NO ceñir: la banda entera se habría comido media zona
    // clara. La cifra que dice cuánto ha servido.
    const double keptFraction =
        static_cast<double>(snapped.keptPixels) / static_cast<double>(snapped.bandPixels);
    EXPECT_GT(keptFraction, 0.15) << "ceñir se lo llevó casi todo";
    EXPECT_LT(keptFraction, 0.85) << "ceñir no descartó nada: no está haciendo nada";
}

// SIN BORDE NO SE INVENTA UN BORDE.
TEST(BrushSnap, AFlatRegionKeepsTheWholeBand) {
    cv::Mat gray(200, 200, CV_8UC1, cv::Scalar(128));
    // Ruido suave: una sola población, no dos.
    cv::Mat noise(gray.size(), CV_8UC1);
    cv::randn(noise, 0, 3);
    gray += noise;

    const cv::Rect area(50, 50, 60, 30);
    cv::Mat band = cv::Mat::zeros(gray.size(), CV_8UC1);
    band(area).setTo(cv::Scalar(255));

    const auto flat = snapBrushBand(gray, band, area, 128.0);
    std::printf("  [ceñir] zona plana: contraste %.1f (mínimo %.1f) -> ceñido=%s\n",
                flat.contrast, kMinBandContrast, flat.snapped ? "sí" : "no");
    EXPECT_FALSE(flat.snapped) << "partió por la mitad una zona sin borde: eso es "
                                  "inventarse un contorno, y un contorno inventado se "
                                  "mide igual de bien que uno real";
    EXPECT_EQ(flat.keptPixels, flat.bandPixels)
        << "sin borde que seguir, la pincelada tiene que hacer lo de siempre";
    EXPECT_LT(flat.contrast, kMinBandContrast);
}

// Una banda diminuta no da para estimar dos poblaciones.
TEST(BrushSnap, ATinyBandIsLeftAlone) {
    const cv::Mat gray = verticalEdge();
    const cv::Rect area(98, 98, 3, 3);
    cv::Mat band = cv::Mat::zeros(gray.size(), CV_8UC1);
    band(area).setTo(cv::Scalar(255));

    const auto tiny = snapBrushBand(gray, band, area, 40.0);
    EXPECT_FALSE(tiny.snapped);
    EXPECT_EQ(tiny.keptPixels, tiny.bandPixels);
}

// La semilla es un DISCO y no un píxel, y eso es lo que la hace resistir un
// reflejo especular justo donde se hizo clic.
TEST(BrushSnap, TheSeedSurvivesASpeckleUnderTheClick) {
    cv::Mat gray = verticalEdge();
    // Un píxel quemado en plena zona oscura, como el brillo de un reflejo.
    gray.at<unsigned char>(100, 65) = 255;

    const double onePixel = gray.at<unsigned char>(100, 65);
    const double disc = seedIntensity(gray, {65, 100}, 4);
    std::printf("  [ceñir] bajo el clic: un píxel dice %.0f, el disco dice %.0f\n", onePixel,
                disc);
    EXPECT_GT(onePixel, 200.0) << "el montaje no tiene el reflejo que se quería probar";
    EXPECT_LT(disc, 60.0) << "el reflejo bajo el cursor se llevó la semilla, y con ella "
                             "la mitad que se queda";

    const cv::Mat band = horizontalBand(gray.size(), 100, 10, 60, 140);
    const auto result = snapBrushBand(gray, band, cv::Rect(60, 90, 80, 20), disc);
    ASSERT_TRUE(result.snapped);
    // Se quedó con lo oscuro pese al píxel quemado.
    EXPECT_LT(result.keptPixels, result.bandPixels);
}

// Entradas imposibles no revientan y se distinguen de «no pude ceñir»: la
// máscara vacía dice «no tengo nada que decir», y el llamador deja la banda.
TEST(BrushSnap, ImpossibleInputsReturnAnEmptyAnswer) {
    const cv::Mat gray = verticalEdge();
    cv::Mat band = cv::Mat::zeros(gray.size(), CV_8UC1);
    band(cv::Rect(60, 90, 80, 20)).setTo(cv::Scalar(255));

    EXPECT_TRUE(snapBrushBand({}, band, cv::Rect(0, 0, 10, 10), 40.0).kept.empty());
    EXPECT_TRUE(snapBrushBand(gray, {}, cv::Rect(0, 0, 10, 10), 40.0).kept.empty());
    // Banda de otro tamaño: no se puede alinear con la imagen.
    const cv::Mat wrong = cv::Mat::zeros(cv::Size(50, 50), CV_8UC1);
    EXPECT_TRUE(snapBrushBand(gray, wrong, cv::Rect(0, 0, 10, 10), 40.0).kept.empty());
    // Área fuera de la imagen.
    EXPECT_TRUE(snapBrushBand(gray, band, cv::Rect(500, 500, 20, 20), 40.0).kept.empty());
}
