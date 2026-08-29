// RODEAR A MANO UNA PIEZA QUE LA DETECCIÓN NO VE — Y QUITAR LA QUE NO EXISTE.
//
// Petición de uso: «añadir pieza dibujando un contorno manualmente, y que
// detecte o intente detectar la pieza (igual para quitarlo), por si en un lote
// no la detecta, o detecta algo que no debe».
//
// Las dos mitades pasan de verdad, y la segunda está medida sobre el banco:
// `arandelas-2.png` es una foto de catálogo con rótulos impresos, y la
// aplicación los detecta como piezas —una de ellas sale como «polígono de 9
// lados» de 168×53 px, que es la palabra «Flat Washers»—. Eso es exactamente
// «detecta algo que no debe», y hasta ahora la única forma de quitarlo era
// pintar encima con el pincel, píxel a píxel.
//
// LO QUE NO SE HACE, Y ES LA DECISIÓN QUE GOBIERNA ESTO. El trazo NO se toma
// como si fuera la pieza. Un contorno dibujado a pulso no se puede medir: el
// ratón no sigue el borde con precisión de píxel, así que el diámetro que
// saliera de ahí sería el pulso del operador y llegaría a la plantilla con su
// tolerancia, indistinguible de una medida de verdad.
//
// El trazo dice DÓNDE MIRAR. Dentro se vuelve a segmentar con el fondo que haya
// ahí, que es el mismo truco de la zona de trabajo: una pieza que el umbral
// global se deja fuera suele aparecer en cuanto el umbral se calcula con lo que
// la rodea.
//
// Y cuando ahí dentro no hay nada que detectar, se DICE. Callar y devolver el
// trazo como si fuera un borde medido es el fallo que hay que evitar.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "vision/outlined_piece.h"
#include "vision/pipeline.h"
#include "vision/shape_class.h"

using namespace pci;

namespace {

// Dos piezas sobre fondo claro, una de ellas MUCHO menos contrastada que la
// otra. Es el caso de la queja: en un lote, la pieza mate o en sombra se cae del
// umbral global mientras las demás salen.
cv::Mat trayWithAFaintPiece() {
    cv::Mat frame(300, 500, CV_8UC1, cv::Scalar(235));
    cv::rectangle(frame, cv::Rect(60, 90, 120, 120), cv::Scalar(30), cv::FILLED);
    cv::rectangle(frame, cv::Rect(300, 100, 100, 100), cv::Scalar(205), cv::FILLED);
    return frame;
}

// Un trazo a pulso alrededor de un rectángulo: ni sigue el borde ni es un
// rectángulo. Es lo que sale de rodear una pieza con el ratón.
std::vector<cv::Point> sloppyOutlineAround(const cv::Rect& box) {
    const int slack = 22;
    return {{box.x - slack, box.y - slack + 6},
            {box.x + box.width / 2, box.y - slack - 4},
            {box.x + box.width + slack, box.y - slack + 8},
            {box.x + box.width + slack - 5, box.y + box.height / 2},
            {box.x + box.width + slack, box.y + box.height + slack},
            {box.x + box.width / 3, box.y + box.height + slack + 5},
            {box.x - slack, box.y + box.height + slack - 6},
            {box.x - slack + 4, box.y + box.height / 2}};
}

cv::Rect boxOfMask(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> blobs;
    cv::findContours(mask, blobs, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    return blobs.empty() ? cv::Rect() : cv::boundingRect(blobs.front());
}

}  // namespace

TEST(OutlinedPiece, TheEdgeComesFromTheImageAndNotFromTheHandThatDrewIt) {
    // EL PUNTO ENTERO. El trazo va 22 px por fuera y ondulado; lo que tiene que
    // salir es el rectángulo de verdad, no el trazo.
    const cv::Mat frame = trayWithAFaintPiece();
    const cv::Rect truth(300, 100, 100, 100);
    const auto outline = sloppyOutlineAround(truth);

    const auto found = vision::pieceInsideOutline(frame, outline);
    const cv::Rect got = boxOfMask(found.mask);
    std::printf("  [trazo] trazo %dx%d -> detectado %dx%d (verdad %dx%d), ocupa el %.0f %%\n",
                cv::boundingRect(outline).width, cv::boundingRect(outline).height, got.width,
                got.height, truth.width, truth.height, 100.0 * found.fillFraction);

    ASSERT_TRUE(found.detected)
        << "no se ha detectado el borde dentro del trazo, así que la pieza mediría lo que "
           "dibujó el ratón: " << found.why;
    // Dos píxeles de holgura: el borde de la máscara y el del rectángulo pueden
    // no caer en el mismo píxel, pero 22 de diferencia serían el trazo.
    EXPECT_NEAR(got.width, truth.width, 3)
        << "el ancho sale del trazo (" << cv::boundingRect(outline).width << " px) y no de "
           "la pieza";
    EXPECT_NEAR(got.height, truth.height, 3) << "el alto sale del trazo y no de la pieza";
}

TEST(OutlinedPiece, ItFindsThePieceTheGlobalThresholdMisses) {
    // Y que eso sirva PARA ALGO: la pieza que se rodea es justo la que el
    // análisis normal no ve. Sin esta comprobación, la de arriba podría estar
    // rodeando una pieza que ya salía sola.
    const cv::Mat frame = trayWithAFaintPiece();
    cv::Mat colour;
    cv::cvtColor(frame, colour, cv::COLOR_GRAY2BGR);
    vision::PipelineConfig config;
    auto all = vision::analyzeFrames(colour, config);
    ASSERT_TRUE(all.isOk()) << all.error().message;
    std::printf("  [trazo] el análisis normal ve %d pieza(s) de las dos que hay\n",
                static_cast<int>(all.value().size()));
    ASSERT_EQ(all.value().size(), 1U)
        << "las dos piezas se detectan solas: esta prueba no está midiendo el caso que "
           "dice —hay que hacer la segunda menos contrastada";

    const cv::Rect faint(300, 100, 100, 100);
    const auto found = vision::pieceInsideOutline(frame, sloppyOutlineAround(faint));
    EXPECT_TRUE(found.detected) << found.why;
    const cv::Rect got = boxOfMask(found.mask);
    EXPECT_NEAR(got.width, faint.width, 3);
    EXPECT_NEAR(got.height, faint.height, 3);
}

TEST(OutlinedPiece, WhenThereIsNothingToDetectItSaysSoInsteadOfPretending) {
    // LA MITAD HONRADA. Sobre fondo liso no hay borde que encontrar. Se devuelve
    // el trazo —marcar la pieza a mano sigue valiendo para contarla— pero
    // `detected` viene en falso y el motivo lo explica, porque de ahí no pueden
    // salir cotas.
    cv::Mat plain(300, 300, CV_8UC1, cv::Scalar(200));
    const auto outline = sloppyOutlineAround(cv::Rect(100, 100, 80, 80));
    const auto found = vision::pieceInsideOutline(plain, outline);
    std::printf("  [trazo] sobre fondo liso -> detectado=%d, «%s»\n",
                static_cast<int>(found.detected), found.why.c_str());

    EXPECT_FALSE(found.detected)
        << "dice haber detectado un borde donde no hay ninguno: entonces las cotas de esa "
           "pieza serían el pulso de quien la dibujó, sin que nada lo advierta";
    EXPECT_FALSE(found.why.empty()) << "no se detecta nada y no se dice por qué";
    EXPECT_GT(cv::countNonZero(found.mask), 0)
        << "se pierde también el trazo: rodear la pieza a mano tiene que valer al menos "
           "para que la pieza exista";
}

TEST(OutlinedPiece, TheTraceIsNeverTakenAsAMeasuredEdgeWhenItFillsItself) {
    // El otro lado del mismo umbral. Si dentro del trazo «se detecta» casi todo
    // el trazo, lo que ha pasado es que no había dos niveles que separar: el
    // borde saldría del trazo. Sin este límite, rodear apretado una pieza
    // devolvería el trazo con `detected` en cierto — la mentira más fácil de
    // colar, porque el resultado se parece a lo que se esperaba.
    cv::Mat frame(300, 300, CV_8UC1, cv::Scalar(235));
    cv::rectangle(frame, cv::Rect(100, 100, 100, 100), cv::Scalar(30), cv::FILLED);
    // Un trazo CEÑIDO al borde de la pieza: no deja fondo dentro.
    const std::vector<cv::Point> tight{{101, 101}, {199, 101}, {199, 199}, {101, 199}};
    const auto found = vision::pieceInsideOutline(frame, tight);
    std::printf("  [trazo] trazo ceñido -> ocupa el %.0f %%, detectado=%d\n",
                100.0 * found.fillFraction, static_cast<int>(found.detected));
    EXPECT_GT(found.fillFraction, vision::kOutlineMaxFill)
        << "el trazo ceñido ya no llena su propia zona: este caso dejó de ser el que "
           "vigila";
    EXPECT_FALSE(found.detected)
        << "un trazo que se llena a sí mismo se da por borde medido: eso es devolver el "
           "pulso del operador con aspecto de medida";
}

TEST(OutlinedPiece, WhatIsNotAPieceCanBeDroppedWithTheSameGesture) {
    // LA OTRA MITAD DE LA PETICIÓN, medida sobre el banco.
    //
    // `arandelas-2.png` es una foto de catálogo: arandelas, rótulos impresos y
    // una regla. De todo eso la aplicación detecta UNA sola cosa, y no es una
    // arandela — es un rótulo de 168×53 px, casi tres veces más largo que ancho,
    // que además sale clasificado como «polígono de 9 lados». O sea que la única
    // pieza que da esa foto es algo que no debería contarse.
    //
    // Marcarla como fondo tiene que quitarla, y sin llevarse nada más.
    const cv::Mat image =
        cv::imread("C:/Users/furro/Pictures/IMG-MC/arandelas-2.png", cv::IMREAD_COLOR);
    if (image.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    vision::PipelineConfig config;
    config.segmentation.recoverHighlightsBy = 12;
    auto before = vision::analyzeFrames(image, config);
    ASSERT_TRUE(before.isOk());
    ASSERT_FALSE(before.value().empty())
        << "en esta foto ya no se detecta nada: esta prueba no está midiendo el caso que "
           "dice";

    // La más alargada: en esta foto, un rótulo. Se elige por su forma y no por
    // su posición para que la prueba no se caiga si la foto se recorta.
    const vision::PieceAnalysis* label = nullptr;
    double worstAspect = 0.0;
    for (const auto& piece : before.value()) {
        const cv::RotatedRect box = cv::minAreaRect(piece.contour.points);
        const double side = std::min(box.size.width, box.size.height);
        const double aspect = side > 0.0 ? std::max(box.size.width, box.size.height) / side : 0.0;
        if (aspect > worstAspect) {
            worstAspect = aspect;
            label = &piece;
        }
    }
    ASSERT_NE(label, nullptr);
    std::printf("  [trazo] %d pieza(s) detectada(s); la más alargada tiene una "
                "relación de %.1f:1\n",
                static_cast<int>(before.value().size()), worstAspect);
    EXPECT_GT(worstAspect, 2.0)
        << "lo que se detecta en esta foto ya no es un rótulo alargado: si la "
           "detección ha mejorado hasta ver las arandelas, este ejemplo hay que "
           "volver a elegirlo";

    // El gesto: se rodea y se marca como fondo. Con holgura, como sale de un
    // trazo a mano.
    cv::Rect box = cv::boundingRect(label->contour.points);
    box.x -= 6;
    box.y -= 6;
    box.width += 12;
    box.height += 12;
    box &= cv::Rect(0, 0, image.cols, image.rows);
    config.forceBackground = cv::Mat(image.size(), CV_8UC1, cv::Scalar(0));
    cv::rectangle(config.forceBackground, box, cv::Scalar(255), cv::FILLED);

    auto after = vision::analyzeFrames(image, config);
    // Quedarse sin ninguna pieza NO es un fallo aquí, es la respuesta correcta:
    // lo único que esta foto daba era un rótulo, y el operador acaba de decir
    // que no es una pieza. `analyzeFrames` devuelve error cuando no hay nada, y
    // eso se traduce a cero en vez de tomarse por avera.
    const std::size_t left = after.isOk() ? after.value().size() : 0U;
    std::printf("  [trazo] tras descartarlo quedan %d (%s)\n", static_cast<int>(left),
                after.isOk() ? "sigue habiendo piezas" : after.error().message.c_str());
    EXPECT_EQ(left, before.value().size() - 1)
        << "descartar una mancha se lleva por delante otra pieza, o no se lleva ninguna";

    // Y la que se descartó ya no está: se comprueba por posición, que es lo que
    // el operador señaló.
    for (const auto& piece : after.isOk() ? after.value() : std::vector<vision::PieceAnalysis>{}) {
        const cv::Rect stillThere = cv::boundingRect(piece.contour.points);
        EXPECT_FALSE(box.contains(cv::Point(stillThere.x + stillThere.width / 2,
                                            stillThere.y + stillThere.height / 2)))
            << "la mancha marcada como fondo sigue contándose como pieza";
    }
}
