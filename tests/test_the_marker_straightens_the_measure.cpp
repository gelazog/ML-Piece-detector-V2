// EL PROGRAMA RECOMENDABA EL MARCADOR PARA ENDEREZAR UNA PIEZA TORCIDA Y LUEGO
// PUBLICABA EL DIÁMETRO SIN ENDEREZAR.
//
// El informe avisa, cuando una pieza redonda se ve de refilón, de que el
// diámetro sale corto y manda a corregir la perspectiva con un marcador ArUco.
// Esta prueba nació de comprobar ese consejo de punta a punta, y encontró la
// mitad que faltaba:
//
//   - las longitudes entre DOS PUNTOS ya se median mapeando los dos puntos al
//     plano (`fmtLenPts`), así que el marcador las arreglaba;
//   - el diámetro del Círculo sale de un RADIO, no de dos puntos, y se convertía
//     con la escala constante. Sobre un plano inclinado esa escala no vale — es
//     exactamente lo que el marcador viene a arreglar.
//
// Medido sobre esta misma escena antes de tocar nada: un disco de 60 mm salía
// 68,09 mm con inclinación suave y 78,21 mm con inclinación fuerte.
//
// La corrección no convierte el radio: mapear un radio no significa nada, porque
// la perspectiva no conserva distancias. Lo que sí se puede mapear son los
// PUNTOS DEL BORDE, uno a uno, y en el plano el borde vuelve a ser una
// circunferencia — así que se ajusta allí.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

#include <array>
#include <cstdio>
#include <string>

#include "inspection_editor/execution/tool_executor.h"
#include "vision/plane_scale.h"

using namespace pci;

namespace {

// Una escena de frente: el marcador de 40 mm (200 px) y un disco de 60 mm
// (300 px) en el mismo plano. Con 0,2 mm/px, los números salen redondos.
cv::Mat flatScene() {
    cv::Mat scene(700, 900, CV_8UC1, cv::Scalar(255));
    static const cv::aruco::Dictionary dictionary =
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
    cv::Mat marker;
    cv::aruco::generateImageMarker(dictionary, 7, 200, marker, 1);
    marker.copyTo(scene(cv::Rect(60, 60, 200, 200)));
    cv::circle(scene, cv::Point(560, 380), 150, cv::Scalar(40), cv::FILLED, cv::LINE_AA);
    return scene;
}

// La misma escena vista con la cámara inclinada: la parte de arriba se estrecha.
cv::Mat tiltedBy(double howMuch) {
    const cv::Mat flat = flatScene();
    const float w = static_cast<float>(flat.cols - 1);
    const float h = static_cast<float>(flat.rows - 1);
    const std::array<cv::Point2f, 4> from = {cv::Point2f(0, 0), cv::Point2f(w, 0),
                                             cv::Point2f(w, h), cv::Point2f(0, h)};
    const std::array<cv::Point2f, 4> to = {
        cv::Point2f(static_cast<float>(w * howMuch / 2), 0),
        cv::Point2f(static_cast<float>(w - w * howMuch / 2), 0), cv::Point2f(w, h),
        cv::Point2f(0, h)};
    cv::Mat tilted;
    cv::warpPerspective(flat, tilted, cv::getPerspectiveTransform(from.data(), to.data()),
                        flat.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                        cv::Scalar(255));
    return tilted;
}

// Los mm que el operador lee en el detalle: «D=60.02 mm (300.1 px), R=…».
double millimetresInTheText(const std::string& detail) {
    const std::size_t start = detail.find("D=");
    if (start == std::string::npos) {
        return -1.0;
    }
    return std::atof(detail.c_str() + start + 2);
}

}  // namespace

TEST(MarkerScale, ItStraightensTheDiameterAndNotOnlyTheRulers) {
    constexpr double kMarkerMm = 40.0;
    constexpr double kDiscMm = 60.0;

    // El círculo se traza donde está el disco, con banda de búsqueda de sobra
    // para que el borde caiga dentro aunque la perspectiva lo mueva.
    inspection::ToolConfig circle;
    circle.type = inspection::ToolType::Circle;
    circle.name = "Ø";
    circle.toleranceMin = 0.0;
    circle.toleranceMax = 1e9;

    for (const double tilt : {0.0, 0.15, 0.30}) {
        const cv::Mat scene = tiltedBy(tilt);
        const auto scale = vision::detectMarkerScale(scene, kMarkerMm);
        ASSERT_TRUE(scale.has_value())
            << "con inclinación " << tilt << " ya no se ve el marcador: sin él esta "
               "prueba no puede comprobar nada";

        // La geometría se traza sobre la imagen inclinada, buscando el borde
        // alrededor de donde está el disco.
        inspection::CircleGeometry geometry;
        geometry.center = cv::Point2f(560.0F, 380.0F);
        geometry.radius = 150.0F;
        geometry.searchBand = 60.0F;
        geometry.rayCount = 72;
        circle.geometryJson = inspection::toJson(inspection::ToolGeometry{geometry});

        const auto plain = inspection::runTool(scene, {}, circle, scale->mmPerPixel,
                                               inspection::LengthUnit::Millimeters);
        const auto onThePlane =
            inspection::runTool(scene, {}, circle, scale->mmPerPixel,
                                inspection::LengthUnit::Millimeters, scale->imageToMm);
        ASSERT_TRUE(plain.isOk() && plain.value().ok) << plain.value().detail;
        ASSERT_TRUE(onThePlane.isOk() && onThePlane.value().ok)
            << onThePlane.value().detail;

        const double byScale = millimetresInTheText(plain.value().detail);
        const double byPlane = millimetresInTheText(onThePlane.value().detail);
        std::printf("  [marcador] inclinación %.2f (calidad %.3f): por escala %6.2f mm, "
                    "por el plano %6.2f mm — verdad %.2f\n",
                    tilt, scale->quality, byScale, byPlane, kDiscMm);

        // Con la cámara de frente los dos caminos tienen que coincidir: si no,
        // la corrección estaría metiendo error donde no lo había.
        if (tilt < 0.01) {
            EXPECT_NEAR(byPlane, byScale, 0.5)
                << "de frente, corregir la perspectiva cambia el número";
        }
        // Y con la cámara inclinada, el plano tiene que acertar.
        EXPECT_NEAR(byPlane, kDiscMm, 1.0)
            << "con inclinación " << tilt << " el diámetro por el plano da " << byPlane
            << " mm para un disco de " << kDiscMm;
        if (tilt > 0.2) {
            // Y la escala constante NO. Sin esto, la prueba pasaría el día que
            // las dos vías se volvieran la misma y no habría corrección ninguna.
            EXPECT_GT(std::abs(byScale - kDiscMm), 5.0)
                << "la escala constante ya acierta con la cámara inclinada: entonces la "
                   "corrección del plano no está haciendo nada y sobra";
        }
    }
}

// Y EL AVISO DE «CÁMARA INCLINADA» TIENE QUE DECIR LO QUE PASA AHORA.
//
// El texto decía «los diámetros salen cortos», y era cierto mientras los mm
// salían de una escala constante. Desde que el Círculo se ajusta sobre el plano
// del marcador ya NO salen cortos con la homografía puesta —el disco de 60 mm de
// la prueba de arriba pasa de 71,7 a 60,3—, así que repetirlo sería avisar de
// algo que la propia herramienta acaba de arreglar. Un aviso que dice lo que no
// pasa enseña a no leer los avisos.
//
// Las dos mitades: con el plano puesto, el aviso habla de precisión y no de un
// error que ya no está; sin el plano, sigue diciendo que salen cortos y además
// dónde se enciende la corrección.
TEST(MarkerScale, TheTiltWarningSaysWhetherItIsBeingCorrected) {
    // Inclinación fuerte, para que la calidad del marcador baje del listón.
    const cv::Mat scene = tiltedBy(0.60);
    const auto scale = vision::detectMarkerScale(scene, 40.0);
    ASSERT_TRUE(scale.has_value()) << "sin marcador no hay calidad que juzgar";
    ASSERT_LT(scale->quality, 0.75)
        << "con esta inclinación el marcador todavía se ve bien (calidad "
        << scale->quality << "): sube la inclinación o esta prueba no comprueba nada";

    inspection::ToolConfig circle;
    circle.type = inspection::ToolType::Circle;
    circle.name = "Ø";
    circle.toleranceMin = 0.0;
    circle.toleranceMax = 1e9;
    // La geometría se traza SOBRE EL DISCO YA DEFORMADO, buscándolo en la
    // imagen en vez de escribir aquí un centro y un radio: con la cámara muy
    // inclinada el disco se va de donde estaba, y una circunferencia trazada de
    // memoria se queda sin borde que medir (6 rayos de 72, y la prueba falla por
    // el sitio equivocado).
    cv::Mat mask;
    cv::threshold(scene, mask, 128, 255, cv::THRESH_BINARY_INV);
    std::vector<std::vector<cv::Point>> blobs;
    cv::findContours(mask, blobs, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    const std::vector<cv::Point>* disc = nullptr;
    double biggest = 0.0;
    for (const auto& blob : blobs) {
        const double area = cv::contourArea(blob);
        // A la derecha del marcador, que también es oscuro.
        if (area > biggest && cv::boundingRect(blob).x > 300) {
            biggest = area;
            disc = &blob;
        }
    }
    ASSERT_NE(disc, nullptr) << "no se encuentra el disco en la escena inclinada";
    const cv::RotatedRect seen = cv::fitEllipse(*disc);

    inspection::CircleGeometry geometry;
    geometry.center = seen.center;
    geometry.radius = (seen.size.width + seen.size.height) / 4.0F;
    geometry.searchBand = geometry.radius * 0.6F;
    geometry.rayCount = 72;
    circle.geometryJson = inspection::toJson(inspection::ToolGeometry{geometry});

    const auto withPlane = inspection::runTool(
        scene, {}, circle, scale->mmPerPixel, inspection::LengthUnit::Millimeters,
        scale->imageToMm, nullptr, scale->quality);
    const auto withoutPlane =
        inspection::runTool(scene, {}, circle, scale->mmPerPixel,
                            inspection::LengthUnit::Millimeters, cv::Mat(), nullptr,
                            scale->quality);
    ASSERT_TRUE(withPlane.isOk() && withPlane.value().ok) << withPlane.value().detail;
    ASSERT_TRUE(withoutPlane.isOk() && withoutPlane.value().ok)
        << withoutPlane.value().detail;
    std::printf("  [marcador] con plano:  %s\n", withPlane.value().detail.c_str());
    std::printf("  [marcador] sin plano:  %s\n", withoutPlane.value().detail.c_str());

    EXPECT_NE(withPlane.value().detail.find("inclinada"), std::string::npos)
        << "con la cámara muy inclinada no se avisa de nada";
    EXPECT_EQ(withPlane.value().detail.find("no es de fiar"), std::string::npos)
        << "se sigue avisando de que el diámetro no es de fiar cuando ya se está "
           "corrigiendo sobre el plano: «" << withPlane.value().detail << "»";
    EXPECT_NE(withoutPlane.value().detail.find("no es de fiar"), std::string::npos)
        << "sin corregir, el aviso tiene que seguir diciendo lo que pasa: «"
        << withoutPlane.value().detail << "»";
    EXPECT_NE(withoutPlane.value().detail.find("ArUco"), std::string::npos)
        << "el aviso dice el problema y no dónde se arregla: «"
        << withoutPlane.value().detail << "»";

    // Y LA DESVIACIÓN RADIAL TAMBIÉN SE CORRIGE, o la misma línea mezclaría dos
    // sistemas: un diámetro medido en el plano junto a una falta de redondez
    // medida en la imagen. El disco está dibujado perfecto, así que en el plano
    // su desviación tiene que ser pequeña; en la imagen es la perspectiva.
    const auto millimetresAfter = [](const std::string& detail, const char* label) {
        const std::size_t at = detail.find(label);
        return at == std::string::npos
                   ? -1.0
                   : std::atof(detail.c_str() + at + std::string(label).size());
    };
    const double correctedRoundness =
        millimetresAfter(withPlane.value().detail, "desv. radial máx.=");
    const double rawRoundness =
        millimetresAfter(withoutPlane.value().detail, "desv. radial máx.=");
    std::printf("  [marcador] desviación radial: %.2f mm sin corregir, %.2f corregida\n",
                rawRoundness, correctedRoundness);
    ASSERT_GT(rawRoundness, 0.0);
    EXPECT_LT(correctedRoundness, rawRoundness / 4.0)
        << "el disco está dibujado perfecto y, con el plano puesto, sigue publicando "
        << correctedRoundness
        << " mm de falta de redondez: eso es la perspectiva sin deshacer, no la pieza";
}
