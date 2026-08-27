// LAS MANCHAS QUE SE CAEN POR PEQUEÑAS SE CONTABAN CON UN `continue`.
//
// `findPieceContours` filtra por área: por debajo del mínimo es ruido, por
// encima del máximo es una segmentación degenerada. Bien. Lo que no estaba bien
// es que las descartadas se iban con un `continue` y **no las contaba nadie**.
//
// Había un parámetro `discarded`, y engaña: cuenta solo las que sobran del TOPE
// de piezas. Las que no llegan al área mínima se caen antes, así que no
// aparecían en ninguna cifra de la pantalla.
//
// Lo que costaba, medido sobre `arandelas-2` —una foto de catálogo con
// dieciséis arandelas graduadas, de la #6 a la de una pulgada—:
//
//     área mínima 0,50 % (de fábrica)    1 pieza
//     área mínima 0,10 %                 7
//     área mínima 0,02 %                47
//
// La última cifra NO son 47 arandelas: esa foto lleva rótulos («#6», «Flat
// Washers»), cotas y una regla, y con el mínimo tan bajo entran las letras. Por
// eso el mínimo existe y por eso no se toca su valor de fábrica.
//
// Pero con el de fábrica el operador ve **1 pieza** sobre dieciséis arandelas,
// sin ningún número que lo explique y sin saber qué tocar. Es el mismo fallo
// que la zona automática que escondía piezas sin decirlo: la aplicación hace
// algo razonable y no lo cuenta.
//
// Esta prueba no fija el valor de fábrica ni cuántas piezas hay: fija que el
// recuento EXISTA y sea el que es.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>

#include "vision/contour_analysis.h"
#include "vision/pipeline.h"

using namespace pci;

TEST(PiecesDroppedForBeingSmall, TheOnesUnderTheMinimumAreCountedAndNotJustSkipped) {
    // Sintético, para poder decir cuántas hay sin mirar ninguna foto: una grande
    // y cuatro claramente por debajo del mínimo.
    cv::Mat frame(400, 400, CV_8UC1, cv::Scalar(0));
    cv::circle(frame, {200, 200}, 60, cv::Scalar(255), cv::FILLED);  // 11310 px
    for (const auto& centre :
         {cv::Point(30, 30), cv::Point(370, 30), cv::Point(30, 370), cv::Point(370, 370)}) {
        cv::circle(frame, centre, 8, cv::Scalar(255), cv::FILLED);  // 201 px
    }
    // 0,5 % de 160000 son 800 px: la grande entra, las cuatro pequeñas no.
    int belowMinArea = -1;
    const auto pieces =
        vision::findPieceContours(frame, 0.005, 0.9, vision::kMaxPieces, nullptr,
                                  &belowMinArea);
    std::printf("  [pequeñas] %d piezas y %d manchas por debajo del mínimo\n",
                static_cast<int>(pieces.size()), belowMinArea);
    EXPECT_EQ(pieces.size(), 1U);
    EXPECT_EQ(belowMinArea, 4)
        << "las manchas que no llegan al área mínima siguen sin contarse, así que no "
           "hay forma de decírselo al operador";

    // Y bajando el mínimo entran las cinco y no queda ninguna fuera: el
    // recuento tiene que seguir al ajuste, no ser un número fijo.
    int noneLeft = -1;
    const auto all =
        vision::findPieceContours(frame, 0.0005, 0.9, vision::kMaxPieces, nullptr,
                                  &noneLeft);
    EXPECT_EQ(all.size(), 5U);
    EXPECT_EQ(noneLeft, 0);
}

TEST(PiecesDroppedForBeingSmall, ThePipelineHandsTheCountUpwards) {
    // De nada sirve contarlas en `vision` si el número no sube. Sobre la foto de
    // catálogo, que es donde más se nota.
    const cv::Mat image =
        cv::imread("C:/Users/furro/Pictures/IMG-MC/arandelas-2.png", cv::IMREAD_COLOR);
    if (image.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    vision::PipelineConfig config;
    config.segmentation.recoverHighlightsBy = 12;

    int belowMinArea = -1;
    auto found = vision::analyzeFrames(image, config, &belowMinArea);
    ASSERT_TRUE(found.isOk());
    std::printf("  [pequeñas] catálogo con el mínimo de fábrica: %d pieza(s) y %d "
                "manchas descartadas por pequeñas\n",
                static_cast<int>(found.value().size()), belowMinArea);

    EXPECT_EQ(found.value().size(), 1U)
        << "esta foto ya no da una sola pieza con el mínimo de fábrica; si cambió la "
           "detección, hay que volver a medir lo que este aviso promete";
    EXPECT_GT(belowMinArea, 10)
        << "no se están contando las manchas pequeñas de una foto donde hay quince "
           "arandelas por debajo del mínimo: el operador seguiría viendo «1 pieza» sin "
           "explicación";
}

TEST(PiecesDroppedForBeingSmall, ItStaysQuietWhereThereIsNothingToSay) {
    // LA OTRA MITAD, que importa tanto como la primera. Un aviso que sale en
    // todas las escenas se aprende a ignorar en dos días, y entonces tampoco
    // sirve donde sí hacía falta.
    //
    // Medido sobre el banco: cinco de nueve fotos no pierden ni una mancha —
    // incluida la bandeja de cien tuercas, que es la más poblada de todas— y las
    // que sí pierden son justo las de arandelas surtidas, donde las pequeñas son
    // piezas de verdad.
    struct Scene {
        const char* photo;
        bool shouldBeQuiet;
    };
    const Scene scenes[] = {
        {"producto-tuercas-prueba.jpg", true},   // cien tuercas iguales
        {"tornillo-1.png", true},                // una pieza sola
        {"tornillos-1.png", true},               // tres piezas grandes
        {"engranaje-1.png", true},               // una rueda
        {"arandelas-1.png", false},              // surtido con arandelas pequeñas
    };
    int checked = 0;
    for (const auto& scene : scenes) {
        const cv::Mat image =
            cv::imread(std::string("C:/Users/furro/Pictures/IMG-MC/") + scene.photo,
                       cv::IMREAD_COLOR);
        if (image.empty()) {
            continue;
        }
        ++checked;
        vision::PipelineConfig config;
        config.segmentation.recoverHighlightsBy = 12;
        int belowMinArea = -1;
        auto found = vision::analyzeFrames(image, config, &belowMinArea);
        ASSERT_TRUE(found.isOk()) << scene.photo;
        std::printf("  [pequeñas] %-30s %3d descartadas -> %s\n", scene.photo,
                    belowMinArea, belowMinArea == 0 ? "se calla" : "avisa");
        if (scene.shouldBeQuiet) {
            EXPECT_EQ(belowMinArea, 0)
                << scene.photo
                << ": avisa de manchas pequeñas donde no hay ninguna que valga la pena. "
                   "Un aviso que sale siempre se aprende a ignorar.";
        } else {
            EXPECT_GT(belowMinArea, 0)
                << scene.photo
                << ": aquí hay arandelas pequeñas que el mínimo deja fuera y no se dice";
        }
    }
    if (checked == 0) {
        GTEST_SKIP() << "sin banco de fotos";
    }
}
