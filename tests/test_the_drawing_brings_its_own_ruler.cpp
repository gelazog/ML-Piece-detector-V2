// EL BANCO NO TENÍA NI UNA MEDIDA CON VERDAD DE CAMPO. ESTA FOTO LA TRAE DENTRO.
//
// Todas las pruebas sobre el banco comprueban COHERENCIA —que el informe no se
// contradiga, que no publique imposibles, que los agujeros sean de la pieza que
// mide—, y eso vale sobre cualquier imagen. Lo que no había era una sola medida
// contrastada contra un patrón: «esta cota son 4,2 mm y la aplicación dice 4,2».
// Sin eso, todo el banco puede estar de acuerdo consigo mismo y equivocado.
//
// `rosca-1.png` trae el patrón dentro. Es un dibujo didáctico de una rosca con
// una flecha azul que rotula **una pulgada** y los seis hilos que caben en ella
// numerados del 1 al 6. O sea, el propio dibujo declara su paso: 1"/6.
//
// La flecha se mide en la imagen en vez de escribir aquí el número que salió,
// porque un número copiado a mano se queda viejo el día que alguien recorte la
// imagen y nadie se entera. La fila más azul del dibujo es el asta de la flecha,
// de punta a punta.
//
// Lo medido: el asta va de x=172 a x=562, o sea **391 px por pulgada**, que
// entre seis hilos son 65,2 px de paso. La herramienta de rosca mide **66,0**.
// Un 1,3 % de diferencia, y el propio patrón no es mejor que eso: las puntas de
// flecha están dibujadas con unos píxeles de más.
//
// Lo que esta prueba protege: que la herramienta de rosca siga acertando el paso
// contra un patrón EXTERNO a la aplicación. Las demás pruebas de rosca comparan
// la aplicación consigo misma.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <filesystem>
#include <string>

#include "inspection_editor/auto_measure.h"
#include "vision/segmentation.h"

using namespace pci;

TEST(ThreadPitch, ItAgreesWithTheInchTheDrawingItselfMarks) {
    const std::filesystem::path file{"C:/Users/furro/Pictures/IMG-MC/rosca-1.png"};
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        GTEST_SKIP() << "las fotos del usuario no están en esta máquina";
    }
    const cv::Mat colour = cv::imread(file.string(), cv::IMREAD_COLOR);
    ASSERT_FALSE(colour.empty());

    // --- El patrón: la flecha azul que rotula una pulgada -------------------
    cv::Mat hsv;
    cv::cvtColor(colour, hsv, cv::COLOR_BGR2HSV);
    cv::Mat blue;
    cv::inRange(hsv, cv::Scalar(90, 80, 60), cv::Scalar(115, 255, 255), blue);
    int shaftRow = -1;
    int widest = 0;
    for (int y = 0; y < blue.rows; ++y) {
        const int painted = cv::countNonZero(blue.row(y));
        if (painted > widest) {
            widest = painted;
            shaftRow = y;
        }
    }
    ASSERT_GT(widest, 100) << "no se encuentra la flecha azul: o la imagen cambió, o el "
                              "filtro de color ya no la coge. Sin flecha no hay patrón y "
                              "esta prueba no puede comprobar nada";
    const cv::Rect shaft = cv::boundingRect(blue.row(shaftRow));
    const double inchInPixels = shaft.width;
    // Cordura del patrón: una pulgada tiene que ocupar la mayor parte del ancho
    // de un dibujo hecho para enseñarla, y no puede pasarse del propio dibujo.
    ASSERT_GT(inchInPixels, 0.4 * colour.cols) << "la flecha mide muy poco para ser la "
                                                  "pulgada del dibujo";
    ASSERT_LE(inchInPixels, colour.cols);

    // Seis hilos en esa pulgada, contados en el dibujo: están numerados del 1 al 6.
    constexpr int kThreadsInTheInch = 6;
    const double pitchTheDrawingDeclares = inchInPixels / kThreadsInTheInch;

    // --- Lo que mide la aplicación ------------------------------------------
    cv::Mat gray;
    cv::cvtColor(colour, gray, cv::COLOR_BGR2GRAY);
    vision::SegmentationOptions options;
    options.recoverHighlightsBy = 12;
    const auto segmented = vision::segmentPiece(gray, options);
    ASSERT_TRUE(segmented.isOk()) << segmented.error().message;

    inspection::ProposeOptions everything;
    everything.maxProposals = 200;
    double pitchMeasured = 0.0;
    for (const auto& proposal :
         inspection::proposeTools(gray, segmented.value(), {}, everything)) {
        if (proposal.config.name.rfind("Paso", 0) == 0) {
            pitchMeasured = proposal.measured;
        }
    }
    ASSERT_GT(pitchMeasured, 0.0)
        << "no se propuso ninguna cota de paso sobre un dibujo que es una rosca de "
           "perfil: eso ya es el fallo, sin necesidad de comparar números";

    const double error = std::abs(pitchMeasured - pitchTheDrawingDeclares) /
                         pitchTheDrawingDeclares;
    std::printf("  [rosca] la pulgada del dibujo son %.0f px -> paso declarado %.2f px; "
                "medido %.2f px (%.1f %%)\n",
                inchInPixels, pitchTheDrawingDeclares, pitchMeasured, 100.0 * error);

    // El 5 % no es un ideal de precisión: es lo que da de sí el patrón. La flecha
    // se dibuja con puntas de varios píxeles y las guías son de puntos, así que
    // la propia pulgada solo es buena al 1 % largo. Pedir menos sería medir el
    // dibujo, no la herramienta.
    EXPECT_LT(error, 0.05)
        << "el paso medido (" << pitchMeasured << " px) no cuadra con el que declara el "
           "dibujo (" << pitchTheDrawingDeclares << " px, de una pulgada de "
        << inchInPixels << " px con seis hilos numerados dentro)";
}
