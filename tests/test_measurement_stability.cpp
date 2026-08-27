// CUÁNTO SE MOVERÍA ESTA MEDIDA SI LA LUZ CAMBIARA UN POCO.
//
// Queja del taller: «la manera en que toma los contornos suele variar mucho por
// su sombra y la luz de enfrente de la pieza, porque puede tener las dos, y
// estar midiendo mal».
//
// Tenía razón y se puede poner número. Un borde limpio no depende del corte de
// gris: moverlo unos niveles no mueve la silueta. Un borde con una sombra
// pegada o un reflejo, sí — y entonces la cifra que el operador apunta depende
// de la lámpara tanto como de la pieza.
//
// Barriendo el umbral ±8 niveles —menos de lo que cambia una lámpara al
// calentarse— sobre el banco de fotos:
//
//     rosca-1                    572,0 px    oscila 0,0    0,0 %
//     tornillo-1                 961,0       oscila 1,0    0,1 %
//     tornillos-1                571,0       oscila 2,0    0,4 %
//     tornillo-2                 631,0       oscila 13,3   2,1 %
//     tornillo-ojo-4             488,2       oscila 19,9   4,1 %
//     producto-tuercas-prueba     84,4       oscila 6,4    7,5 %
//     arandelas-1                168,3       oscila 15,4   9,2 %
//
// Ocho de once se quedan por debajo del 0,5 % y las tres que fallan van del 2,1
// al 9,2. En medio no hay nada, así que el corte en el 2 % no es delicado.
//
// ESTO NO ES LA INCERTIDUMBRE EXPANDIDA de la norma: falta la escala, la
// repetibilidad del montaje y la del propio operador. Es una de sus
// componentes, la más barata de medir y justo la que este taller está viendo
// con los ojos. Decirlo es muchísimo más que publicar cuatro decimales sin
// ninguna advertencia.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>

#include "vision/pipeline.h"

using namespace pci;

namespace {

cv::Mat photo(const std::string& name) {
    return cv::imread("C:/Users/furro/Pictures/IMG-MC/" + name, cv::IMREAD_COLOR);
}

}  // namespace

TEST(MeasurementStability, ACleanEdgeDoesNotMoveWithTheThreshold) {
    // Sintético y con el borde más limpio que existe: un disco negro sobre
    // blanco, sin sombra ni reflejo. Si aquí oscilara, la medida no mediría la
    // sensibilidad a la luz sino el ruido de la propia función.
    cv::Mat frame(400, 400, CV_8UC1, cv::Scalar(250));
    cv::circle(frame, {200, 200}, 120, cv::Scalar(20), cv::FILLED);

    const auto stability = vision::measureStability(frame);
    std::printf("  [luz] disco limpio: %.1f px, oscila %.1f (%.1f %%)\n",
                stability.medianWidthPx, stability.swingPx,
                100.0 * stability.swingFraction);
    ASSERT_TRUE(stability.measured);
    EXPECT_LT(stability.swingFraction, vision::kMeasurementMovesWithTheLight)
        << "un borde de contraste máximo se mueve con el umbral: entonces la cifra no "
           "habla de la escena, habla de la función";
    EXPECT_NE(stability.summary.find("El borde manda sobre la luz"), std::string::npos);
}

TEST(MeasurementStability, AShadowedEdgeMovesAndSaysSo) {
    // El mismo disco, con una SOMBRA pegada: un halo gris alrededor, que es lo
    // que deja una lámpara lateral. El borde deja de estar donde el contraste
    // es máximo y pasa a depender de dónde se ponga el corte.
    //
    // La sombra se dibuja PEGADA al borde y se difumina DESPUÉS de poner la
    // pieza, no antes: con la pieza encima el borde vuelve a quedar nítido y la
    // escena no reproduce nada. Costó una vuelta.
    cv::Mat frame(400, 400, CV_8UC1, cv::Scalar(250));
    cv::circle(frame, {200, 200}, 150, cv::Scalar(190), cv::FILLED);  // la sombra
    cv::circle(frame, {200, 200}, 120, cv::Scalar(20), cv::FILLED);   // la pieza
    cv::GaussianBlur(frame, frame, cv::Size(61, 61), 0);

    const auto stability = vision::measureStability(frame);
    std::printf("  [luz] disco con sombra: %.1f px, oscila %.1f (%.1f %%)\n",
                stability.medianWidthPx, stability.swingPx,
                100.0 * stability.swingFraction);
    ASSERT_TRUE(stability.measured);
    EXPECT_GT(stability.swingFraction, vision::kMeasurementMovesWithTheLight)
        << "con una sombra pegada al borde la medida no se mueve al cambiar el corte, "
           "así que este aviso no serviría para lo único que existe";
    EXPECT_NE(stability.summary.find("le afecta la luz"), std::string::npos)
        << "el aviso no dice qué mirar: " << stability.summary;
}

TEST(MeasurementStability, ItTellsTheTwoScenesOfTheBankApart) {
    // Y sobre fotos de verdad, que es donde importa. `rosca-1` es la más limpia
    // del banco y `arandelas-1` la que más se mueve.
    const cv::Mat clean = photo("rosca-1.png");
    const cv::Mat shaky = photo("arandelas-1.png");
    if (clean.empty() || shaky.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    vision::PipelineConfig config;
    config.segmentation.recoverHighlightsBy = 12;

    const auto stable = vision::measureStability(clean, config);
    const auto moving = vision::measureStability(shaky, config);
    std::printf("  [luz] rosca-1:     %.1f px, oscila %.1f (%.1f %%)\n",
                stable.medianWidthPx, stable.swingPx, 100.0 * stable.swingFraction);
    std::printf("  [luz] arandelas-1: %.1f px, oscila %.1f (%.1f %%)\n",
                moving.medianWidthPx, moving.swingPx, 100.0 * moving.swingFraction);

    ASSERT_TRUE(stable.measured);
    ASSERT_TRUE(moving.measured);
    EXPECT_LT(stable.swingFraction, vision::kMeasurementMovesWithTheLight)
        << "la foto más limpia del banco sale como sensible a la luz: el aviso saltaría "
           "en todas partes y se aprendería a ignorar";
    EXPECT_GT(moving.swingFraction, vision::kMeasurementMovesWithTheLight)
        << "la foto que más se mueve del banco sale como estable: entonces el aviso no "
           "avisa de nada";
}
