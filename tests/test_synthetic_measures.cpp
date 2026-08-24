// MEDIR PIEZAS CUYA MEDIDA SE CONOCE DE ANTEMANO.
//
// `test_calibration_images.cpp` ya hace la ida y vuelta con discos y hexágonos.
// Esto la extiende a las tres formas que rompen el pipeline por sitios que esas
// dos no tocan, y que son justo las que el usuario tiene en su carpeta de
// pruebas:
//
//  · TABLERO acotado — el caso literal del asistente de escala: conoces el lado
//    del tablero entero y de ahí sale todo lo demás.
//  · ENGRANAJE — tiene un AGUJERO (¿el área lo resta o lo rellena?) y DIENTES
//    (el caso extremo del dentado del contorno).
//  · TORNILLOS — tres piezas de distinta longitud en fila: recuento, orden de
//    lectura y una cota que las distingue.
//
// La diferencia con probar sobre las fotos reales es la que decide si una
// prueba sirve: en una foto no se sabe cuánto mide el agujero de verdad, así
// que como mucho se puede comprobar que dos ejecuciones coinciden. Aquí la cota
// va primero y la imagen se dibuja a partir de ella, así que se puede afirmar
// que el número es EL BUENO y no solo que es repetible.

#include <gtest/gtest.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <vector>

#include "domain/calibration.h"
#include "synthetic_scenes.h"
#include "vision/contour_analysis.h"
#include "vision/quality_metrics.h"
#include "vision/pipeline.h"

using namespace pci;
using namespace pci::testing_support;

namespace {

// El contorno de la pieza mayor de una escena, con la configuración por
// defecto: lo mismo que hace la aplicación cuando no se toca nada.
std::vector<vision::PieceAnalysis> piecesOf(const cv::Mat& gray,
                                            vision::PipelineConfig config = {}) {
    auto all = vision::analyzeFrames(gray, config);
    return all.isOk() ? all.value() : std::vector<vision::PieceAnalysis>{};
}

}  // namespace

// ---------------------------------------------------------------------------
// Tablero: la escala sale de una cota conocida y tiene que volver
// ---------------------------------------------------------------------------

TEST(SyntheticMeasures, ABoardOfKnownSizeCalibratesToTheScaleItWasDrawnWith) {
    // Un tablero de 8x8 casillas de 60 px dibujado a 500 mm de lado: es el
    // tablero de silicona de la foto real, con su «50 CM» de cota.
    const BoardScene board = chessboard();
    ASSERT_NEAR(board.playingWidthMm(), 500.0, 1e-9)
        << "la escena no está construida a 500 mm: la prueba mediría contra otra cosa";

    // El operador marca los dos extremos del tablero y escribe cuánto mide.
    // Eso es exactamente `calibrationFromKnownLength`.
    const auto calibration = domain::calibrationFromKnownLength(
        board.playingWidthPx(), board.playingWidthMm(), 60.0, board.gray.cols);
    ASSERT_TRUE(calibration.valid()) << "marcar el tablero entero no calibra";

    std::printf("  [tablero] %d casillas de %d px | dibujado a %.6f mm/px | "
                "calibrado a %.6f mm/px\n",
                board.squares, board.squarePx, board.mmPerPixel, calibration.mmPerPixel);

    // La escala que deduce el programa tiene que ser la que se usó para dibujar.
    // No «parecida»: es una división exacta, y cualquier desvío señala que la
    // calibración está metiendo un factor que no debería.
    EXPECT_NEAR(calibration.mmPerPixel, board.mmPerPixel, 1e-9)
        << "la escala calibrada no es la real";

    // Y de ahí sale la casilla: 500 / 8 = 62,5 mm. Es la comprobación que le
    // importa al operador, porque es el número que va a leer en pantalla.
    const double squareMm = calibration.toMm(board.squarePx);
    std::printf("  [tablero] casilla: %.4f mm (esperado %.4f)\n", squareMm,
                board.squareMm());
    EXPECT_NEAR(squareMm, 62.5, 1e-6) << "la casilla no mide los 62,5 mm que mide";
}

TEST(SyntheticMeasures, TheBoardCornersAreFoundWhereTheyWereDrawn) {
    // Si `findChessboardCorners` encuentra la rejilla, la calibración de lente
    // puede usar esta clase de imagen. Y si las esquinas caen donde se
    // dibujaron, el detector no está metiendo un sesgo propio.
    const BoardScene board = chessboard();
    std::vector<cv::Point2f> corners;
    const bool found = cv::findChessboardCorners(
        board.gray, cv::Size(board.innerCorners, board.innerCorners), corners,
        cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
    ASSERT_TRUE(found) << "no se encuentra la rejilla en un tablero perfecto";
    ASSERT_EQ(corners.size(),
              static_cast<std::size_t>(board.innerCorners * board.innerCorners));

    // El paso entre esquinas contiguas de la misma fila es el lado de casilla.
    std::vector<double> steps;
    for (int row = 0; row < board.innerCorners; ++row) {
        for (int col = 1; col < board.innerCorners; ++col) {
            const auto& a = corners[static_cast<std::size_t>(row * board.innerCorners + col - 1)];
            const auto& b = corners[static_cast<std::size_t>(row * board.innerCorners + col)];
            steps.push_back(cv::norm(b - a));
        }
    }
    const double mean =
        std::accumulate(steps.begin(), steps.end(), 0.0) / static_cast<double>(steps.size());
    double worst = 0.0;
    for (double step : steps) {
        worst = std::max(worst, std::abs(step - mean));
    }
    std::printf("  [tablero] paso entre esquinas: %.3f px (dibujado %d), peor desvío %.3f px\n",
                mean, board.squarePx, worst);

    EXPECT_NEAR(mean, board.squarePx, 0.5)
        << "el paso medido no es el lado de casilla que se dibujó";
    // Y todas las casillas iguales: un tablero plano no puede dar pasos que
    // varíen, y si varían es que el detector está deformando la rejilla.
    EXPECT_LT(worst, 1.0) << "las casillas no salen todas del mismo tamaño";
}

// ---------------------------------------------------------------------------
// Engranaje: el agujero y los dientes
// ---------------------------------------------------------------------------

TEST(SyntheticMeasures, TheGearAreaSubtractsItsBoreInsteadOfFillingIt) {
    const GearScene scene = gear();
    const auto pieces = piecesOf(scene.gray);
    ASSERT_EQ(pieces.size(), 1U) << "el engranaje no sale como una sola pieza";

    const double measured = pieces.front().contour.area;
    const double filled = scene.filledBodyAreaPx2();
    const double hollow = scene.bodyAreaPx2();

    std::printf("  [engranaje] área medida %.0f px² | relleno %.0f | con agujero %.0f | "
                "el agujero son %.0f px² (%.1f %% del cuerpo)\n",
                measured, filled, hollow, scene.boreAreaPx2(),
                100.0 * scene.boreAreaPx2() / filled);

    // El contorno EXTERIOR encierra el agujero, así que su área es la rellena.
    // Esto no es un fallo: es lo que significa `contour.area`, y hay que
    // saberlo para no restarle un agujero dos veces o ninguna.
    //
    // Se comprueba de qué lado cae, porque las dos cifras se diferencian en un
    // 6,8 % y confundirlas es un error del tamaño del agujero — en una cota con
    // tolerancia del 5 % eso es la diferencia entre OK y NG.
    const double toFilled = std::abs(measured - filled) / filled;
    const double toHollow = std::abs(measured - hollow) / hollow;
    EXPECT_LT(toFilled, toHollow)
        << "el área del contorno ya no es la del contorno relleno: si ahora resta el "
           "agujero, todo lo que compare áreas con umbrales viejos cambia de veredicto";

    // Los dientes añaden área por fuera del cuerpo, así que la medida tiene que
    // quedar POR ENCIMA del círculo liso. Si quedara por debajo, el contorno se
    // los estaría comiendo.
    EXPECT_GT(measured, filled) << "el contorno no recoge los dientes";
    // Pero no mucho: son 28 dientes finos, no una corona maciza. Si el área
    // llegara al círculo exterior completo, el contorno estaría uniendo las
    // puntas y perdiendo los huecos.
    const double outerDisc =
        CV_PI * std::pow(scene.bodyRadiusPx + scene.toothHeightPx, 2.0);
    EXPECT_LT(measured, outerDisc)
        << "el contorno une las puntas de los dientes: se pierden los huecos";
}

TEST(SyntheticMeasures, TheTeethShowUpAsRaggednessAndAHexagonDoesNot) {
    // El dentado del contorno es lo que distingue una pieza con dientes de una
    // lisa, y es la única señal que tiene el programa para avisar de que un
    // contorno «no es una silueta limpia». Aquí se comprueba que separa de
    // verdad los dos casos, y no que devuelve un número cualquiera.
    const GearScene toothed = gear();
    const auto gearPieces = piecesOf(toothed.gray);
    ASSERT_EQ(gearPieces.size(), 1U);

    // Un hexágono del mismo tamaño, que es la silueta de una tuerca.
    cv::Mat hex(400, 400, CV_8UC1, cv::Scalar(kSceneBackground));
    std::vector<cv::Point> corners;
    for (int i = 0; i < 6; ++i) {
        const double angle = CV_PI / 6.0 + i * CV_PI / 3.0;
        corners.emplace_back(static_cast<int>(std::lround(200 + 140 * std::cos(angle))),
                             static_cast<int>(std::lround(200 + 140 * std::sin(angle))));
    }
    cv::fillConvexPoly(hex, corners, cv::Scalar(kScenePiece), cv::LINE_8);
    const auto hexPieces = piecesOf(hex);
    ASSERT_EQ(hexPieces.size(), 1U);

    const double gearRagged = vision::contourRaggedness(gearPieces.front().contour.area,
                                                        gearPieces.front().contour.perimeter);
    const double hexRagged = vision::contourRaggedness(hexPieces.front().contour.area,
                                                       hexPieces.front().contour.perimeter);
    std::printf("  [dentado] engranaje %.3f | hexágono %.3f | %.1fx más\n", gearRagged,
                hexRagged, hexRagged > 0.0 ? gearRagged / hexRagged : 0.0);

    EXPECT_GT(gearRagged, hexRagged * 1.5)
        << "el dentado no distingue un engranaje de una tuerca: con este número no se "
           "puede avisar de un contorno sucio sin avisar también de las buenas";
}

// ---------------------------------------------------------------------------
// Tornillos: recuento, orden de lectura y una cota que los separa
// ---------------------------------------------------------------------------

TEST(SyntheticMeasures, ThreeScrewsAreCountedAndNumberedLeftToRight) {
    const ScrewsScene scene = screws();
    const auto pieces = piecesOf(scene.gray);
    ASSERT_EQ(pieces.size(), 3U) << "no salen tres tornillos";

    // El orden de lectura tiene que ser el que ve una persona: de izquierda a
    // derecha. Es el orden con el que se numeran en el mosaico, en el selector
    // y en el informe, así que si aquí sale otro, esos tres numeran mal.
    std::vector<double> centresX;
    for (const auto& piece : pieces) {
        centresX.push_back(piece.contour.centroid.x);
    }
    std::printf("  [tornillos] centros en x:");
    for (double x : centresX) {
        std::printf(" %.0f", x);
    }
    std::printf("\n");
    EXPECT_TRUE(std::is_sorted(centresX.begin(), centresX.end()))
        << "los tornillos no salen numerados de izquierda a derecha";
}

TEST(SyntheticMeasures, EachScrewMeasuresItsOwnLength) {
    const ScrewsScene scene = screws();
    const auto pieces = piecesOf(scene.gray);
    ASSERT_EQ(pieces.size(), 3U);

    // La longitud es la altura de la caja del contorno. Se compara con la que
    // se dibujó, tornillo por tornillo: que el recuento salga bien no significa
    // que cada pieza sea la que se cree.
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const cv::Rect box = cv::boundingRect(pieces[i].contour.points);
        const double expected = scene.lengthsPx[i];
        std::printf("  [tornillos] %zu) alto %d px (dibujado %.0f)  ancho %d px\n", i + 1,
                    box.height, expected, box.width);
        EXPECT_NEAR(box.height, expected, 3.0)
            << "el tornillo " << (i + 1) << " no mide lo que se dibujó";
        // Y el ancho es el de la cabeza, igual en los tres: si variara, el
        // contorno estaría cortando la cabeza de alguno.
        EXPECT_NEAR(box.width, scene.headWidthPx, 3.0)
            << "la cabeza del tornillo " << (i + 1) << " no sale entera";
    }
}

TEST(SyntheticMeasures, WithAScaleTheScrewLengthsComeBackInMillimetres) {
    // La cadena entera: se calibra con una cota conocida y se leen las
    // longitudes en milímetros. Es lo que hace el operador y es donde un factor
    // de escala mal aplicado sale a la luz — en píxeles todo cuadra igual.
    const ScrewsScene scene = screws();
    const auto pieces = piecesOf(scene.gray);
    ASSERT_EQ(pieces.size(), 3U);

    // El tornillo más largo mide 560 px y suponemos que son 80 mm reales.
    constexpr double kLongestMm = 80.0;
    const cv::Rect longest = cv::boundingRect(pieces.back().contour.points);
    const auto calibration = domain::calibrationFromKnownLength(longest.height, kLongestMm,
                                                                60.0, scene.gray.cols);
    ASSERT_TRUE(calibration.valid());

    // Con esa escala, los otros dos tienen que salir en la misma proporción en
    // que se dibujaron. 320/560 y 440/560 de 80 mm.
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const cv::Rect box = cv::boundingRect(pieces[i].contour.points);
        const double mm = calibration.toMm(box.height);
        const double expected = kLongestMm * scene.lengthsPx[i] / scene.lengthsPx.back();
        std::printf("  [tornillos] %zu) %.2f mm (esperado %.2f)\n", i + 1, mm, expected);
        EXPECT_NEAR(mm, expected, 0.6)
            << "el tornillo " << (i + 1) << " no vuelve en milímetros";
    }
}

// EL AFINADO SUBPÍXEL NO PUEDE IGNORARSE CUANDO HAY VARIAS PIEZAS.
//
// El afinado vivía solo en `analyzeFrame`, el camino de UNA pieza.
// `analyzeFrames` —el de varias— pasa por `analyzePiece`, que no lo hacía, así
// que el ajuste se ignoraba en silencio en cuanto había más de una pieza.
//
// Lo grave no es que faltara: es que ese ajuste abre un diálogo avisando de que
// «las medidas de la pieza cambian a partir de ahora» y pidiendo revisar las
// tolerancias. El operador revisa sus tolerancias contra un cambio que en su
// bandeja no se ha producido.
//
// Y desde que el modo automático mide TODAS las piezas, ese camino es el
// normal, así que el hueco pasó de raro a habitual.
TEST(SyntheticMeasures, SubpixelRefinementReachesEveryPieceAndNotJustTheFirst) {
    const ScrewsScene scene = screws();

    vision::PipelineConfig plain;
    vision::PipelineConfig refined;
    refined.subpixelEdges = true;

    const auto without = piecesOf(scene.gray, plain);
    const auto with = piecesOf(scene.gray, refined);
    ASSERT_EQ(without.size(), 3U);
    ASSERT_EQ(with.size(), 3U);

    for (std::size_t i = 0; i < with.size(); ++i) {
        std::printf("  [subpixel] pieza %zu: sin afinar %zu puntos, área %.1f | "
                    "afinada %zu puntos, área %.1f\n",
                    i + 1, without[i].contour.points.size(), without[i].contour.area,
                    with[i].contour.subpixel.size(), with[i].contour.area);
        // TODAS las piezas, no solo la primera: el fallo era exactamente que el
        // camino multipieza no afinaba ninguna.
        EXPECT_FALSE(with[i].contour.subpixel.empty())
            << "la pieza " << (i + 1)
            << " no lleva contorno afinado: el ajuste se ignora con varias piezas";
    }
    // Y sin pedirlo, no se afina nada: encenderlo tiene que ser una decisión.
    for (const auto& piece : without) {
        EXPECT_TRUE(piece.contour.subpixel.empty())
            << "se afina sin haberlo pedido";
    }
}
