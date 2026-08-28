// UN ÁNGULO DE 164° PRESENTADO COMO «UNA DE LAS ESQUINAS DE LA PIEZA».
//
// `ProposeOptions` lo dice desde que existe: «un ángulo entre caras solo se
// propone si está lejos de 0° y de 180°», y para eso está `minCornerAngleDeg`.
// El filtro estaba escrito en la rama que descompone el contorno en primitivas
// — y esa rama ya casi no se recorre: desde que un polígono mide por los
// vértices con los que se le reconoció, **la rama de los vértices es la que da
// hoy casi todos los ángulos**, y no miraba la opción.
//
// O sea que la opción seguía documentada, seguía teniendo su valor por defecto,
// y no gobernaba el camino por el que salen las cotas.
//
// Medido sobre el banco antes de tocar nada: **5 de 597** ángulos propuestos
// salían por encima de 160°, el peor a **163,8°**, repartidos en dos piezas. No
// es un número grande y el daño no está en el número: un «Ángulo 4 = 164°»
// presentado como «una de las 9 esquinas con las que se reconoció la pieza» es
// una cota sobre algo que no es una esquina. El operador la acepta, le pone
// banda, y la pieza buena de al lado —donde el mismo ajuste parte esa curva un
// punto más allá— da otro número y sale NG.
//
// Es el mismo fallo que los «Radio» inventados sobre una tuerca hexagonal, y la
// misma regla: lo que se propone tiene que existir en la pieza.
//
// LO QUE ESTA PRUEBA VIGILA POR EL OTRO LADO. Un filtro de esquinas es fácil de
// apretar de más, y entonces se lleva por delante las esquinas legítimas de las
// piezas con muchos lados: un dodecágono las tiene a 150°, que son esquinas de
// verdad y hay que proponerlas. Por eso aquí se comprueban las dos mitades, y
// además que la opción GOBIERNE de verdad este camino — subirla tiene que
// quitar esas esquinas de 150°, o el filtro estaría escrito otra vez donde no
// se recorre.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "inspection_editor/auto_measure.h"
#include "vision/contour_analysis.h"
#include "vision/pipeline.h"
#include "vision/shape_class.h"

using namespace pci;

namespace {

// Una figura regular de `n` lados, con las esquinas vivas. El ángulo interior
// de cada una es (n-2)·180/n: 120° el hexágono, 150° el de doce.
cv::Mat regularPolygon(int n, int radius) {
    cv::Mat canvas(radius * 2 + 80, radius * 2 + 80, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point> points;
    for (int i = 0; i < n; ++i) {
        const double angle = 2.0 * CV_PI * i / n;
        points.emplace_back(cvRound(radius + 40 + radius * std::cos(angle)),
                            cvRound(radius + 40 + radius * std::sin(angle)));
    }
    cv::fillPoly(canvas, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255),
                 cv::LINE_AA);
    return canvas;
}

std::vector<double> anglesProposedFor(const cv::Mat& mask,
                                      const inspection::ProposeOptions& options) {
    cv::Mat colour;
    cv::cvtColor(mask, colour, cv::COLOR_GRAY2BGR);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        return {};
    }
    const cv::Moments m = cv::moments(contours.front());
    const vision::Fixture fixture{
        {static_cast<float>(m.m10 / m.m00), static_cast<float>(m.m01 / m.m00)}, 0.0};
    std::vector<double> angles;
    for (const auto& p :
         inspection::proposeTools(colour, mask, fixture, options, 0.0, nullptr)) {
        if (p.config.name.rfind("Ángulo", 0) == 0) {
            angles.push_back(p.measured);
        }
    }
    return angles;
}

}  // namespace

TEST(AngleProposalsAreCorners, NoPieceInTheBankIsOfferedAnAngleThatIsNotACorner) {
    const std::vector<std::string> photos = {
        "producto-tuercas-prueba.jpg", "Producto_Tuerca_Liv_02.jpg", "arandelas-1.png",
        "arandelas-2.png", "arandelas-3.jpg", "arandelas-4.png", "arandelas-5.png",
        "engranaje-1.png", "engranajes-1.jpg", "rosca-1.png", "tornillo-1.png",
        "tornillo-2.png", "tornillo-ojo-3.png", "tornillo-ojo-4.png",
        "tornillo-ojo-5.png", "tornillos-1.png"};

    int pieces = 0;
    int angles = 0;
    int notCorners = 0;
    double flattest = 0.0;
    std::string worstPhoto;
    for (const auto& photo : photos) {
        const cv::Mat image =
            cv::imread("C:/Users/furro/Pictures/IMG-MC/" + photo, cv::IMREAD_COLOR);
        if (image.empty()) {
            GTEST_SKIP() << "sin banco de fotos";
        }
        vision::PipelineConfig config;
        config.segmentation.recoverHighlightsBy = 12;
        auto all = vision::analyzeFrames(image, config);
        if (!all.isOk()) {
            continue;
        }
        for (const auto& piece : all.value()) {
            const cv::Mat mask =
                vision::pieceMaskWithHoles(image, piece.mask, config.segmentation);
            ++pieces;
            // Con el tope subido: lo que se comprueba es qué GENERA el
            // proponedor, no qué sobrevive al recorte de doce.
            inspection::ProposeOptions wide;
            wide.maxProposals = 100;
            for (const auto& p :
                 inspection::proposeTools(image, mask, piece.fixture, wide, 0.0, nullptr)) {
                if (p.config.name.rfind("Ángulo", 0) != 0) {
                    continue;
                }
                ++angles;
                if (p.measured > 180.0 - wide.minCornerAngleDeg ||
                    p.measured < wide.minCornerAngleDeg) {
                    ++notCorners;
                }
                if (p.measured > flattest) {
                    flattest = p.measured;
                    worstPhoto = photo;
                }
            }
        }
    }

    std::printf("  [esquinas] %d piezas, %d ángulos propuestos, %d que no son esquina; "
                "el más plano %.2f° (%s)\n",
                pieces, angles, notCorners, flattest, worstPhoto.c_str());

    // Que el barrido esté mirando de verdad.
    ASSERT_GT(angles, 300)
        << "el banco apenas propone ángulos: esta prueba no comprueba lo que cree";
    EXPECT_EQ(notCorners, 0)
        << "se propone un ángulo sobre una junta que no es una esquina. Va presentado "
           "como «una de las esquinas con las que se reconoció la pieza», así que se "
           "acepta, se le pone banda, y la pieza buena de al lado da otro número";
}

TEST(AngleProposalsAreCorners, TheCornersOfARealPolygonAreStillOffered) {
    // LA OTRA MITAD. Un filtro de esquinas apretado de más se lleva las de las
    // piezas con muchos lados, que son esquinas de verdad: el dodecágono las
    // tiene a 150°, o sea a 30° de estar recto, y el suelo está en 20°.
    const std::vector<std::pair<int, double>> shapes{{4, 90.0}, {6, 120.0}, {12, 150.0}};
    for (const auto& [sides, interior] : shapes) {
        const cv::Mat mask = regularPolygon(sides, sides <= 6 ? 150 : 200);
        inspection::ProposeOptions wide;
        wide.maxProposals = 100;
        const auto angles = anglesProposedFor(mask, wide);
        std::printf("  [esquinas] polígono de %2d lados -> %d ángulos propuestos\n", sides,
                    static_cast<int>(angles.size()));
        EXPECT_EQ(static_cast<int>(angles.size()), sides)
            << "a un polígono de " << sides << " lados se le proponen " << angles.size()
            << " ángulos: el filtro de esquinas se está llevando esquinas de verdad";
        for (const double measured : angles) {
            EXPECT_NEAR(measured, interior, 6.0)
                << "un polígono regular de " << sides << " lados tiene las esquinas a "
                << interior << "° y se propone una de " << measured;
        }
    }
}

TEST(AngleProposalsAreCorners, TheOptionActuallyGovernsThisPath) {
    // Y QUE EL FILTRO ESTÉ DONDE SE RECORRE. Este es el fallo exacto que se
    // arregló: `minCornerAngleDeg` existía, estaba documentada y gobernaba
    // solamente la rama de las primitivas, mientras los ángulos salían por la
    // de los vértices.
    //
    // Con el suelo por encima de lo que gira el dodecágono en cada esquina
    // (30°), sus doce ángulos tienen que desaparecer. Si siguen saliendo, el
    // filtro se ha vuelto a escribir en el camino que no se recorre.
    const cv::Mat mask = regularPolygon(12, 200);

    inspection::ProposeOptions asUsual;
    asUsual.maxProposals = 100;
    const auto before = anglesProposedFor(mask, asUsual);

    inspection::ProposeOptions demanding = asUsual;
    demanding.minCornerAngleDeg = 45.0;  // por encima de los 30° que gira
    const auto after = anglesProposedFor(mask, demanding);

    std::printf("  [esquinas] dodecágono: con el suelo en 20° salen %d ángulos, con 45° "
                "salen %d\n",
                static_cast<int>(before.size()), static_cast<int>(after.size()));
    EXPECT_EQ(static_cast<int>(before.size()), 12);
    EXPECT_TRUE(after.empty())
        << "subir `minCornerAngleDeg` no quita ningún ángulo: la opción no gobierna el "
           "camino por el que salen, que es justo el fallo que esto vino a cerrar";
}
