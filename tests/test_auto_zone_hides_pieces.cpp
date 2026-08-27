// LA ZONA AUTOMÁTICA ESCONDE PIEZAS, Y HAY QUE DECIRLO.
//
// Queja del taller: «el detector de zona automática / toda la imagen toma solo
// una parte de la imagen para detectar».
//
// Es cierto, y por diseño: el modo automático recorta un rectángulo alrededor de
// la pieza y lo sigue, para no analizar el frame entero en cada fotograma. Eso
// está bien y ahorra trabajo de verdad.
//
// Lo que NO estaba bien era la conclusión que colgaba de ahí. `auto_roi.cpp`
// justificaba que al borrar una zona dibujada se cayera en modo automático —en
// vez de en imagen entera— con esta frase: «la automática nunca cambia una
// respuesta». La primera prueba de este fichero la mide, y es falsa.
//
// Así que el operador que borraba su zona creyendo volver al frame completo se
// quedaba mirando OTRO recorte, más pequeño, que le escondía piezas. Y nada se
// lo decía.
//
// La automática sigue estando: es útil cuando hay una pieza y no se mueve de
// sitio. Lo que deja de ser es el estado al que se cae sin pedirlo.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <vector>

#include "vision/auto_roi.h"
#include "vision/pipeline.h"

using namespace pci;
using vision::WorkingZoneMode;

namespace {

// Un frame con una o dos piezas oscuras sobre fondo claro, bien separadas.
cv::Mat sceneWith(bool secondPiece) {
    cv::Mat frame(480, 640, CV_8UC1, cv::Scalar(240));
    cv::circle(frame, {120, 240}, 55, cv::Scalar(40), cv::FILLED);
    if (secondPiece) {
        // Al otro extremo: fuera de cualquier recorte que siga a la primera.
        cv::circle(frame, {520, 240}, 55, cv::Scalar(40), cv::FILLED);
    }
    return frame;
}

int piecesSeen(const cv::Mat& frame, const cv::Rect& roi) {
    vision::PipelineConfig config;
    config.roi = roi;
    auto all = vision::analyzeFrames(frame, config);
    return all.isOk() ? static_cast<int>(all.value().size()) : 0;
}

}  // namespace

TEST(AutoZoneHidesPieces, TheTrackedCropDoesHidePiecesThatAppearOutsideIt) {
    // Esto FIJA el límite en vez de arreglarlo, y a propósito: seguir a la pieza
    // es lo que hace este modo, y una pieza que aparece fuera del recorte no se
    // ve porque no se está mirando ahí. Lo que no puede volver a pasar es que
    // alguien apoye una decisión en lo contrario.
    vision::AutoRoiTracker tracker;
    const cv::Mat alone = sceneWith(false);
    const cv::Rect firstPiece(65, 185, 110, 110);
    for (int frame = 0; frame < 10; ++frame) {
        tracker.update(true, firstPiece, cv::Size(alone.cols, alone.rows));
    }
    const cv::Rect crop = tracker.roi();
    ASSERT_FALSE(crop.empty()) << "el modo automático no llegó a recortar nada: entonces "
                                 "esta prueba no mide lo que cree";
    const double share = 100.0 * crop.area() / (alone.cols * alone.rows);
    std::printf("  [zona] el recorte se asienta en %dx%d — el %.1f %% del frame\n",
                crop.width, crop.height, share);

    const cv::Mat both = sceneWith(true);
    const int wholeFrame = piecesSeen(both, cv::Rect());
    const int cropped = piecesSeen(both, crop);
    std::printf("  [zona] con dos piezas: imagen entera ve %d, el recorte ve %d\n",
                wholeFrame, cropped);

    ASSERT_EQ(wholeFrame, 2) << "la escena de prueba no tiene dos piezas separables";
    EXPECT_EQ(cropped, 1)
        << "el recorte automático ha dejado de esconder la pieza de fuera. Si eso es "
           "deliberado, esta prueba y el comentario de `modeAfterFixedZoneChanged` hay "
           "que reescribirlos: la decisión de a dónde se cae al quitar una zona depende "
           "de este número.";
}

TEST(AutoZoneHidesPieces, RemovingAZoneGivesBackTheWholeImageAndNotAnotherCrop) {
    // Lo que se pidió, y lo que cualquiera espera: borro mi zona, veo la imagen
    // entera. Antes se caía en automática — otro recorte, más pequeño que el que
    // el operador acababa de borrar, y sin decirlo.
    EXPECT_EQ(vision::modeAfterFixedZoneChanged(WorkingZoneMode::Fixed, false),
              WorkingZoneMode::Off)
        << "al quitar la zona dibujada se cae en un modo que sigue recortando";

    // Y los otros modos no se tocan: quitar la rectangular no puede apagar una
    // zona libre que el operador dibujó aparte.
    EXPECT_EQ(vision::modeAfterFixedZoneChanged(WorkingZoneMode::Free, false),
              WorkingZoneMode::Free);
    EXPECT_EQ(vision::modeAfterFixedZoneChanged(WorkingZoneMode::Automatic, false),
              WorkingZoneMode::Automatic)
        << "quien eligió la automática a propósito no puede perderla por borrar otra cosa";
}
