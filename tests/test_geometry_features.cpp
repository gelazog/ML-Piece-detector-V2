// Pruebas de la descomposición del contorno. Se miden contra siluetas dibujadas
// con dimensiones exactas: lo que se comprueba no es que salgan "algunos"
// tramos, sino que salgan LOS que hay y con las medidas correctas.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "vision/geometry_features.h"

using pci::vision::ContourPrimitive;
using pci::vision::decomposeContour;
using pci::vision::DecomposeOptions;
using pci::vision::findHoles;
using pci::vision::PrimitiveKind;
using pci::vision::resampleClosedContour;

namespace {

// Contorno externo de una máscara binaria (pieza = 255).
std::vector<cv::Point> outerContour(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        return {};
    }
    return *std::max_element(contours.begin(), contours.end(),
                             [](const auto& a, const auto& b) {
                                 return cv::contourArea(a) < cv::contourArea(b);
                             });
}

cv::Mat blank(int size = 500) { return cv::Mat(size, size, CV_8UC1, cv::Scalar(0)); }

// Rectángulo con las cuatro esquinas redondeadas de radio conocido.
cv::Mat roundedRect(cv::Rect box, int radius) {
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(box.x + radius, box.y, box.width - 2 * radius, box.height),
                  cv::Scalar(255), cv::FILLED);
    cv::rectangle(mask, cv::Rect(box.x, box.y + radius, box.width, box.height - 2 * radius),
                  cv::Scalar(255), cv::FILLED);
    for (const auto& c : {cv::Point(box.x + radius, box.y + radius),
                          cv::Point(box.x + box.width - radius, box.y + radius),
                          cv::Point(box.x + radius, box.y + box.height - radius),
                          cv::Point(box.x + box.width - radius, box.y + box.height - radius)}) {
        cv::circle(mask, c, radius, cv::Scalar(255), cv::FILLED);
    }
    return mask;
}

int countOf(const std::vector<ContourPrimitive>& primitives, PrimitiveKind kind,
            double minLength = 0.0) {
    return static_cast<int>(std::count_if(
        primitives.begin(), primitives.end(), [kind, minLength](const ContourPrimitive& p) {
            return p.kind == kind && p.length >= minLength;
        }));
}

}  // namespace

TEST(Resampling, WalksTheWholePerimeterAtAConstantStep) {
    // Si el paso no fuera uniforme, todo lo que se construye encima -residuos,
    // longitudes, elección entre recta y arco- estaría sesgado hacia los tramos
    // con más puntos.
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(100, 100, 200, 160), cv::Scalar(255), cv::FILLED);
    const auto points = resampleClosedContour(outerContour(mask), 4.0);
    ASSERT_GT(points.size(), 100U);

    // El paso es de ARCO, no de cuerda: al doblar una esquina, avanzar 4 px a
    // lo largo del contorno deja los dos puntos a menos de 4 px en línea recta
    // (en un giro de 90°, a 4/√2 = 2,83). Así que se exige que ninguna cuerda
    // se pase de 4 y que solo las esquinas se queden cortas.
    int shortChords = 0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const double chord = cv::norm(points[i] - points[i - 1]);
        EXPECT_LE(chord, 4.0 + 1e-3) << "una cuerda nunca puede pasarse del paso";
        if (chord < 4.0 - 0.05) {
            ++shortChords;
        }
    }
    EXPECT_LE(shortChords, 8) << "solo las cuatro esquinas deberían acortar la cuerda";

    // Y el recorrido total se parece al perímetro real (2*(200+160) = 720).
    EXPECT_NEAR(points.size() * 4.0, 720.0, 40.0);
}

TEST(Resampling, RefusesWhatIsNotAContour) {
    EXPECT_TRUE(resampleClosedContour({}, 2.0).empty());
    EXPECT_TRUE(resampleClosedContour({{0, 0}, {1, 1}}, 2.0).empty());
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(100, 100, 200, 160), cv::Scalar(255), cv::FILLED);
    EXPECT_TRUE(resampleClosedContour(outerContour(mask), 0.0).empty());
    // Paso mayor que la propia figura: no hay nada que recorrer.
    EXPECT_TRUE(resampleClosedContour(outerContour(mask), 10000.0).empty());
}

TEST(ContourDecomposition, ARectangleIsFourStraightSides) {
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(120, 100, 240, 180), cv::Scalar(255), cv::FILLED);
    const auto primitives = decomposeContour(outerContour(mask));

    // Se cuentan solo los tramos con longitud apreciable: en las esquinas
    // pueden salir trocitos de dos o tres puntos que no representan nada.
    const int lines = countOf(primitives, PrimitiveKind::Line, 40.0);
    const int arcs = countOf(primitives, PrimitiveKind::Arc, 40.0);
    std::printf("  rectángulo: %d rectas, %d arcos (de %zu tramos)\n", lines, arcs,
                primitives.size());
    EXPECT_EQ(lines, 4);
    EXPECT_EQ(arcs, 0) << "un rectángulo no tiene arcos";

    // Los lados largos miden 240 y los cortos 180.
    std::vector<double> lengths;
    for (const auto& p : primitives) {
        if (p.kind == PrimitiveKind::Line && p.length >= 40.0) {
            lengths.push_back(p.length);
        }
    }
    std::sort(lengths.begin(), lengths.end());
    ASSERT_EQ(lengths.size(), 4U);
    EXPECT_NEAR(lengths[0], 180.0, 12.0);
    EXPECT_NEAR(lengths[1], 180.0, 12.0);
    EXPECT_NEAR(lengths[2], 240.0, 12.0);
    EXPECT_NEAR(lengths[3], 240.0, 12.0);
}

TEST(ContourDecomposition, ARoundedRectangleGivesFourSidesAndFourCorners) {
    // El caso que un detector de esquinas se salta: en la unión tangente de la
    // recta con el redondeo NO hay esquina, solo cambia la curvatura.
    constexpr int kRadius = 40;
    const cv::Mat mask = roundedRect(cv::Rect(100, 90, 280, 220), kRadius);
    const auto primitives = decomposeContour(outerContour(mask));

    const int lines = countOf(primitives, PrimitiveKind::Line, 30.0);
    const int arcs = countOf(primitives, PrimitiveKind::Arc, 20.0);
    std::printf("  rect. redondeado: %d rectas, %d arcos (de %zu tramos)\n", lines, arcs,
                primitives.size());
    EXPECT_EQ(lines, 4);
    EXPECT_EQ(arcs, 4);

    // Y los cuatro arcos tienen el radio dibujado.
    for (const auto& p : primitives) {
        if (p.kind == PrimitiveKind::Arc && p.length >= 20.0) {
            EXPECT_NEAR(p.radius, kRadius, kRadius * 0.15)
                << "radio medido " << p.radius;
        }
    }
}

TEST(ContourDecomposition, ADiscIsAllArcAndNoStraightSide) {
    cv::Mat mask = blank();
    cv::circle(mask, {250, 250}, 120, cv::Scalar(255), cv::FILLED);
    const auto primitives = decomposeContour(outerContour(mask));

    const int lines = countOf(primitives, PrimitiveKind::Line, 30.0);
    const int arcs = countOf(primitives, PrimitiveKind::Arc, 30.0);
    std::printf("  disco: %d rectas, %d arcos, R=%.1f\n", lines, arcs,
                primitives.empty() ? 0.0 : primitives.front().radius);
    EXPECT_EQ(lines, 0) << "un disco no tiene lados rectos";
    // Y sale como UN solo arco: el contorno cierra, así que el muñón que deja
    // el barrido al dar la vuelta se funde con el primer tramo.
    EXPECT_EQ(arcs, 1) << "una circunferencia entera es un solo rasgo";
    ASSERT_FALSE(primitives.empty());
    EXPECT_NEAR(primitives.front().radius, 120.0, 1.5);
}

TEST(ContourDecomposition, AnLShapeKeepsItsSixSides) {
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(120, 120, 100, 240), cv::Scalar(255), cv::FILLED);
    cv::rectangle(mask, cv::Rect(120, 260, 240, 100), cv::Scalar(255), cv::FILLED);
    const auto primitives = decomposeContour(outerContour(mask));

    const int lines = countOf(primitives, PrimitiveKind::Line, 40.0);
    std::printf("  pieza en L: %d rectas de %zu tramos\n", lines, primitives.size());
    EXPECT_EQ(lines, 6) << "una L tiene seis lados";
    EXPECT_EQ(countOf(primitives, PrimitiveKind::Arc, 40.0), 0);
}

TEST(ContourDecomposition, TheResidualSaysHowWellEachPieceFits) {
    // Es el número que permite confiar en una primitiva. Sobre formas limpias
    // tiene que ser pequeño, o la descomposición no vale para proponer medidas.
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(120, 100, 240, 180), cv::Scalar(255), cv::FILLED);
    for (const auto& p : decomposeContour(outerContour(mask))) {
        if (p.length >= 40.0) {
            EXPECT_LT(p.rmsResidual, 1.5) << "tramo de " << p.length << " px";
        }
    }
}

TEST(ContourDecomposition, RefusesWhatIsTooSmallToDecompose) {
    EXPECT_TRUE(decomposeContour({}).empty());
    EXPECT_TRUE(decomposeContour({{0, 0}, {1, 0}, {1, 1}}).empty());
}

TEST(Holes, FindsTheInternalContoursAndIgnoresTheOutline) {
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(80, 80, 340, 340), cv::Scalar(255), cv::FILLED);
    cv::circle(mask, {160, 160}, 30, cv::Scalar(0), cv::FILLED);
    cv::circle(mask, {330, 160}, 45, cv::Scalar(0), cv::FILLED);
    cv::circle(mask, {250, 330}, 20, cv::Scalar(0), cv::FILLED);

    const auto holes = findHoles(mask);
    std::printf("  agujeros encontrados: %zu\n", holes.size());
    ASSERT_EQ(holes.size(), 3U) << "el contorno exterior no es un agujero";

    // Sus áreas son las de los círculos dibujados.
    std::vector<double> radii;
    for (const auto& hole : holes) {
        radii.push_back(std::sqrt(std::abs(cv::contourArea(hole)) / 3.14159265358979323846));
    }
    std::sort(radii.begin(), radii.end());
    EXPECT_NEAR(radii[0], 20.0, 1.5);
    EXPECT_NEAR(radii[1], 30.0, 1.5);
    EXPECT_NEAR(radii[2], 45.0, 1.5);
}

TEST(Holes, SpecklesAreFilteredOutByArea) {
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(80, 80, 340, 340), cv::Scalar(255), cv::FILLED);
    cv::circle(mask, {160, 160}, 30, cv::Scalar(0), cv::FILLED);  // agujero real
    cv::circle(mask, {300, 300}, 2, cv::Scalar(0), cv::FILLED);   // mota
    const auto holes = findHoles(mask, 40.0);
    EXPECT_EQ(holes.size(), 1U) << "una mota de 2 px no es un agujero";
}

TEST(Holes, ASolidPieceHasNone) {
    cv::Mat mask = blank();
    cv::rectangle(mask, cv::Rect(80, 80, 340, 340), cv::Scalar(255), cv::FILLED);
    EXPECT_TRUE(findHoles(mask).empty());
    EXPECT_TRUE(findHoles(cv::Mat()).empty());
}
