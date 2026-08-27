// NINGUNA PIEZA SE QUEDA SOLO CON COTAS QUE NO COMPRUEBAN NADA.
//
// La medición automática propone dos clases de cosas, y lo dice en el motivo de
// cada una. Las que COMPRUEBAN —un diámetro, la redondez, el paso de una rosca,
// los dientes de un engranaje— tienen un valor que significa algo de la pieza y
// al que se le puede poner una tolerancia. Y las de REFERENCIA, que llevan este
// descargo escrito:
//
//     «Se mide ahora sobre esta pieza; guardada, repite este valor: vale como
//      cota de referencia, no como comprobación.»
//
// Un `ruler` sobre el rectángulo envolvente es eso: mide lo que mide hoy, y
// guardado repite ese número. Está bien que se ofrezca y está mejor que lo diga.
//
// Lo que no puede pasar es que a una pieza le salgan SOLO de esas. El operador
// abriría la lista de propuestas, aceptaría cinco, y se quedaría con una
// plantilla que no puede rechazar nada — sin que ningún error se lo dijera.
//
// Medido sobre el banco entero: 48 piezas, 268 propuestas, 89 con descargo
// (33 %, y son todos los `ruler` y los cuatro `angle`), y **cero** piezas donde
// todo lleve descargo. Esta prueba fija ese cero.
//
// No fija el 33 %: esa proporción puede moverse por motivos legítimos —una foto
// nueva, una pieza más sencilla— y atarla sería atar el banco de fotos.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "inspection_editor/auto_measure.h"
#include "inspection_editor/tools/tool_types.h"
#include "vision/contour_analysis.h"
#include "vision/pipeline.h"

using namespace pci;

namespace {

// El descargo, tal y como lo escribe el proponedor. Se busca la frase y no el
// tipo de herramienta a propósito: si mañana un `ruler` sobre un tramo recto
// pasa a valer como comprobación, esta prueba tiene que enterarse por el texto
// que el operador lee, no por una lista de tipos escrita aparte.
bool isOnlyAReference(const std::string& reason) {
    return reason.find("no como comprobación") != std::string::npos;
}

}  // namespace

TEST(ProposalsAreCheckable, EveryPieceGetsAtLeastOneThingItCanBeJudgedBy) {
    const std::vector<std::string> photos = {
        "Producto_Tuerca_Liv_02.jpg", "arandelas-1.png", "arandelas-2.png",
        "arandelas-3.jpg", "arandelas-4.png", "arandelas-5.png", "engranaje-1.png",
        "engranajes-1.jpg", "rosca-1.png", "tornillo-1.png", "tornillo-2.png",
        "tornillo-ojo-3.png", "tornillo-ojo-4.png", "tornillo-ojo-5.png",
        "tornillos-1.png"};

    int pieces = 0;
    int proposals = 0;
    int references = 0;
    std::vector<std::string> barren;

    for (const auto& photo : photos) {
        const cv::Mat image =
            cv::imread("C:/Users/furro/Pictures/IMG-MC/" + photo, cv::IMREAD_COLOR);
        if (image.empty()) {
            continue;
        }
        vision::PipelineConfig config;
        config.segmentation.recoverHighlightsBy = 12;
        auto all = vision::analyzeFrames(image, config);
        if (!all.isOk()) {
            continue;
        }
        for (std::size_t i = 0; i < all.value().size(); ++i) {
            const auto& piece = all.value()[i];
            ++pieces;
            const cv::Mat mask =
                vision::pieceMaskWithHoles(image, piece.mask, config.segmentation);
            const auto offered =
                inspection::proposeTools(image, mask, piece.fixture, {}, 0.0, nullptr);
            int checkable = 0;
            for (const auto& proposal : offered) {
                ++proposals;
                if (isOnlyAReference(proposal.reason)) {
                    ++references;
                } else {
                    ++checkable;
                }
            }
            // Una pieza sin NINGUNA propuesta no es este fallo: es que no había
            // nada que proponer, y la aplicación lo dice por otro camino.
            if (!offered.empty() && checkable == 0) {
                barren.push_back(photo + " pieza " + std::to_string(i + 1));
            }
        }
    }

    if (pieces == 0) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    std::printf("  [propuestas] %d piezas, %d cotas, %d de referencia (%.0f %%)\n", pieces,
                proposals, references,
                proposals > 0 ? 100.0 * references / proposals : 0.0);

    // Que el barrido esté mirando de verdad: sin esto, la prueba pasaría en
    // verde el día que `proposeTools` dejara de proponer nada.
    ASSERT_GT(pieces, 30) << "el banco da muy pocas piezas: esta prueba no está "
                             "comprobando lo que cree";
    ASSERT_GT(proposals, 100) << "apenas se proponen cotas: idem";
    // Y que haya de las dos clases, o la comprobación de abajo sería trivial.
    ASSERT_GT(references, 0)
        << "ya no hay cotas de referencia; si el descargo se reescribió, hay que "
           "actualizar la frase que busca esta prueba — no borrarla";

    for (const auto& piece : barren) {
        ADD_FAILURE() << piece
                      << ": todas sus cotas llevan el descargo de «no como comprobación». "
                         "El operador aceptaría la lista entera y se quedaría con una "
                         "plantilla que no puede rechazar nada, sin que nadie se lo diga.";
    }
    std::printf("  [propuestas] piezas sin ninguna cota comprobable: %d\n",
                static_cast<int>(barren.size()));
}
