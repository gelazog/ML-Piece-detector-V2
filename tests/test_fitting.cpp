// Pruebas de los ajustes geométricos. La pregunta que responden no es "¿corre?"
// sino "¿cuánto se equivoca?": un ajuste que devuelve siempre un número parece
// funcionar hasta que alguien compara con la pieza patrón.
#include <gtest/gtest.h>

#include <cmath>
#include <numeric>
#include <random>
#include <vector>

#include <opencv2/core.hpp>

#include "vision/fitting.h"

using pci::vision::CircleFit;
using pci::vision::fitCircleRobust;
using pci::vision::fitCircleTaubin;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Puntos sobre un arco de `spanDeg` grados, con ruido radial gaussiano.
std::vector<cv::Point2f> arcPoints(cv::Point2f center, double radius, double spanDeg,
                                   int count, double noiseSigma, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, noiseSigma);
    std::vector<cv::Point2f> points;
    points.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double t = count == 1 ? 0.0 : static_cast<double>(i) / (count - 1);
        const double angle = (-spanDeg / 2.0 + spanDeg * t) * kPi / 180.0;
        const double r = radius + (noiseSigma > 0.0 ? noise(rng) : 0.0);
        points.emplace_back(static_cast<float>(center.x + r * std::cos(angle)),
                            static_cast<float>(center.y + r * std::sin(angle)));
    }
    return points;
}

// El ajuste que se usaba antes (Kasa), reproducido aquí como referencia: sirve
// para medir la mejora, y para que quede escrito por qué se cambió.
CircleFit fitCircleKasa(const std::vector<cv::Point2f>& points) {
    CircleFit fit;
    if (points.size() < 3) {
        return fit;
    }
    cv::Mat a(static_cast<int>(points.size()), 3, CV_64F);
    cv::Mat b(static_cast<int>(points.size()), 1, CV_64F);
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const auto& p = points[static_cast<std::size_t>(i)];
        a.at<double>(i, 0) = 2.0 * p.x;
        a.at<double>(i, 1) = 2.0 * p.y;
        a.at<double>(i, 2) = 1.0;
        b.at<double>(i, 0) = static_cast<double>(p.x) * p.x + static_cast<double>(p.y) * p.y;
    }
    cv::Mat solution;
    if (!cv::solve(a, b, solution, cv::DECOMP_SVD)) {
        return fit;
    }
    const double cx = solution.at<double>(0);
    const double cy = solution.at<double>(1);
    fit.center = cv::Point2f(static_cast<float>(cx), static_cast<float>(cy));
    fit.radius = std::sqrt(std::max(0.0, solution.at<double>(2) + cx * cx + cy * cy));
    fit.valid = true;
    return fit;
}

}  // namespace

TEST(CircleFitting, RecoversAPerfectCircleExactly) {
    const auto points = arcPoints({320.0F, 240.0F}, 100.0, 360.0, 72, 0.0, 1);
    const CircleFit fit = fitCircleTaubin(points);
    ASSERT_TRUE(fit.valid);
    EXPECT_NEAR(fit.center.x, 320.0, 1e-3);
    EXPECT_NEAR(fit.center.y, 240.0, 1e-3);
    EXPECT_NEAR(fit.radius, 100.0, 1e-3);
    EXPECT_LT(fit.rmsResidual, 1e-3);
    EXPECT_EQ(fit.inlierCount, static_cast<int>(points.size()));
}

TEST(CircleFitting, TaubinBeatsKasaOnPartialArcs) {
    // El motivo del cambio, medido. Kasa sesga el radio hacia abajo en arcos
    // cortos, que es justo el caso del radio de una esquina; Taubin apenas.
    // El test imprime los dos errores para que la diferencia quede a la vista
    // de quien lo lea, no solo en un umbral.
    constexpr double kRadius = 200.0;
    double biasAt30 = 0.0;
    double kasaBiasAt30 = 0.0;

    for (const double span : {30.0, 60.0, 90.0, 180.0, 360.0}) {
        double taubinError = 0.0;
        double kasaError = 0.0;
        constexpr int kTrials = 30;
        for (int trial = 0; trial < kTrials; ++trial) {
            const auto points = arcPoints({100.0F, 100.0F}, kRadius, span, 40, 0.5,
                                          static_cast<unsigned>(trial + 1));
            taubinError += fitCircleTaubin(points).radius - kRadius;
            kasaError += fitCircleKasa(points).radius - kRadius;
        }
        taubinError /= kTrials;  // sesgo medio, con signo
        kasaError /= kTrials;
        std::printf("  arco %5.0f°  sesgo Taubin %+8.3f px   sesgo Kasa %+8.3f px\n", span,
                    taubinError, kasaError);

        // Taubin nunca es apreciablemente peor. El margen relativo existe para
        // el caso de 360°, donde los dos aciertan y difieren en la séptima
        // cifra: ahí no hay nada que reclamar, solo ruido de coma flotante.
        EXPECT_LT(std::abs(taubinError), std::abs(kasaError) * 1.05 + 1e-3)
            << "arco de " << span << "°";

        if (span == 30.0) {
            biasAt30 = std::abs(taubinError);
            kasaBiasAt30 = std::abs(kasaError);
        }
    }

    // Y donde de verdad importa —el arco corto, que es el radio de una esquina—
    // la mejora es de otro orden, no un ajuste fino. La ventaja crece según se
    // acorta el arco: ~2x a 90°, ~3x a 60°, más de 10x a 30°.
    EXPECT_GT(kasaBiasAt30, biasAt30 * 5.0)
        << "a 30°: Taubin " << biasAt30 << " px frente a Kasa " << kasaBiasAt30 << " px";
}

TEST(CircleFitting, TheBiasOnAShortArcStaysSmall) {
    // Cota concreta: en un arco de 30° con ruido de media unidad, el radio no
    // puede irse más de un 2 %. Sin esto, "mejor que Kasa" no dice nada.
    constexpr double kRadius = 200.0;
    double bias = 0.0;
    constexpr int kTrials = 40;
    for (int trial = 0; trial < kTrials; ++trial) {
        const auto points =
            arcPoints({50.0F, 400.0F}, kRadius, 30.0, 40, 0.5, static_cast<unsigned>(trial));
        bias += fitCircleTaubin(points).radius - kRadius;
    }
    bias /= kTrials;
    EXPECT_LT(std::abs(bias), 0.02 * kRadius) << "sesgo medio " << bias << " px";
}

TEST(CircleFitting, DegenerateInputIsRejectedInsteadOfInvented) {
    EXPECT_FALSE(fitCircleTaubin({}).valid);
    EXPECT_FALSE(fitCircleTaubin({{0.0F, 0.0F}, {1.0F, 1.0F}}).valid);
    // Puntos alineados: no hay circunferencia que los explique.
    std::vector<cv::Point2f> collinear;
    for (int i = 0; i < 20; ++i) {
        collinear.emplace_back(static_cast<float>(i * 5), 100.0F);
    }
    const CircleFit fit = fitCircleTaubin(collinear);
    EXPECT_FALSE(fit.valid) << "radio inventado: " << fit.radius;
}

TEST(CircleFitting, RobustFitIgnoresABurr) {
    // Una rebaba: unos pocos puntos muy fuera del círculo. El ajuste simple los
    // promedia y se desplaza; el robusto tiene que dejarlos fuera.
    auto points = arcPoints({300.0F, 300.0F}, 80.0, 360.0, 60, 0.3, 7);
    // Saliente de 5 a 21 px por fuera del radio. Empieza en 5 y no en 0 a
    // propósito: un punto justo sobre la circunferencia no es un atípico y el
    // ajuste hace bien en conservarlo.
    for (int i = 0; i < 5; ++i) {
        points.emplace_back(385.0F + static_cast<float>(i) * 4.0F, 300.0F);
    }

    const CircleFit plain = fitCircleTaubin(points);
    const CircleFit robust = fitCircleRobust(points);
    ASSERT_TRUE(robust.valid);
    std::printf("  con rebaba: simple R=%.2f  robusto R=%.2f  (real 80.00)\n", plain.radius,
                robust.radius);

    EXPECT_NEAR(robust.radius, 80.0, 1.0);
    EXPECT_LT(std::abs(robust.radius - 80.0), std::abs(plain.radius - 80.0));
    EXPECT_LE(robust.inlierCount, 60) << "los puntos de la rebaba no deben contar";
    EXPECT_GE(robust.inlierCount, 55) << "y los buenos no deben descartarse";
}

TEST(CircleFitting, RobustFitLeavesACleanCircleAlone) {
    // La otra mitad: si no hay atípicos, el robusto no debe estropear nada ni
    // descartar puntos buenos.
    const auto points = arcPoints({120.0F, 90.0F}, 45.0, 360.0, 48, 0.2, 11);
    const CircleFit robust = fitCircleRobust(points);
    ASSERT_TRUE(robust.valid);
    EXPECT_NEAR(robust.radius, 45.0, 0.2);
    EXPECT_GE(robust.inlierCount, 44);
}

TEST(CircleFitting, ResidualTellsACircleFromSomethingElse) {
    // El residuo es lo que permite decidir si un tramo de contorno es un arco o
    // no; si no distinguiera, la descomposición del contorno sería inútil.
    const auto circle = arcPoints({200.0F, 200.0F}, 60.0, 360.0, 40, 0.2, 3);
    std::vector<cv::Point2f> square;
    for (int i = 0; i < 40; ++i) {
        const double t = static_cast<double>(i) / 10.0;
        const int side = i / 10;
        const double f = t - side;
        switch (side) {
            case 0: square.emplace_back(static_cast<float>(140.0 + 120.0 * f), 140.0F); break;
            case 1: square.emplace_back(260.0F, static_cast<float>(140.0 + 120.0 * f)); break;
            case 2: square.emplace_back(static_cast<float>(260.0 - 120.0 * f), 260.0F); break;
            default: square.emplace_back(140.0F, static_cast<float>(260.0 - 120.0 * f)); break;
        }
    }
    const double circleResidual = fitCircleTaubin(circle).rmsResidual;
    const double squareResidual = fitCircleTaubin(square).rmsResidual;
    std::printf("  residuo: círculo %.3f px   cuadrado %.3f px\n", circleResidual,
                squareResidual);
    EXPECT_LT(circleResidual, 0.5);
    EXPECT_GT(squareResidual, 5.0);
}
