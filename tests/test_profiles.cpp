// Pruebas de los perfiles radial y axial. Se miden contra figuras dibujadas con
// dimensiones exactas: si el perfil no recupera el radio que se pintó, ninguna
// medida construida encima valdrá nada.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "inspection_editor/execution/profiles.h"

using pci::inspection::axialProfile;
using pci::inspection::foundCount;
using pci::inspection::ProfileSide;
using pci::inspection::profileNormal;
using pci::inspection::radialProfile;

namespace {

// Fondo oscuro con la pieza clara: el caso de contraluz que estas herramientas
// piden.
//
// Sobre el suavizado del dibujo, medido y no supuesto: `cv::circle` con
// `LINE_AA` pinta el disco **0,60 px más grande** que el radio pedido, y ese
// desfase es constante para cualquier radio (se comprobó de R=30 a R=150). Es
// un artefacto del rasterizado, no del perfil — contra un semiplano, cuyo borde
// cae en una coordenada exacta, el perfil acierta con sesgo +0,000 px (hay un
// test que lo fija). Por eso la EXACTITUD se afirma sobre figuras de borde duro
// y el suavizado se reserva para lo que de verdad aporta: reduce la dispersión
// entre rayos a menos de la mitad (0,24 frente a 0,64 px).
cv::Mat blankScene(int width = 640, int height = 480) {
    return cv::Mat(height, width, CV_8UC1, cv::Scalar(30));
}

cv::Mat sceneWithDisk(cv::Point2f center, double radius, int lineType = cv::LINE_8) {
    cv::Mat scene = blankScene();
    cv::circle(scene, cv::Point(cvRound(center.x), cvRound(center.y)), cvRound(radius),
               cv::Scalar(220), cv::FILLED, lineType);
    return scene;
}

// Media de los radios (o de los offsets) de las muestras que encontraron borde.
double meanRadius(const std::vector<pci::inspection::RadialSample>& profile) {
    double sum = 0.0;
    int n = 0;
    for (const auto& s : profile) {
        if (s.found) {
            sum += s.radius;
            ++n;
        }
    }
    return n > 0 ? sum / n : 0.0;
}

double meanOffset(const std::vector<pci::inspection::AxialSample>& profile) {
    double sum = 0.0;
    int n = 0;
    for (const auto& s : profile) {
        if (s.found) {
            sum += s.offset;
            ++n;
        }
    }
    return n > 0 ? sum / n : 0.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Perfil radial
// ---------------------------------------------------------------------------

TEST(RadialProfile, RecoversTheRadiusOfADiskAtEveryAngle) {
    constexpr double kRadius = 90.0;
    const cv::Point2f center(320.0F, 240.0F);
    const cv::Mat scene = sceneWithDisk(center, kRadius);

    const auto profile = radialProfile(scene, center, 60.0, 120.0, 180);
    ASSERT_EQ(profile.size(), 180U);
    EXPECT_GE(foundCount(profile), 175) << "el borde del disco debería verse en casi todo rayo";

    double worst = 0.0;
    for (const auto& s : profile) {
        if (s.found) {
            worst = std::max(worst, std::abs(s.radius - kRadius));
        }
    }
    std::printf("  disco R=90: radio medio %.3f px, peor desviación %.3f px\n",
                meanRadius(profile), worst);
    EXPECT_NEAR(meanRadius(profile), kRadius, 0.2);
    EXPECT_LT(worst, 1.0) << "ningún rayo debería irse más de un píxel";
}

TEST(RadialProfile, SmoothingTightensTheSpreadButInflatesTheRadius) {
    // Deja escrito el artefacto para que nadie lo vuelva a descubrir midiendo
    // mal: el disco suavizado se lee 0,6 px más grande —lo pinta así OpenCV—
    // pero sus rayos concuerdan mucho mejor entre sí. Traducido a la práctica:
    // un borde suave da mejor REDONDEZ y peor diámetro absoluto que uno duro.
    constexpr double kRadius = 90.0;
    const cv::Point2f center(320.0F, 240.0F);

    const auto hard = radialProfile(sceneWithDisk(center, kRadius, cv::LINE_8), center, 60.0,
                                    120.0, 180);
    const auto smooth = radialProfile(sceneWithDisk(center, kRadius, cv::LINE_AA), center,
                                      60.0, 120.0, 180);

    const auto spreadOf = [](const std::vector<pci::inspection::RadialSample>& p) {
        double lo = 1e9;
        double hi = -1e9;
        for (const auto& s : p) {
            if (s.found) {
                lo = std::min(lo, s.radius);
                hi = std::max(hi, s.radius);
            }
        }
        return hi - lo;
    };
    std::printf("  duro: medio %.3f dispersión %.3f | suave: medio %.3f dispersión %.3f\n",
                meanRadius(hard), spreadOf(hard), meanRadius(smooth), spreadOf(smooth));

    EXPECT_LT(spreadOf(smooth), spreadOf(hard));
    EXPECT_GT(meanRadius(smooth), meanRadius(hard));
}

TEST(RadialProfile, ThePointMatchesTheAngleAndRadiusItReports) {
    // Coherencia interna: si `point` no cayera donde dicen `angleDeg` y
    // `radius`, dibujar el perfil y medirlo darían cosas distintas.
    const cv::Point2f center(300.0F, 220.0F);
    const cv::Mat scene = sceneWithDisk(center, 70.0);
    const auto profile = radialProfile(scene, center, 40.0, 100.0, 72);
    ASSERT_FALSE(profile.empty());
    for (const auto& s : profile) {
        if (!s.found) {
            continue;
        }
        const double rad = s.angleDeg * 3.14159265358979323846 / 180.0;
        const cv::Point2f expected(static_cast<float>(center.x + s.radius * std::cos(rad)),
                                   static_cast<float>(center.y + s.radius * std::sin(rad)));
        EXPECT_LT(cv::norm(s.point - expected), 0.05) << "ángulo " << s.angleDeg;
    }
}

TEST(RadialProfile, TheSearchWindowSelectsWhichEdgeIsFound) {
    // Un anillo tiene dos bordes. La ventana [rMin, rMax] es lo que decide cuál
    // se mide, y sin eso el círculo saltaría entre el interior y el exterior.
    cv::Mat scene = blankScene();
    const cv::Point center(320, 240);
    cv::circle(scene, center, 100, cv::Scalar(220), cv::FILLED, cv::LINE_8);
    cv::circle(scene, center, 55, cv::Scalar(30), cv::FILLED, cv::LINE_8);

    const cv::Point2f c(320.0F, 240.0F);
    EXPECT_NEAR(meanRadius(radialProfile(scene, c, 30.0, 80.0, 120)), 55.0, 0.3);
    EXPECT_NEAR(meanRadius(radialProfile(scene, c, 80.0, 130.0, 120)), 100.0, 0.3);
}

TEST(RadialProfile, AGapIsReportedAsNotFoundWithoutShiftingTheRest) {
    // Un tramo sin borde no puede desplazar las demás muestras: el periodo de
    // un engranaje se mide sobre un muestreo uniforme en ángulo, y quitar
    // muestras lo falsearía.
    cv::Mat scene = sceneWithDisk({320.0F, 240.0F}, 90.0);
    // Se borra un sector: se pinta de fondo un triángulo desde el centro.
    const std::vector<cv::Point> wedge = {{320, 240}, {520, 190}, {520, 290}};
    cv::fillConvexPoly(scene, wedge, cv::Scalar(30), cv::LINE_AA);

    const auto profile = radialProfile(scene, {320.0F, 240.0F}, 60.0, 120.0, 360);
    ASSERT_EQ(profile.size(), 360U) << "el muestreo debe seguir siendo uniforme";
    EXPECT_LT(foundCount(profile), 360) << "en el sector borrado no hay borde que encontrar";
    EXPECT_GT(foundCount(profile), 300) << "y en el resto sí";

    // Los ángulos siguen repartidos por igual pese a los huecos.
    for (std::size_t i = 1; i < profile.size(); ++i) {
        EXPECT_NEAR(profile[i].angleDeg - profile[i - 1].angleDeg, 1.0, 1e-6);
    }
}

TEST(RadialProfile, RefusesNonsenseInsteadOfGuessing) {
    const cv::Mat scene = sceneWithDisk({320.0F, 240.0F}, 90.0);
    EXPECT_TRUE(radialProfile(cv::Mat(), {320.0F, 240.0F}, 10.0, 50.0, 36).empty());
    EXPECT_TRUE(radialProfile(scene, {320.0F, 240.0F}, 50.0, 10.0, 36).empty()) << "rMax < rMin";
    EXPECT_TRUE(radialProfile(scene, {320.0F, 240.0F}, -5.0, 50.0, 36).empty());
    EXPECT_TRUE(radialProfile(scene, {320.0F, 240.0F}, 10.0, 50.0, 1).empty());
    // Centro fuera de la imagen: no debe reventar, solo no encontrar nada útil.
    const auto outside = radialProfile(scene, {-500.0F, -500.0F}, 10.0, 50.0, 36);
    EXPECT_EQ(outside.size(), 36U);
    EXPECT_EQ(foundCount(outside), 0);
}

// ---------------------------------------------------------------------------
// Perfil axial
// ---------------------------------------------------------------------------

TEST(AxialProfile, SubpixelIsExactOnAnEdgeWeControl) {
    // La afirmación de exactitud del módulo, hecha donde se puede hacer: un
    // semiplano. La primera fila clara es la 200, así que el borde está
    // exactamente en y = 199,5 y no interviene el rasterizado de ninguna curva.
    // Cualquier desviación aquí sería del perfil, no del dibujo.
    cv::Mat scene = blankScene();
    cv::rectangle(scene, cv::Rect(0, 200, 640, 280), cv::Scalar(220), cv::FILLED);

    const auto profile = axialProfile(scene, {320.0F, 100.0F}, {420.0F, 100.0F},
                                      ProfileSide::Positive, 20, 200.0);
    ASSERT_EQ(foundCount(profile), 20);
    std::printf("  semiplano: borde real a 99.500 px, medido %.3f\n", meanOffset(profile));
    EXPECT_NEAR(meanOffset(profile), 99.5, 0.05);
    for (const auto& s : profile) {
        EXPECT_NEAR(s.offset, 99.5, 0.1) << "estación t=" << s.t;
    }
}

TEST(AxialProfile, MeasuresBothSidesOfABarAndTheirSumIsItsWidth) {
    // Es exactamente lo que hará el eje torneado: dos perfiles, uno por lado, y
    // el diámetro es la suma.
    cv::Mat scene = blankScene();
    cv::rectangle(scene, cv::Rect(100, 200, 440, 80), cv::Scalar(220), cv::FILLED);

    const cv::Point2f from(140.0F, 240.0F);  // eje por el centro de la barra
    const cv::Point2f to(500.0F, 240.0F);
    const auto positive = axialProfile(scene, from, to, ProfileSide::Positive, 40, 70.0);
    const auto negative = axialProfile(scene, from, to, ProfileSide::Negative, 40, 70.0);

    ASSERT_EQ(positive.size(), 40U);
    ASSERT_EQ(negative.size(), 40U);
    EXPECT_GE(foundCount(positive), 38);
    EXPECT_GE(foundCount(negative), 38);

    const double width = meanOffset(positive) + meanOffset(negative);
    std::printf("  barra de 80 px: lados %.2f + %.2f = %.2f\n", meanOffset(positive),
                meanOffset(negative), width);
    EXPECT_NEAR(width, 80.0, 0.2);
}

TEST(AxialProfile, TheTwoSidesAreOppositeAlongTheNormal) {
    // El convenio del lado tiene que ser comprobable, no una convención oral.
    cv::Mat scene = blankScene();
    cv::rectangle(scene, cv::Rect(100, 200, 440, 80), cv::Scalar(220), cv::FILLED);
    const cv::Point2f from(140.0F, 240.0F);
    const cv::Point2f to(500.0F, 240.0F);
    const cv::Point2f n = profileNormal(from, to);
    EXPECT_NEAR(n.x, 0.0F, 1e-6);
    EXPECT_NEAR(n.y, 1.0F, 1e-6) << "eje hacia +X: la normal (-dy, dx) apunta a +Y";

    const auto positive = axialProfile(scene, from, to, ProfileSide::Positive, 10, 70.0);
    const auto negative = axialProfile(scene, from, to, ProfileSide::Negative, 10, 70.0);
    ASSERT_TRUE(positive[5].found && negative[5].found);
    // Positive va hacia +n (aquí, +Y); Negative hacia el lado contrario.
    EXPECT_GT(positive[5].point.y, 240.0F);
    EXPECT_LT(negative[5].point.y, 240.0F);
}

TEST(AxialProfile, StationsAreEvenlySpacedAlongTheAxis) {
    // La rosca mide su paso sobre este eje: si las estaciones no estuvieran
    // repartidas por igual, el paso saldría mal aunque el borde fuera perfecto.
    cv::Mat scene = blankScene();
    cv::rectangle(scene, cv::Rect(100, 200, 440, 80), cv::Scalar(220), cv::FILLED);
    const cv::Point2f from(140.0F, 240.0F);
    const cv::Point2f to(500.0F, 240.0F);
    const auto profile = axialProfile(scene, from, to, ProfileSide::Positive, 25, 70.0);
    ASSERT_EQ(profile.size(), 25U);
    EXPECT_NEAR(profile.front().t, 0.0, 1e-6);
    EXPECT_NEAR(profile.back().t, 360.0, 1e-3) << "el eje mide 360 px";
    for (std::size_t i = 1; i < profile.size(); ++i) {
        EXPECT_NEAR(profile[i].t - profile[i - 1].t, 15.0, 1e-3);
    }
}

TEST(AxialProfile, FollowsATiltedAxisJustTheSame) {
    // Nada del cálculo puede depender de que el eje esté horizontal: la pieza
    // llega como llega.
    cv::Mat scene = blankScene(700, 700);
    // Barra girada 30°: se dibuja como rectángulo rotado.
    cv::RotatedRect bar(cv::Point2f(350.0F, 350.0F), cv::Size2f(400.0F, 60.0F), 30.0F);
    cv::Point2f corners[4];
    bar.points(corners);
    std::vector<cv::Point> poly;
    for (const auto& c : corners) {
        poly.emplace_back(cvRound(c.x), cvRound(c.y));
    }
    cv::fillConvexPoly(scene, poly, cv::Scalar(220), cv::LINE_AA);

    const double a = 30.0 * 3.14159265358979323846 / 180.0;
    const cv::Point2f dir(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
    const cv::Point2f center(350.0F, 350.0F);
    const auto positive = axialProfile(scene, center - dir * 150.0F, center + dir * 150.0F,
                                       ProfileSide::Positive, 30, 60.0);
    const auto negative = axialProfile(scene, center - dir * 150.0F, center + dir * 150.0F,
                                       ProfileSide::Negative, 30, 60.0);
    EXPECT_GE(foundCount(positive), 28);
    EXPECT_GE(foundCount(negative), 28);
    EXPECT_NEAR(meanOffset(positive) + meanOffset(negative), 60.0, 1.5);
}

TEST(AxialProfile, RefusesADegenerateAxis) {
    const cv::Mat scene = blankScene();
    EXPECT_TRUE(
        axialProfile(scene, {100.0F, 100.0F}, {100.0F, 100.0F}, ProfileSide::Positive, 10, 50.0)
            .empty());
    EXPECT_TRUE(
        axialProfile(cv::Mat(), {0.0F, 0.0F}, {100.0F, 0.0F}, ProfileSide::Positive, 10, 50.0)
            .empty());
    EXPECT_TRUE(
        axialProfile(scene, {0.0F, 0.0F}, {100.0F, 0.0F}, ProfileSide::Positive, 1, 50.0)
            .empty());
    EXPECT_TRUE(
        axialProfile(scene, {0.0F, 0.0F}, {100.0F, 0.0F}, ProfileSide::Positive, 10, 0.0)
            .empty());
    const cv::Point2f nullNormal = profileNormal({5.0F, 5.0F}, {5.0F, 5.0F});
    EXPECT_EQ(nullNormal.x, 0.0F);
    EXPECT_EQ(nullNormal.y, 0.0F);
}
