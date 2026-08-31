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

#include <algorithm>
#include <cmath>
#include <string>
#include <cstdio>
#include <vector>

#include "inspection_editor/piece_report.h"
#include "ui/main_window.h"
#include "vision/shape_class.h"

#include <QAction>

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

// Y SE DICE, PORQUE LOS DOS NÚMEROS QUE PUBLICA VAN AL PARTE.
//
// Reconocerla como arandela era la mitad del trabajo. La otra mitad: una pieza
// redonda vista de refilón publica dos números equivocados a la vez.
//
//   - El **diámetro** sale del círculo ajustado, que sobre una elipse se queda
//     entre los dos ejes. Medido sobre las 70 piezas redondas del banco, el Ø se
//     queda hasta un **12,8 %** por debajo del eje mayor, y la cuenta sigue a la
//     excentricidad: 1,05 → 2,4 % · 1,16 → 7,1 % · 1,32 → 12,8 %.
//   - La **redondez** mide la inclinación de la cámara y no la pieza: arandelas
//     que son redondas salen con 4 a 9 px de falta de redondez.
//
// No se corrige el número por dentro a propósito: el diámetro de una elipse no
// está definido, y elegir el eje mayor sería decidir por el operador que su
// pieza es redonda y está torcida, cuando puede ser ovalada de verdad. Lo que sí
// se puede es decirlo con la cifra y con el arreglo, que es físico.
//
// El listón (1,10) deja el aviso en 7 de las 70 piezas redondas del banco. Un
// aviso que saltara en las setenta se aprende a ignorar en dos días, y eso ya
// está escrito en este proyecto más de una vez.
TEST(PieceReport, ARoundPieceSeenAtAnAngleSaysSo) {
    // La misma arandela de arriba, con 12,5 % de excentricidad.
    cv::Mat drawing(300, 340, CV_8UC1, cv::Scalar(0));
    cv::ellipse(drawing, cv::Point(170, 150), cv::Size(90, 80), 0.0, 0.0, 360.0,
                cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::ellipse(drawing, cv::Point(170, 150), cv::Size(36, 32), 0.0, 0.0, 360.0,
                cv::Scalar(0), cv::FILLED, cv::LINE_AA);
    const cv::Mat mask = maskOf(drawing);

    // La imagen para medir: la pieza clara sobre fondo oscuro, como la máscara.
    const auto report = inspection::measureWholePiece(drawing, mask, {}, 0.0,
                                                      inspection::LengthUnit::Auto,
                                                      drawing.size());
    ASSERT_TRUE(report.ok) << report.problem;
    for (const auto& warning : report.warnings) {
        std::printf("  [refilón] aviso: %s\n", warning.c_str());
    }
    const bool saysIt = std::any_of(
        report.warnings.begin(), report.warnings.end(), [](const std::string& warning) {
            return warning.find("elipse") != std::string::npos;
        });
    EXPECT_TRUE(saysIt)
        << "la pieza se ve de refilón, publica un diámetro corto y una redondez que mide "
           "la inclinación, y el informe no lo dice";

    // Y una pieza redonda de verdad no lleva ese aviso: si saltara siempre, se
    // aprendería a ignorar y no serviría cuando hace falta.
    cv::Mat flat(300, 340, CV_8UC1, cv::Scalar(0));
    cv::circle(flat, cv::Point(170, 150), 85, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::circle(flat, cv::Point(170, 150), 34, cv::Scalar(0), cv::FILLED, cv::LINE_AA);
    const auto straight = inspection::measureWholePiece(flat, maskOf(flat), {}, 0.0,
                                                        inspection::LengthUnit::Auto,
                                                        flat.size());
    ASSERT_TRUE(straight.ok) << straight.problem;
    const bool quiet = std::none_of(
        straight.warnings.begin(), straight.warnings.end(), [](const std::string& warning) {
            return warning.find("elipse") != std::string::npos;
        });
    EXPECT_TRUE(quiet) << "el aviso salta sobre una arandela dibujada redonda: así se "
                          "aprende a ignorarlo";
}

// Y EL ARREGLO QUE NOMBRA EL AVISO TIENE QUE SER EL QUE SIRVE.
//
// En esta aplicación hay TRES cosas que se llaman «calibrar», y la cabecera de
// `vision/lens_calibration.h` avisa de la confusión con todas las letras:
//
//   - la ESCALA (mm por píxel) es un número, y no sabe de inclinaciones;
//   - el MARCADOR ArUco corrige la PERSPECTIVA — es una homografía;
//   - el TABLERO de ajedrez corrige la LENTE, que no lleva rectas a rectas.
//     Ninguna homografía deshace eso, y ninguna corrección de lente endereza una
//     perspectiva.
//
// La primera versión del aviso de arriba mandaba a «calibrar el plano con el
// tablero», que es justo el que no vale para esto. Sonaba bien y mandaba a otro
// sitio — y un consejo equivocado gasta el tiempo del operador y encima le deja
// creyendo que ya lo ha arreglado.
//
// Esta prueba comprueba las dos mitades: que el aviso nombra el marcador, y que
// el camino que nombra EXISTE en los menús. Un aviso que manda a un sitio que no
// está es peor que no avisar.
TEST(PieceReport, TheTiltWarningNamesTheCalibrationThatFixesIt) {
    cv::Mat drawing(300, 340, CV_8UC1, cv::Scalar(0));
    cv::ellipse(drawing, cv::Point(170, 150), cv::Size(90, 80), 0.0, 0.0, 360.0,
                cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::ellipse(drawing, cv::Point(170, 150), cv::Size(36, 32), 0.0, 0.0, 360.0,
                cv::Scalar(0), cv::FILLED, cv::LINE_AA);
    const auto report =
        inspection::measureWholePiece(drawing, maskOf(drawing), {}, 0.0,
                                      inspection::LengthUnit::Auto, drawing.size());
    ASSERT_TRUE(report.ok) << report.problem;
    std::string tilt;
    for (const auto& warning : report.warnings) {
        if (warning.find("elipse") != std::string::npos) {
            tilt = warning;
        }
    }
    ASSERT_FALSE(tilt.empty()) << "no hay aviso de perspectiva que comprobar";
    EXPECT_NE(tilt.find("ArUco"), std::string::npos)
        << "el aviso no nombra el marcador, que es lo único que corrige la perspectiva: «"
        << tilt << "»";

    // Y el camino que nombra existe. Se busca la acción por su texto en los
    // menús de la ventana real, no en una lista escrita aparte: una lista aparte
    // se queda vieja el día que alguien renombre la entrada.
    pci::ui::MainWindow window;
    bool found = false;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->text().contains(QStringLiteral("ArUco"))) {
            found = true;
            std::printf("  [refilón] el aviso manda a «%s»\n",
                        action->text().toStdString().c_str());
        }
    }
    EXPECT_TRUE(found)
        << "el aviso manda a «Escala por marcador ArUco» y esa entrada no está en ningún "
           "menú: mandar a un sitio que no existe es peor que no avisar";
}
