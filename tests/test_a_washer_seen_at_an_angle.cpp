// UNA ARANDELA VISTA DE REFILÓN SALÍA COMO «POLÍGONO DE 8 LADOS».
//
// En la mesa las piezas redondas están tumbadas, pero las que caen lejos del
// centro del encuadre se ven en PERSPECTIVA: son elipses. Con una óptica normal
// eso es un 10-20 % de excentricidad sin que nada vaya mal, y en
// `arandelas-1.png` —veinte arandelas repartidas por un cartón— pasa en media
// docena de ellas.
//
// A una elipse, un octógono la explica mejor que una circunferencia. Medido
// sobre esas piezas, el punto peor se separa:
//
//     del octógono   1,4 a 3,5 px
//     del círculo    2,4 a 6,2 px   <- el círculo pierde
//     de la elipse   1,1 a 2,3 px   <- y la elipse gana a los dos
//
// Así que el clasificador NO se estaba equivocando de regla: con las dos varas
// que tenía, el octógono ganaba de verdad. Lo que faltaba era la tercera.
//
// Lo que costaba en pantalla: la arandela salía titulada «Polígono de 8 lados» y
// con ocho «Lado N» y ocho «Ángulo N» entre sus cotas — dieciséis números que
// sobre una arandela no significan nada, y que la arandela de al lado no repite.
//
// Esta prueba dibuja el caso en vez de depender del banco: una elipse con su
// agujero, que es una arandela vista de refilón, y un octógono de verdad al
// lado. La primera no puede tener lados y el segundo tiene que conservarlos —
// sin esa segunda mitad, «no llamar polígono a nada» pasaría la prueba.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#include "inspection_editor/piece_report.h"
#include "vision/shape_class.h"

using namespace pci;

namespace {

// La máscara de una figura dibujada sobre negro.
cv::Mat maskOf(const cv::Mat& drawing) {
    cv::Mat mask;
    cv::threshold(drawing, mask, 128, 255, cv::THRESH_BINARY);
    return mask;
}

std::vector<cv::Point> outerContourOf(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> found;
    cv::findContours(mask, found, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (found.empty()) {
        return {};
    }
    return *std::max_element(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return cv::contourArea(a) < cv::contourArea(b);
    });
}

}  // namespace

TEST(ShapeClass, AWasherSeenAtAnAngleIsNotAnOctagon) {
    // Semiejes 90 y 80: un 12,5 % de excentricidad, que es LA MEDIDA de las
    // arandelas de `arandelas-1.png` que salían octógonos (cajas de 63x71,
    // 39x45, 64x72…). Con su agujero, para que sea una arandela y no un disco.
    //
    // No se dibuja más aplastada a propósito: pasado cierto punto la
    // circunferencia deja de caber en la tolerancia y la pieza sale «de contorno
    // libre», que es otra conversación. Lo que esta prueba defiende es el caso
    // real, no el extremo.
    cv::Mat drawing(300, 340, CV_8UC1, cv::Scalar(0));
    cv::ellipse(drawing, cv::Point(170, 150), cv::Size(90, 80), 0.0, 0.0, 360.0,
                cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::ellipse(drawing, cv::Point(170, 150), cv::Size(36, 32), 0.0, 0.0, 360.0,
                cv::Scalar(0), cv::FILLED, cv::LINE_AA);
    const cv::Mat mask = maskOf(drawing);
    const auto contour = outerContourOf(mask);
    ASSERT_FALSE(contour.empty());

    const vision::ShapeClass shape = vision::classifyShape(contour, mask);
    std::printf("  [refilón] la elipse con agujero se lee como «%s» (%s)\n",
                inspection::describeShape(shape).c_str(), shape.reason.c_str());
    EXPECT_NE(shape.kind, vision::ShapeKind::Polygon)
        << "una arandela vista de refilón sale con " << shape.sides
        << " lados: son dieciséis cotas de lado y ángulo que la pieza no tiene";
    EXPECT_EQ(shape.kind, vision::ShapeKind::Ring)
        << "y tiene que leerse como lo que es, una arandela: se lee «"
        << inspection::describeShape(shape) << "»";
}

// LA OTRA MITAD, sin la cual lo de arriba se aprueba no llamando polígono a
// nada: un octógono de verdad conserva sus ocho lados.
TEST(ShapeClass, ARealOctagonKeepsItsSides) {
    cv::Mat drawing(300, 340, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point> corners;
    for (int i = 0; i < 8; ++i) {
        const double angle = 2.0 * CV_PI * i / 8 + CV_PI / 8.0;
        corners.emplace_back(static_cast<int>(170 + 110 * std::cos(angle)),
                             static_cast<int>(150 + 110 * std::sin(angle)));
    }
    std::vector<std::vector<cv::Point>> polygon{corners};
    cv::fillPoly(drawing, polygon, cv::Scalar(255), cv::LINE_AA);
    const cv::Mat mask = maskOf(drawing);
    const auto contour = outerContourOf(mask);
    ASSERT_FALSE(contour.empty());

    const vision::ShapeClass shape = vision::classifyShape(contour, mask);
    std::printf("  [refilón] el octógono de verdad se lee como «%s»\n",
                inspection::describeShape(shape).c_str());
    EXPECT_EQ(shape.kind, vision::ShapeKind::Polygon)
        << "un octógono dibujado exacto ha dejado de tener lados: la regla de la elipse "
           "se ha llevado por delante a los polígonos de verdad";
    EXPECT_EQ(shape.sides, 8);
}
