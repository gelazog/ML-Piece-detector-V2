// UNA IMAGEN DE CUATRO CANALES NO ES UNA IMAGEN GRIS.
//
// `vision/gray.h` existe porque el módulo tenía varias copias de «pasar a gris»
// y una de ellas devolvía la imagen tal cual cuando tenía cuatro canales (BGRA,
// que es lo que entrega más de una fuente de imagen). No fallaba: daba números.
//
// La auditoría de código encontró el mismo fallo vivo en otros sitios que no se
// habían pasado a `toGray`:
//
//   - `checkThresholdClipping`, `detectMarkerScale`, `sharpnessOf` y
//     `computeQualityMetrics` hacían `if (channels() == 3) convierte; else usa
//     la imagen tal cual` — o sea, con BGRA trabajaban sobre una matriz de
//     cuatro canales como si fuera gris;
//   - `findBoard` y `runTool` la RECHAZABAN: la calibración de lente decía que
//     no había tablero y la herramienta, «formato no soportado», con la imagen
//     perfectamente válida delante.
//
// La prueba es la más simple posible y por eso la más difícil de discutir: la
// MISMA escena en BGR y en BGRA tiene que dar el mismo número en cada función.
// Un canal alfa no cambia lo que hay en la foto.

#include <gtest/gtest.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

#include "inspection_editor/execution/tool_executor.h"
#include "vision/edge_segmentation.h"
#include "vision/lens_calibration.h"
#include "vision/plane_scale.h"
#include "vision/quality_metrics.h"

using namespace pci;

namespace {

// Una escena con de todo: un disco oscuro con textura para que la nitidez no
// sea cero, un marcador ArUco para la escala y fondo claro.
cv::Mat sceneBgr() {
    cv::Mat scene(480, 640, CV_8UC3, cv::Scalar(225, 220, 215));
    static const cv::aruco::Dictionary dictionary =
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
    cv::Mat marker;
    cv::aruco::generateImageMarker(dictionary, 3, 120, marker, 1);
    cv::Mat markerBgr;
    cv::cvtColor(marker, markerBgr, cv::COLOR_GRAY2BGR);
    markerBgr.copyTo(scene(cv::Rect(40, 40, 120, 120)));
    cv::circle(scene, cv::Point(420, 260), 110, cv::Scalar(60, 70, 80), cv::FILLED,
               cv::LINE_AA);
    cv::circle(scene, cv::Point(420, 260), 40, cv::Scalar(225, 220, 215), cv::FILLED,
               cv::LINE_AA);
    return scene;
}

cv::Mat toBgra(const cv::Mat& bgr) {
    cv::Mat bgra;
    cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);
    return bgra;
}

}  // namespace

TEST(BgraIsNotGray, SharpnessAndQualityReadTheSameScene) {
    const cv::Mat bgr = sceneBgr();
    const cv::Mat bgra = toBgra(bgr);
    EXPECT_DOUBLE_EQ(vision::sharpnessOf(bgra, {}), vision::sharpnessOf(bgr, {}))
        << "la nitidez cambia por tener un canal alfa";
    const auto a = vision::computeQualityMetrics(bgr, nullptr);
    const auto b = vision::computeQualityMetrics(bgra, nullptr);
    EXPECT_DOUBLE_EQ(b.sharpness, a.sharpness);
    EXPECT_DOUBLE_EQ(b.meanBrightness, a.meanBrightness)
        << "el brillo medio de una imagen BGRA sale de mezclar los cuatro canales";
}

TEST(BgraIsNotGray, TheClippingCheckReadsTheSameScene) {
    const cv::Mat bgr = sceneBgr();
    const auto a = vision::checkThresholdClipping(bgr);
    const auto b = vision::checkThresholdClipping(toBgra(bgr));
    EXPECT_DOUBLE_EQ(b.swing, a.swing);
    EXPECT_EQ(b.thresholdCutsThePiece, a.thresholdCutsThePiece);
}

TEST(BgraIsNotGray, TheMarkerIsFoundAndGivesTheSameScale) {
    const cv::Mat bgr = sceneBgr();
    const auto a = vision::detectMarkerScale(bgr, 30.0);
    const auto b = vision::detectMarkerScale(toBgra(bgr), 30.0);
    ASSERT_TRUE(a.has_value()) << "la escena de prueba ya no tiene un marcador legible";
    ASSERT_TRUE(b.has_value()) << "con un canal alfa el marcador deja de verse";
    EXPECT_DOUBLE_EQ(b->mmPerPixel, a->mmPerPixel);
}

TEST(BgraIsNotGray, TheLensBoardIsFoundInBgraToo) {
    // Un tablero de 9x6 esquinas interiores: 10x7 casillas de 40 px.
    cv::Mat board(7 * 40 + 80, 10 * 40 + 80, CV_8UC3, cv::Scalar(255, 255, 255));
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 10; ++col) {
            if ((row + col) % 2 == 0) {
                cv::rectangle(board, cv::Rect(40 + col * 40, 40 + row * 40, 40, 40),
                              cv::Scalar(0, 0, 0), cv::FILLED);
            }
        }
    }
    const vision::BoardSpec spec;  // 9x6, lo que se dibujó
    ASSERT_TRUE(vision::findBoard(board, spec).has_value())
        << "el tablero dibujado ya no se encuentra ni en BGR";
    EXPECT_TRUE(vision::findBoard(toBgra(board), spec).has_value())
        << "con un canal alfa, la calibración de lente dice que no hay tablero";
}

TEST(BgraIsNotGray, AToolMeasuresABgraFrame) {
    const cv::Mat bgr = sceneBgr();
    inspection::ToolConfig circle;
    circle.type = inspection::ToolType::Circle;
    circle.name = "Ø";
    circle.toleranceMin = 0.0;
    circle.toleranceMax = 1e9;
    inspection::CircleGeometry geometry;
    geometry.center = cv::Point2f(420.0F, 260.0F);
    geometry.radius = 110.0F;
    geometry.searchBand = 25.0F;
    geometry.rayCount = 72;
    circle.geometryJson = inspection::toJson(inspection::ToolGeometry{geometry});

    const auto a = inspection::runTool(bgr, {}, circle);
    const auto b = inspection::runTool(toBgra(bgr), {}, circle);
    ASSERT_TRUE(a.isOk() && a.value().ok) << "el círculo ya no mide la escena de prueba";
    ASSERT_TRUE(b.isOk()) << "con un canal alfa la herramienta se niega: "
                          << b.error().message;
    ASSERT_TRUE(b.value().ok) << b.value().detail;
    EXPECT_DOUBLE_EQ(b.value().measured, a.value().measured);
}
