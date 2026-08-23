// EL MAPA DE DIFERENCIA: ¿señala el defecto, o señala el contorno?
//
// Un NG por anomalía es hoy un número: la similitud cayó por debajo de la banda.
// El operador tiene que buscar a ojo qué le pasa a la pieza, y a veces no le
// pasa nada — lo que ha cambiado es la luz, o la pieza está medio grado girada.
//
// Esto se mide antes de darle sitio en la interfaz, porque un mapa de calor que
// enciende el contorno de todas las piezas parece muy técnico y no dice nada. La
// comprobación central de este fichero no es que el mapa se calcule: es que el
// punto más caliente CAE ENCIMA del defecto, y que dos fotos de la misma pieza
// no encienden nada.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>

#include "vision/difference_map.h"

using pci::vision::compareToReference;
using pci::vision::DifferenceOptions;
using pci::vision::paintDifference;

namespace {

constexpr int kSide = 256;

// Un recorte normalizado creíble: pieza clara sobre fondo a cero, con textura
// suave para que la comparación no sea trivial.
cv::Mat referencePiece() {
    cv::Mat crop = cv::Mat::zeros(kSide, kSide, CV_8UC1);
    cv::circle(crop, {kSide / 2, kSide / 2}, 95, cv::Scalar(180), cv::FILLED, cv::LINE_8);
    // Un par de rasgos internos, como los tendría una pieza real.
    cv::circle(crop, {kSide / 2, kSide / 2}, 30, cv::Scalar(120), cv::FILLED, cv::LINE_8);
    cv::rectangle(crop, cv::Rect(96, 40, 64, 18), cv::Scalar(210), cv::FILLED);
    return crop;
}

// La distancia del punto más caliente al defecto que se puso a propósito.
double missBy(const pci::vision::DifferenceMap& map, const cv::Point& defect) {
    return std::hypot(static_cast<double>(map.worst.x - defect.x),
                      static_cast<double>(map.worst.y - defect.y));
}

}  // namespace

// LO QUE TIENE QUE HACER: señalar dónde está el defecto.
TEST(DifferenceMap, ThePeakLandsOnTheDefectAndNotOnTheOutline) {
    const cv::Mat reference = referencePiece();

    // Tres defectos distintos, en tres sitios distintos de la pieza.
    struct Case {
        const char* name;
        cv::Point where;
    };
    const Case cases[] = {
        {"muesca en el borde", {128 + 90, 128}},
        {"arañazo en el centro", {100, 150}},
        {"mancha oscura", {160, 90}},
    };

    for (const auto& one : cases) {
        cv::Mat damaged = reference.clone();
        if (std::string(one.name) == "muesca en el borde") {
            cv::circle(damaged, one.where, 14, cv::Scalar(0), cv::FILLED, cv::LINE_8);
        } else if (std::string(one.name) == "arañazo en el centro") {
            cv::line(damaged, one.where - cv::Point(18, 0), one.where + cv::Point(18, 0),
                     cv::Scalar(60), 5, cv::LINE_8);
        } else {
            cv::circle(damaged, one.where, 12, cv::Scalar(90), cv::FILLED, cv::LINE_8);
        }

        const auto map = compareToReference(damaged, reference);
        ASSERT_TRUE(map.ok) << map.problem;
        const double miss = missBy(map, one.where);
        std::printf("  [diferencia] %-22s pico a %5.1f px del defecto, intensidad %.2f, "
                    "encendido %.1f %%\n",
                    one.name, miss, map.worstValue, 100.0 * map.litFraction);
        EXPECT_LT(miss, 20.0)
            << one.name << ": el punto más caliente no cae sobre el defecto. Un mapa que "
                           "señala otro sitio es peor que no tener mapa";
        EXPECT_GT(map.worstValue, 0.2) << one.name << ": el defecto apenas se enciende";
    }
}

// LO QUE NO PUEDE HACER: encenderse porque la pieza se movió un píxel.
//
// Es la mitad que decide si esto sirve. Dos recortes de la MISMA pieza nunca
// coinciden píxel a píxel —el centroide y el ángulo se estiman de un contorno
// que baila— y una resta a secas enciende todo el borde. Ese mapa señalaría el
// contorno en todas las piezas, o sea nada.
TEST(DifferenceMap, TheSamePieceShiftedALittleDoesNotLightUp) {
    const cv::Mat reference = referencePiece();

    for (const int shift : {1, 2}) {
        cv::Mat moved = cv::Mat::zeros(reference.size(), CV_8UC1);
        const cv::Mat translation =
            (cv::Mat_<double>(2, 3) << 1, 0, static_cast<double>(shift), 0, 1,
             static_cast<double>(shift));
        cv::warpAffine(reference, moved, translation, reference.size());

        const auto map = compareToReference(moved, reference);
        ASSERT_TRUE(map.ok) << map.problem;
        std::printf("  [diferencia] misma pieza movida %d px: intensidad %.2f\n", shift,
                    map.worstValue);
        EXPECT_LT(map.worstValue, 0.25)
            << "la misma pieza movida " << shift
            << " px enciende el mapa: entonces señalaría el contorno de todas las piezas "
               "y no serviría para encontrar nada";
    }
}

// Ni porque la luz haya cambiado.
TEST(DifferenceMap, TheSamePieceUnderDifferentLightDoesNotLightUp) {
    const cv::Mat reference = referencePiece();
    cv::Mat brighter;
    // Más luz y algo más de contraste: la misma pieza en otra toma.
    reference.convertTo(brighter, CV_8U, 1.18, 12.0);
    // El fondo tiene que seguir siendo fondo, o el recorte deja de ser un
    // recorte normalizado.
    brighter.setTo(cv::Scalar(0), reference == 0);

    const auto map = compareToReference(brighter, reference);
    ASSERT_TRUE(map.ok) << map.problem;
    std::printf("  [diferencia] misma pieza con otra luz: intensidad %.2f\n",
                map.worstValue);
    EXPECT_LT(map.worstValue, 0.25)
        << "un cambio de luz enciende el mapa entero: el operador vería un defecto cada "
           "vez que alguien enciende una lámpara";
}

// Y dos fotos idénticas no encienden nada en absoluto.
TEST(DifferenceMap, AnIdenticalCropIsCompletelyQuiet) {
    const cv::Mat reference = referencePiece();
    const auto map = compareToReference(reference, reference);
    ASSERT_TRUE(map.ok) << map.problem;
    std::printf("  [diferencia] recorte idéntico: intensidad %.3f\n", map.worstValue);
    EXPECT_LT(map.worstValue, 0.02);
    EXPECT_LT(map.litFraction, 0.02);
}

// La superficie encendida distingue «un arañazo» de «esta pieza no es la misma».
TEST(DifferenceMap, TheLitAreaTellsAScratchFromADifferentPiece) {
    const cv::Mat reference = referencePiece();

    cv::Mat scratched = reference.clone();
    cv::line(scratched, {100, 150}, {136, 150}, cv::Scalar(60), 5, cv::LINE_8);
    const auto scratch = compareToReference(scratched, reference);
    ASSERT_TRUE(scratch.ok);

    // Otra pieza: un cuadrado donde había un círculo.
    cv::Mat other = cv::Mat::zeros(kSide, kSide, CV_8UC1);
    cv::rectangle(other, cv::Rect(40, 40, 176, 176), cv::Scalar(180), cv::FILLED);
    const auto different = compareToReference(other, reference);
    ASSERT_TRUE(different.ok);

    std::printf("  [diferencia] arañazo enciende %.1f %%, otra pieza enciende %.1f %%\n",
                100.0 * scratch.litFraction, 100.0 * different.litFraction);
    EXPECT_LT(scratch.litFraction, 0.10)
        << "un arañazo enciende media pieza: la cifra no distingue un defecto local de "
           "una pieza equivocada";
    EXPECT_GT(different.litFraction, scratch.litFraction * 2.0)
        << "una pieza distinta enciende lo mismo que un arañazo: entonces esta cifra no "
           "informa de nada";
}

// El mapa pintado enseña la pieza DEBAJO. Un borrón de color sin la pieza
// detrás no señala ningún sitio.
TEST(DifferenceMap, ThePaintedMapKeepsThePieceVisible) {
    const cv::Mat reference = referencePiece();
    cv::Mat damaged = reference.clone();
    cv::circle(damaged, {100, 150}, 12, cv::Scalar(40), cv::FILLED, cv::LINE_8);

    const auto map = compareToReference(damaged, reference);
    ASSERT_TRUE(map.ok);
    const cv::Mat painted = paintDifference(damaged, map);
    ASSERT_EQ(painted.size(), damaged.size());
    ASSERT_EQ(painted.channels(), 3);

    // Lejos del defecto, la pieza se ve TAL CUAL: el tinte se pesa con el propio
    // mapa, así que donde no hay diferencia no hay color. Una mezcla uniforme
    // teñiría la pieza entera y taparía justo lo que hay que mirar.
    cv::Mat baseBgr;
    cv::cvtColor(damaged, baseBgr, cv::COLOR_GRAY2BGR);
    const cv::Vec3b farBase = baseBgr.at<cv::Vec3b>(128, 200);
    const cv::Vec3b farPainted = painted.at<cv::Vec3b>(128, 200);
    int farShift = 0;
    for (int c = 0; c < 3; ++c) {
        farShift = std::max(farShift, std::abs(farPainted[c] - farBase[c]));
    }

    // Y EN EL PUNTO QUE EL PROPIO MAPA SEÑALA, el color tiene que ser
    // inconfundible. Se mira ahí y no en el centro geométrico del defecto: el
    // mapa está suavizado, así que su máximo cae cerca pero no exactamente
    // encima, y comprobar el centro medía un sitio que el mapa no señalaba.
    const cv::Vec3b atBase = baseBgr.at<cv::Vec3b>(map.worst);
    const cv::Vec3b atPainted = painted.at<cv::Vec3b>(map.worst);
    int atShift = 0;
    for (int c = 0; c < 3; ++c) {
        atShift = std::max(atShift, std::abs(atPainted[c] - atBase[c]));
    }

    std::printf("  [diferencia] el tinte mueve %d niveles lejos del defecto y %d en el "
                "punto señalado\n", farShift, atShift);
    EXPECT_LT(farShift, 12) << "el mapa tiñe la pieza entera y tapa lo que hay que mirar";
    EXPECT_GT(atShift, 60)
        << "en el punto que el propio mapa señala como el peor apenas se pinta: el "
           "operador no tiene dónde mirar";
}

// Y UNA PIEZA LIMPIA NO SE PINTA EN ABSOLUTO.
//
// Es la condición del realce de arriba: el mapa se estira hasta su propio
// máximo para que el punto peor se vea, y sin este corte una pieza sin defectos
// enseñaría un faro encendido sobre su píxel más ruidoso. Un mapa que siempre
// señala algo enseña a ignorarlo.
TEST(DifferenceMap, ACleanPieceIsNotPaintedAtAll) {
    const cv::Mat reference = referencePiece();
    cv::Mat clean;
    cv::Mat noise(reference.size(), CV_8UC1);
    cv::randn(noise, 0, 2);
    cv::add(reference, noise, clean);
    clean.setTo(cv::Scalar(0), reference == 0);

    const auto map = compareToReference(clean, reference);
    ASSERT_TRUE(map.ok);
    std::printf("  [diferencia] pieza limpia con ruido: intensidad %.3f\n", map.worstValue);

    const cv::Mat painted = paintDifference(clean, map);
    cv::Mat cleanBgr;
    cv::cvtColor(clean, cleanBgr, cv::COLOR_GRAY2BGR);
    EXPECT_EQ(cv::countNonZero(cv::Mat(painted != cleanBgr).reshape(1)), 0)
        << "se pinta calor sobre una pieza que no tiene nada: el operador aprendería a "
           "no hacerle caso al mapa";
}

// Entradas imposibles se explican en vez de reventar.
TEST(DifferenceMap, ImpossibleInputsSayWhy) {
    const cv::Mat reference = referencePiece();
    const auto none = compareToReference({}, reference);
    EXPECT_FALSE(none.ok);
    EXPECT_FALSE(none.problem.empty());

    const cv::Mat blank = cv::Mat::zeros(kSide, kSide, CV_8UC1);
    const auto empty = compareToReference(blank, blank);
    EXPECT_FALSE(empty.ok);
    std::printf("  [diferencia] recorte vacío dice: %s\n", empty.problem.c_str());

    // Referencia de otro tamaño: se reescala en vez de rendirse, porque una
    // versión anterior pudo guardar el recorte con otra resolución.
    cv::Mat small;
    cv::resize(reference, small, cv::Size(128, 128));
    const auto rescaled = compareToReference(reference, small);
    EXPECT_TRUE(rescaled.ok) << rescaled.problem;
}
