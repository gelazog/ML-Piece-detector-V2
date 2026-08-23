// EL ORDEN DE LAS PIEZAS, QUE ES SU NOMBRE.
//
// La queja que trae esto es de uso real: «además de tener que tener relación y
// no detectarlas aleatoriamente». Y era literal, por dos motivos que se sumaban:
//
//   1. Se ordenaban por área con un `std::sort`, que NO es estable. Con seis
//      tornillos iguales, el orden relativo lo decidía el algoritmo.
//   2. El área de cada pieza baila unos píxeles de un fotograma al siguiente por
//      culpa del umbral y de la morfología. Así que dos piezas casi iguales se
//      intercambiaban el puesto continuamente.
//
// El resultado era que «pieza 2» era un tornillo distinto en cada inspección, y
// el informe que la nombraba no decía nada. No es un fallo de precisión: es un
// fallo de IDENTIDAD, y esos no se ven en las cifras.
//
// La comprobación central de este fichero no es que el orden sea bonito, es que
// sea EL MISMO al repetir sobre una escena que apenas ha cambiado.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "vision/contour_analysis.h"

using pci::vision::findPieceContours;
using pci::vision::largestPiece;
using pci::vision::orderPiecesForReading;
using pci::vision::PieceContour;

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;

cv::Mat maskWith(const std::vector<cv::Point>& centres, int radius = 40) {
    cv::Mat mask = cv::Mat::zeros(kHeight, kWidth, CV_8UC1);
    for (const auto& centre : centres) {
        cv::circle(mask, centre, radius, cv::Scalar(255), cv::FILLED, cv::LINE_8);
    }
    return mask;
}

// Los centros de las piezas encontradas, en el orden en que salen.
std::string orderOf(const std::vector<PieceContour>& pieces) {
    std::string text;
    for (const auto& piece : pieces) {
        text += "(" + std::to_string(static_cast<int>(piece.centroid.x)) + "," +
                std::to_string(static_cast<int>(piece.centroid.y)) + ") ";
    }
    return text;
}

}  // namespace

// Dos filas de tres: se leen como se lee un texto.
TEST(PieceOrder, SixPiecesComeOutInReadingOrder) {
    // A propósito DESORDENADOS al construir la escena: el orden de salida no
    // puede depender del orden en que se dibujaron.
    const cv::Mat mask = maskWith({{460, 340}, {120, 130}, {320, 340},
                                   {460, 130}, {120, 340}, {320, 130}});
    const auto pieces = findPieceContours(mask);
    ASSERT_EQ(pieces.size(), 6U);
    std::printf("  [orden] %s\n", orderOf(pieces).c_str());

    // Fila de arriba, de izquierda a derecha; después la de abajo.
    const int expected[6][2] = {{120, 130}, {320, 130}, {460, 130},
                                {120, 340}, {320, 340}, {460, 340}};
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        EXPECT_NEAR(pieces[i].centroid.x, expected[i][0], 3.0)
            << "la pieza " << (i + 1) << " no está donde debería en orden de lectura";
        EXPECT_NEAR(pieces[i].centroid.y, expected[i][1], 3.0)
            << "la pieza " << (i + 1) << " no está donde debería en orden de lectura";
    }
}

// LA COMPROBACIÓN QUE IMPORTA: el orden no se mueve porque las áreas bailen.
//
// Se reproduce el fallo original a propósito: seis piezas casi iguales, y en la
// segunda pasada cada una cambia de tamaño un poco, como cambia de verdad entre
// fotogramas por el umbral y la morfología. Con el orden por área, esto bastaba
// para barajarlas.
TEST(PieceOrder, TheOrderSurvivesTheAreasWobblingBetweenFrames) {
    const std::vector<cv::Point> layout = {{120, 130}, {320, 130}, {460, 130},
                                           {120, 340}, {320, 340}, {460, 340}};

    const auto first = findPieceContours(maskWith(layout, 40));
    ASSERT_EQ(first.size(), 6U);

    // Segunda «toma»: cada pieza con un radio ligeramente distinto, de modo que
    // el ranking POR ÁREA queda del revés respecto a la primera.
    cv::Mat wobbled = cv::Mat::zeros(kHeight, kWidth, CV_8UC1);
    const int radii[6] = {43, 38, 41, 39, 42, 40};
    for (std::size_t i = 0; i < layout.size(); ++i) {
        cv::circle(wobbled, layout[i], radii[i], cv::Scalar(255), cv::FILLED, cv::LINE_8);
    }
    const auto second = findPieceContours(wobbled);
    ASSERT_EQ(second.size(), 6U);

    std::printf("  [orden] toma 1: %s\n  [orden] toma 2: %s\n", orderOf(first).c_str(),
                orderOf(second).c_str());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_NEAR(first[i].centroid.x, second[i].centroid.x, 3.0)
            << "la pieza " << (i + 1) << " cambió de sitio en la lista al cambiar las "
            << "áreas: «pieza " << (i + 1) << "» ya no es la misma pieza de una toma a "
            << "la siguiente, y el informe que la nombra no significa nada";
        EXPECT_NEAR(first[i].centroid.y, second[i].centroid.y, 3.0);
    }

    // Y las áreas SÍ cambiaron: si no, esta prueba no estaría probando nada.
    bool anyAreaMoved = false;
    for (std::size_t i = 0; i < first.size(); ++i) {
        if (std::abs(first[i].area - second[i].area) > 100.0) {
            anyAreaMoved = true;
        }
    }
    EXPECT_TRUE(anyAreaMoved) << "el montaje no llegó a mover ninguna área";
}

// Una fila algo torcida sigue siendo una fila. La tolerancia sale de la altura
// mediana de las piezas, no de un número de píxeles fijo.
TEST(PieceOrder, ATiltedRowIsStillOneRow) {
    // Cuatro piezas avanzando en x y bajando un poco en y: una bandeja mal
    // apoyada, o la cámara ligeramente girada.
    const cv::Mat mask = maskWith({{120, 200}, {250, 215}, {380, 230}, {510, 245}}, 35);
    const auto pieces = findPieceContours(mask);
    ASSERT_EQ(pieces.size(), 4U);
    std::printf("  [orden] fila torcida: %s\n", orderOf(pieces).c_str());

    for (std::size_t i = 1; i < pieces.size(); ++i) {
        EXPECT_GT(pieces[i].centroid.x, pieces[i - 1].centroid.x)
            << "la fila inclinada se partió en varias filas y se leyó en zigzag";
    }
}

// Y dos filas de verdad no se funden en una.
TEST(PieceOrder, TwoRealRowsDoNotMergeIntoOne) {
    const cv::Mat mask = maskWith({{120, 120}, {380, 120}, {120, 330}, {380, 330}}, 35);
    const auto pieces = findPieceContours(mask);
    ASSERT_EQ(pieces.size(), 4U);
    std::printf("  [orden] dos filas: %s\n", orderOf(pieces).c_str());

    EXPECT_LT(pieces[0].centroid.y, 200.0F);
    EXPECT_LT(pieces[1].centroid.y, 200.0F);
    EXPECT_GT(pieces[2].centroid.y, 250.0F);
    EXPECT_GT(pieces[3].centroid.y, 250.0F);
    EXPECT_LT(pieces[0].centroid.x, pieces[1].centroid.x);
    EXPECT_LT(pieces[2].centroid.x, pieces[3].centroid.x);
}

// Una columna se lee de arriba abajo, que es lo mismo dicho de otra forma.
TEST(PieceOrder, AColumnReadsTopToBottom) {
    const cv::Mat mask = maskWith({{300, 380}, {300, 100}, {300, 240}}, 45);
    const auto pieces = findPieceContours(mask);
    ASSERT_EQ(pieces.size(), 3U);
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        EXPECT_GT(pieces[i].centroid.y, pieces[i - 1].centroid.y);
    }
}

// La mayor se pide por su nombre, y no es la primera de la lista.
//
// Es lo que impide que el cambio de orden mueva en silencio QUÉ pieza se mide.
TEST(PieceOrder, TheLargestIsAskedForByNameAndIsNotTheFirst) {
    cv::Mat mask = cv::Mat::zeros(kHeight, kWidth, CV_8UC1);
    cv::circle(mask, {120, 120}, 30, cv::Scalar(255), cv::FILLED, cv::LINE_8);  // arriba izq
    cv::circle(mask, {420, 330}, 70, cv::Scalar(255), cv::FILLED, cv::LINE_8);  // la mayor
    const auto pieces = findPieceContours(mask);
    ASSERT_EQ(pieces.size(), 2U);

    // En orden de lectura, la primera es la pequeña de arriba a la izquierda.
    EXPECT_NEAR(pieces.front().centroid.x, 120.0F, 3.0);
    const auto* biggest = largestPiece(pieces);
    ASSERT_NE(biggest, nullptr);
    EXPECT_NEAR(biggest->centroid.x, 420.0F, 3.0)
        << "«la mayor» devolvió otra: quien mida con esto mediría la pieza equivocada";
    std::printf("  [orden] primera %.0f px2, mayor %.0f px2\n", pieces.front().area,
                biggest->area);
    EXPECT_GT(biggest->area, pieces.front().area);

    EXPECT_EQ(largestPiece({}), nullptr);
}

// Ordenar una lista de una o de ninguna no es un caso especial que reviente.
TEST(PieceOrder, OrderingTrivialListsIsSafe) {
    std::vector<PieceContour> none;
    orderPiecesForReading(none);
    EXPECT_TRUE(none.empty());

    std::vector<PieceContour> one(1);
    one.front().centroid = {5.0F, 5.0F};
    orderPiecesForReading(one);
    ASSERT_EQ(one.size(), 1U);
    EXPECT_FLOAT_EQ(one.front().centroid.x, 5.0F);
}
