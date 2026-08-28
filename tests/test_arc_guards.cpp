// LAS DOS GUARDAS SE COMPROBABAN ANTES DEL NÚMERO QUE VIGILAN.
//
// `makePrimitive` acepta un arco si barre bastante y si ajusta bastante. Las dos
// condiciones se evaluaban con el ajuste VORAZ; después, el reajuste robusto
// —que está ahí porque el voraz dejaba el radio hasta un 40 % desviado—
// republica radio, barrido y residuo, y nadie volvía a mirar.
//
// Lo que eso publicaba, medido sobre los 1288 arcos del banco de fotos:
//
//     barrido mínimo                    0,4 grados   (la opción pide 15)
//     radio máximo / radio de la pieza     31 x
//     residuo de los arcos del dodecágono  0,83-0,87  (el tope es 0,80)
//
// Un arco de radio treinta y una veces su propia pieza no es una curva: es un
// lado recto con un número inventado encima.
//
// POR QUÉ NO SE PUDO ARREGLAR ANTES, que es la parte interesante.
//
// Se intentó dos veces y se revirtió las dos. Reaplicar las guardas convertía
// en rectas unos arcos falsos, y eso hacía que la rama de «polígono redondeado»
// —que se pregunta la PRIMERA y se disparaba con `straight >= 3 && arcs >= 1 &&
// curva >= 10 %`— saltara en piezas que no lo son: un dodecágono salía
// «redondeado de 3 lados» y un polígono de 16 salía «redondeado» en vez de
// círculo.
//
// La rama era el punto frágil, no las guardas. Con dos condiciones que
// significan algo geométrico —los arcos son las ESQUINAS, o sea más cortos que
// los lados, y hay tantos como lados— la rama deja de dispararse donde no toca
// y las guardas entran sin romper nada.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "vision/contour_analysis.h"
#include "vision/geometry_features.h"
#include "vision/pipeline.h"

using namespace pci;

namespace {

std::vector<cv::Point> roundedRectangle(int width, int height, int radius) {
    cv::Mat canvas(height + 80, width + 80, CV_8UC1, cv::Scalar(0));
    const cv::Rect box(40, 40, width, height);
    cv::rectangle(canvas, cv::Rect(box.x + radius, box.y, box.width - 2 * radius, box.height),
                  cv::Scalar(255), cv::FILLED);
    cv::rectangle(canvas, cv::Rect(box.x, box.y + radius, box.width, box.height - 2 * radius),
                  cv::Scalar(255), cv::FILLED);
    for (const auto& corner :
         {cv::Point(box.x + radius, box.y + radius),
          cv::Point(box.x + box.width - radius, box.y + radius),
          cv::Point(box.x + radius, box.y + box.height - radius),
          cv::Point(box.x + box.width - radius, box.y + box.height - radius)}) {
        cv::circle(canvas, corner, radius, cv::Scalar(255), cv::FILLED);
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(canvas, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    return contours.empty() ? std::vector<cv::Point>{} : contours.front();
}

}  // namespace

TEST(ArcGuards, NothingIsPublishedAsAnArcThatTheOptionsWouldRefuse) {
    // El contrato, dicho sobre lo que se PUBLICA y no sobre un valor intermedio
    // que después cambia.
    const auto contour = roundedRectangle(300, 200, 40);
    ASSERT_FALSE(contour.empty());

    const vision::DecomposeOptions options;
    int arcs = 0;
    int lines = 0;
    double smallestSweep = 1e9;
    double worstResidual = 0.0;
    for (const auto& primitive : vision::decomposeContour(contour, options)) {
        if (primitive.kind == vision::PrimitiveKind::Arc) {
            ++arcs;
            smallestSweep = std::min(smallestSweep, primitive.sweepDeg);
            worstResidual = std::max(worstResidual, primitive.rmsResidual);
        } else {
            ++lines;
        }
    }
    std::printf("  [arco] rectángulo redondeado: %d rectas, %d arcos; el menor barre "
                "%.1f° y el peor residuo es %.2f\n",
                lines, arcs, arcs > 0 ? smallestSweep : 0.0, worstResidual);

    // Que siga reconociendo la pieza: sin arcos esta prueba pasaría por no tener
    // nada que comprobar, que es la forma más fácil de fingir.
    ASSERT_GE(arcs, 4) << "no se reconocen las cuatro esquinas: la guarda se ha llevado "
                          "por delante arcos de verdad";
    ASSERT_GE(lines, 4) << "no se reconocen los cuatro lados";
    EXPECT_GE(smallestSweep, options.minArcSweepDeg)
        << "se publica un arco que barre menos de lo que la propia opción admite";
    EXPECT_LE(worstResidual, options.maxResidual)
        << "se publica un arco con un error mayor del que su propia tolerancia admite: "
           "es un ajuste rechazado que nadie volvió a mirar";
}

TEST(ArcGuards, NoArcIsWiderThanSeveralTimesItsOwnPiece) {
    // El síntoma que se veía en las fotos. Una curva que forma parte del
    // contorno de una pieza no puede tener un radio enorme y seguir
    // curvándose; si sale así, es un lado recto al que se le ha puesto un radio.
    const cv::Mat image = cv::imread(
        "C:/Users/furro/Pictures/IMG-MC/producto-tuercas-prueba.jpg", cv::IMREAD_COLOR);
    if (image.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    vision::PipelineConfig config;
    config.segmentation.recoverHighlightsBy = 12;
    auto all = vision::analyzeFrames(image, config);
    ASSERT_TRUE(all.isOk());
    ASSERT_GE(all.value().size(), 50U);

    double worstRatio = 0.0;
    double smallestSweep = 1e9;
    int arcs = 0;
    for (const auto& piece : all.value()) {
        cv::Point2f centre;
        float radius = 0.0F;
        cv::minEnclosingCircle(piece.contour.points, centre, radius);
        if (radius <= 1.0F) {
            continue;
        }
        for (const auto& primitive :
             vision::decomposeContour(piece.contour.points, vision::DecomposeOptions{})) {
            if (primitive.kind != vision::PrimitiveKind::Arc) {
                continue;
            }
            ++arcs;
            worstRatio = std::max(worstRatio, primitive.radius / radius);
            smallestSweep = std::min(smallestSweep, primitive.sweepDeg);
        }
    }
    std::printf("  [arco] %d arcos en la bandeja: el mayor mide %.1f veces su pieza, "
                "el menor barre %.1f°\n",
                arcs, worstRatio, smallestSweep);

    EXPECT_GE(smallestSweep, vision::DecomposeOptions{}.minArcSweepDeg)
        << "vuelve a publicarse un arco por debajo del tope: la guarda se está "
           "comprobando otra vez antes del reajuste";
    // El límite MEDIDO de hoy, no un ideal: antes de esto llegaba a 31.
    EXPECT_LT(worstRatio, 6.0)
        << "reaparecen arcos de radio mucho mayor que la pieza que los contiene";
}

TEST(ArcGuards, ARoundedPolygonIsOneWhoseArcsAreItsCorners) {
    // La condición que permitió que las guardas entraran, y que vale por sí
    // sola: los arcos de un polígono redondeado son sus ESQUINAS. Una esquina es
    // más corta que el lado al que pertenece, y hay tantas como lados.
    //
    // Medido: los rectángulos redondeados de verdad dan 4 rectas y 4 arcos, con
    // un cociente arco/lado de 0,07 a 0,60. Un dodecágono mal leído da 5 rectas
    // y 4 arcos con cociente 3,57 — ni son tantos ni son cortos.
    for (const int radius : {20, 40, 60}) {
        const auto contour = roundedRectangle(300, 200, radius);
        int lines = 0;
        int arcs = 0;
        std::vector<double> lineLengths;
        std::vector<double> arcLengths;
        for (const auto& primitive :
             vision::decomposeContour(contour, vision::DecomposeOptions{})) {
            if (primitive.kind == vision::PrimitiveKind::Line) {
                ++lines;
                lineLengths.push_back(primitive.length);
            } else {
                ++arcs;
                arcLengths.push_back(primitive.length);
            }
        }
        std::sort(lineLengths.begin(), lineLengths.end());
        std::sort(arcLengths.begin(), arcLengths.end());
        const double ratio = lineLengths.empty() || arcLengths.empty()
                                 ? -1.0
                                 : arcLengths[arcLengths.size() / 2] /
                                       lineLengths[lineLengths.size() / 2];
        std::printf("  [arco] redondeo %2d: %d rectas, %d arcos, arco/lado %.2f\n", radius,
                    lines, arcs, ratio);
        EXPECT_EQ(arcs, lines)
            << "un rectángulo redondeado tiene tantas esquinas como lados, y con radio "
            << radius << " no salen iguales";
        EXPECT_LT(ratio, 0.75)
            << "las esquinas salen tan largas como los lados: entonces no son esquinas";
    }
}
