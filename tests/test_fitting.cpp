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
    // En una vuelta completa el reparto es por `count` (si fuera por count-1,
    // el primer punto y el último caerían encima y sesgarían los momentos).
    const bool closed = spanDeg >= 359.999;
    const int divisions = closed ? count : std::max(1, count - 1);
    for (int i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / divisions;
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

// ---------------------------------------------------------------------------
// Rectas
// ---------------------------------------------------------------------------

namespace {

// Puntos sobre la recta que pasa por `origin` con el ángulo dado, repartidos a
// lo largo de `span` px, con ruido perpendicular.
std::vector<cv::Point2f> linePoints(cv::Point2f origin, double angleDeg, double span,
                                    int count, double noiseSigma, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, noiseSigma);
    const double a = angleDeg * kPi / 180.0;
    const cv::Point2d dir(std::cos(a), std::sin(a));
    const cv::Point2d normal(-dir.y, dir.x);
    std::vector<cv::Point2f> points;
    points.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double t = -span / 2.0 + span * i / std::max(1, count - 1);
        const double n = noiseSigma > 0.0 ? noise(rng) : 0.0;
        points.emplace_back(static_cast<float>(origin.x + dir.x * t + normal.x * n),
                            static_cast<float>(origin.y + dir.y * t + normal.y * n));
    }
    return points;
}

}  // namespace

TEST(LineFitting, RecoversEveryOrientationIncludingTheVertical) {
    // La vertical es el caso que rompe el ajuste clásico y=mx+b (pendiente
    // infinita). Con mínimos cuadrados totales no tiene nada de especial, y eso
    // es justo lo que hay que comprobar.
    for (const double angle : {0.0, 17.0, 45.0, 89.0, 90.0, -45.0, -89.0}) {
        const auto points = linePoints({250.0F, 180.0F}, angle, 200.0, 40, 0.0, 1);
        const pci::vision::LineFit fit = pci::vision::fitLineTotal(points);
        ASSERT_TRUE(fit.valid) << "ángulo " << angle;
        // La recta no tiene sentido, así que 90° y -90° son la misma.
        const double expected = angle <= -90.0 ? angle + 180.0 : angle;
        EXPECT_NEAR(fit.angleDeg(), expected, 1e-3) << "ángulo pedido " << angle;
        EXPECT_LT(fit.rmsResidual, 1e-3);
        // Y todos los puntos caen sobre ella.
        for (const auto& p : points) {
            EXPECT_LT(std::abs(fit.signedDistance(p)), 1e-3);
        }
    }
}

TEST(LineFitting, TheResidualMatchesTheNoiseItWasGiven) {
    // Si el residuo no refleja el ruido real, no sirve para decidir si un tramo
    // de contorno es recto: es el criterio de la descomposición del contorno.
    for (const double sigma : {0.25, 1.0, 3.0}) {
        const auto points = linePoints({0.0F, 0.0F}, 33.0, 400.0, 400, sigma, 5);
        const pci::vision::LineFit fit = pci::vision::fitLineTotal(points);
        ASSERT_TRUE(fit.valid);
        EXPECT_NEAR(fit.rmsResidual, sigma, sigma * 0.2) << "sigma " << sigma;
        EXPECT_NEAR(fit.angleDeg(), 33.0, 1.0);
    }
}

TEST(LineFitting, SignedDistanceSeparatesTheTwoSides) {
    // El signo es lo que permite separar los dos costados de un eje torneado:
    // sin él, los dos bordes se mezclarían en un solo ajuste.
    const auto points = linePoints({100.0F, 100.0F}, 0.0, 200.0, 20, 0.0, 1);
    const pci::vision::LineFit fit = pci::vision::fitLineTotal(points);
    ASSERT_TRUE(fit.valid);
    const double above = fit.signedDistance({100.0F, 90.0F});
    const double below = fit.signedDistance({100.0F, 110.0F});
    EXPECT_LT(above * below, 0.0) << "los dos lados deben tener signo opuesto";
    EXPECT_NEAR(std::abs(above), 10.0, 1e-3);
    EXPECT_NEAR(std::abs(below), 10.0, 1e-3);
}

TEST(LineFitting, DirectionIsCanonicalSoAnglesDoNotFlip) {
    // Los mismos puntos en orden inverso deben dar exactamente la misma recta;
    // si no, un ángulo entre dos rectas saltaría 180° según cómo se recorrieran.
    auto forward = linePoints({40.0F, 40.0F}, 120.0, 150.0, 30, 0.0, 2);
    std::vector<cv::Point2f> backward(forward.rbegin(), forward.rend());
    const pci::vision::LineFit a = pci::vision::fitLineTotal(forward);
    const pci::vision::LineFit b = pci::vision::fitLineTotal(backward);
    ASSERT_TRUE(a.valid && b.valid);
    EXPECT_NEAR(a.angleDeg(), b.angleDeg(), 1e-6);
    EXPECT_GE(a.direction.x, 0.0F);
    EXPECT_LE(a.angleDeg(), 90.0);
    EXPECT_GT(a.angleDeg(), -90.0);
}

TEST(LineFitting, RobustFitIgnoresAChip) {
    // Una viruta pegada al borde: unos pocos puntos claramente fuera.
    auto points = linePoints({200.0F, 200.0F}, 10.0, 300.0, 60, 0.3, 9);
    for (int i = 0; i < 6; ++i) {
        points.emplace_back(220.0F + static_cast<float>(i) * 3.0F, 160.0F);
    }
    const pci::vision::LineFit plain = pci::vision::fitLineTotal(points);
    const pci::vision::LineFit robust = pci::vision::fitLineRobust(points);
    ASSERT_TRUE(robust.valid);
    std::printf("  con viruta: simple %.2f°  robusto %.2f°  (real 10.00°)\n",
                plain.angleDeg(), robust.angleDeg());
    EXPECT_NEAR(robust.angleDeg(), 10.0, 0.5);
    EXPECT_LT(std::abs(robust.angleDeg() - 10.0), std::abs(plain.angleDeg() - 10.0));
    EXPECT_LE(robust.inlierCount, 60);
    EXPECT_GE(robust.inlierCount, 55);
}

TEST(LineFitting, RobustFitLeavesACleanLineAlone) {
    const auto points = linePoints({10.0F, 500.0F}, -60.0, 250.0, 50, 0.2, 13);
    const pci::vision::LineFit robust = pci::vision::fitLineRobust(points);
    ASSERT_TRUE(robust.valid);
    EXPECT_NEAR(robust.angleDeg(), -60.0, 0.3);
    EXPECT_GE(robust.inlierCount, 46);
}

TEST(LineFitting, RefusesWhatIsNotALine) {
    EXPECT_FALSE(pci::vision::fitLineTotal({}).valid);
    EXPECT_FALSE(pci::vision::fitLineTotal({{5.0F, 5.0F}}).valid);
    // Todos los puntos en el mismo sitio: no hay dirección.
    EXPECT_FALSE(
        pci::vision::fitLineTotal({{7.0F, 7.0F}, {7.0F, 7.0F}, {7.0F, 7.0F}}).valid);
}

TEST(LineFitting, AnisotropySaysWhenTheDirectionMeansNothing) {
    // Una nube redonda (puntos en círculo) sí produce una dirección, pero es
    // ruido: no hay eje principal. En vez de esconder un umbral dentro del
    // ajuste, se devuelve la anisotropía —la misma medida que ya usa el fixture
    // para no perseguir el ángulo de piezas casi circulares— y decide quien
    // pregunta, que es el único que sabe cuánta le hace falta.
    const auto circle = arcPoints({0.0F, 0.0F}, 50.0, 360.0, 60, 0.0, 4);
    const pci::vision::LineFit round = pci::vision::fitLineTotal(circle);
    ASSERT_TRUE(round.valid);
    EXPECT_LT(round.anisotropy, 0.05) << "una nube redonda no tiene dirección fiable";

    const auto straight = linePoints({0.0F, 0.0F}, 25.0, 300.0, 60, 0.5, 4);
    const pci::vision::LineFit line = pci::vision::fitLineTotal(straight);
    ASSERT_TRUE(line.valid);
    EXPECT_GT(line.anisotropy, 0.95) << "una recta de verdad sí";

    // Y el residuo distingue lo mismo por otra vía: una nube redonda no es una
    // recta delgada. Las dos señales tienen que apuntar al mismo sitio.
    EXPECT_GT(round.rmsResidual, 10.0 * line.rmsResidual);
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

// ---------------------------------------------------------------------------
// El invariante de la zona minima (D2)
// ---------------------------------------------------------------------------

namespace {

// Banda que hace falta alrededor de la recta de MINIMOS CUADRADOS para contener
// todos los puntos: el maximo residuo con signo menos el minimo.
double leastSquaresBandWidth(const std::vector<cv::Point2f>& points) {
    const pci::vision::LineFit fit = pci::vision::fitLineRobust(points);
    EXPECT_TRUE(fit.valid);
    const cv::Point2f normal(-fit.direction.y, fit.direction.x);
    double lo = 1e18;
    double hi = -1e18;
    for (const auto& p : points) {
        const double d = (p - fit.point).dot(normal);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    return hi - lo;
}

// Lo mismo para la redondez: separacion radial alrededor del centro que da el
// ajuste de circulo por minimos cuadrados.
double leastSquaresRadialWidth(const std::vector<cv::Point2f>& points) {
    const pci::vision::CircleFit fit = pci::vision::fitCircleTaubin(points);
    EXPECT_TRUE(fit.valid);
    double lo = 1e18;
    double hi = -1e18;
    for (const auto& p : points) {
        const double r = cv::norm(p - fit.center);
        lo = std::min(lo, r);
        hi = std::max(hi, r);
    }
    return hi - lo;
}

}  // namespace

TEST(MinimumZoneInvariant, NeverWiderThanTheLeastSquaresBand) {
    // Invariante matematico, no una medida empirica: la zona minima es el MINIMO
    // sobre todas las orientaciones, y la banda alrededor de la recta de minimos
    // cuadrados es una candidata mas. No puede ganarle nunca.
    //
    // Barato de comprobar y sorprendentemente util: casi cualquier error de
    // implementacion en los calipers giratorios lo rompe. Y de paso deja escrito
    // por que la norma pide zona minima — usar minimos cuadrados da SIEMPRE un
    // valor igual o mayor, o sea que rechaza piezas buenas.
    std::mt19937 rng(20260812);
    std::uniform_real_distribution<double> noise(-3.0, 3.0);
    double worstRatio = 0.0;
    int strictlyBetter = 0;
    for (int trial = 0; trial < 200; ++trial) {
        std::vector<cv::Point2f> points;
        const double slope = std::uniform_real_distribution<double>(-2.0, 2.0)(rng);
        for (int i = 0; i < 40; ++i) {
            const double x = i * 4.0;
            points.emplace_back(static_cast<float>(x + noise(rng)),
                                static_cast<float>(120.0 + slope * x + noise(rng)));
        }
        const auto zone = pci::vision::minimumZoneBand(points);
        ASSERT_TRUE(zone.valid);
        const double lsq = leastSquaresBandWidth(points);
        EXPECT_LE(zone.width, lsq + 1e-6)
            << "zona minima " << zone.width << " > minimos cuadrados " << lsq;
        if (zone.width < lsq - 1e-6) {
            ++strictlyBetter;
        }
        worstRatio = std::max(worstRatio, zone.width / std::max(lsq, 1e-9));
    }
    std::printf("  200 nubes: la zona minima gana estrictamente en %d, peor razon %.4f\n",
                strictlyBetter, worstRatio);
    // Y que no sea una igualdad disfrazada: si las dos dieran siempre lo mismo,
    // el test pasaria sin probar que son cosas distintas.
    EXPECT_GT(strictlyBetter, 100) << "las dos coinciden demasiado: ¿de verdad se esta "
                                      "minimizando el ancho y no los cuadrados?";
}

TEST(MinimumZoneInvariant, TheRoundnessZoneIsNeverWiderThanTheLeastSquaresOne) {
    // El mismo argumento en circular: el centro que menos separa el radio maximo
    // del minimo casi nunca es el que minimiza los residuos al cuadrado.
    std::mt19937 rng(20260813);
    std::uniform_real_distribution<double> noise(-2.0, 2.0);
    double worstRatio = 0.0;
    int strictlyBetter = 0;
    for (int trial = 0; trial < 100; ++trial) {
        std::vector<cv::Point2f> points;
        const double radius = std::uniform_real_distribution<double>(40.0, 120.0)(rng);
        for (int i = 0; i < 72; ++i) {
            const double a = 2.0 * CV_PI * i / 72.0;
            const double r = radius + noise(rng);
            points.emplace_back(static_cast<float>(200.0 + r * std::cos(a)),
                                static_cast<float>(200.0 + r * std::sin(a)));
        }
        const auto zone = pci::vision::minimumZoneCircle(points);
        ASSERT_TRUE(zone.valid);
        const double lsq = leastSquaresRadialWidth(points);
        EXPECT_LE(zone.width(), lsq + 1e-6)
            << "MZC " << zone.width() << " > minimos cuadrados " << lsq;
        if (zone.width() < lsq - 1e-6) {
            ++strictlyBetter;
        }
        worstRatio = std::max(worstRatio, zone.width() / std::max(lsq, 1e-9));
    }
    std::printf("  100 perfiles: la MZC gana estrictamente en %d, peor razon %.4f\n",
                strictlyBetter, worstRatio);
    EXPECT_GT(strictlyBetter, 50) << "la MZC y los minimos cuadrados coinciden demasiado";
}
