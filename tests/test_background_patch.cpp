// SEÑALAR UN TROZO DE MESA, EN VEZ DE TECLEAR UN COLOR.
//
// Petición del taller: «lo del color de fondo, al momento de seleccionarlo, el
// usuario debería poder recortar o seleccionar un área del fondo por la
// textura, y la descarte, para poder tomar las piezas correctamente».
//
// Lo que había era una rueda de colores. Ahí el operador tiene que ADIVINAR el
// rojo de su propio cartón, y no lo sabe nadie de memoria — el color está
// delante, en la foto.
//
// Estas pruebas fijan las dos mitades que hacen que esto sirva de algo:
//
//   1. Que señalar un trozo de mesa dé un fondo MEJOR que la mediana del marco,
//      que es lo automático de hoy.
//   2. Que cuando el recuadro cae encima de una PIEZA, se note y se diga. Esa
//      es la equivocación fácil, no da ningún error, y arruina la detección de
//      todo lo que se inspeccione después.
//
// La segunda importa más que la primera. Un color de fondo mal elegido no falla
// ruidosamente: sigue midiendo, peor, durante meses.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>

#include "vision/pipeline.h"
#include "vision/segmentation.h"

using namespace pci;

namespace {

// El banco de fotos vive FUERA del repositorio. Sin él, estas pruebas se saltan
// solas en vez de fallar: el repositorio tiene que poder clonarse y compilar.
const std::string kBank = "C:/Users/furro/Pictures/IMG-MC/";

cv::Mat photo(const std::string& name) {
    return cv::imread(kBank + name, cv::IMREAD_COLOR);
}

// Cuántas piezas y cuánto del cuadro, con la clave de color puesta en un color
// concreto. Es lo que el operador acaba viendo.
struct Seen {
    int pieces = 0;
    double coverage = 0.0;
};

Seen seenWith(const cv::Mat& image, const cv::Vec3b& background) {
    vision::SegmentationOptions options;
    // Con la recuperación de brillos encendida, que es como se mira una foto de
    // metal de verdad. Sin ella, estas fotos salen partidas por los reflejos y
    // el número de piezas no diría nada del color del fondo.
    options.recoverHighlightsBy = 12;
    options.backgroundKey = vision::SegmentationOptions::BackgroundKey::Fixed;
    options.background = background;

    Seen seen;
    auto mask = vision::segmentPiece(image, options);
    if (mask.isOk()) {
        seen.coverage = 100.0 * cv::countNonZero(mask.value()) /
                        static_cast<double>(mask.value().total());
    }
    vision::PipelineConfig config;
    config.segmentation = options;
    auto pieces = vision::analyzeFrames(image, config);
    if (pieces.isOk()) {
        seen.pieces = static_cast<int>(pieces.value().size());
    }
    return seen;
}

}  // namespace

TEST(BackgroundPatch, PointingAtTheTableBeatsGuessingFromTheBorder) {
    // `arandelas-1`: veinte arandelas surtidas sobre un cartón ROJO, que es la
    // foto que motivó toda la clave de color.
    const cv::Mat image = photo("arandelas-1.png");
    if (image.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }

    // Un cuadro de mesa limpia, arriba y entre dos arandelas.
    const auto sample = vision::sampleBackground(image, cv::Rect(192, 0, 64, 64));
    ASSERT_TRUE(sample.valid);
    const cv::Vec3b border = vision::estimateBackgroundColour(image);

    const Seen fromBorder = seenWith(image, border);
    const Seen fromPatch = seenWith(image, sample.colour);
    std::printf("  [fondo] marco  (%d,%d,%d) -> %2d piezas, %.1f %%\n", border[2], border[1],
                border[0], fromBorder.pieces, fromBorder.coverage);
    std::printf("  [fondo] parche (%d,%d,%d) -> %2d piezas, %.1f %%  (dispersión %.0f)\n",
                sample.colour[2], sample.colour[1], sample.colour[0], fromPatch.pieces,
                fromPatch.coverage, sample.spread);

    EXPECT_TRUE(sample.looksUniform)
        << "un cuadro de cartón vacío no se reconoce como mesa; entonces el aviso de la "
           "ventana saltaría siempre y se aprendería a ignorar";
    EXPECT_GT(fromPatch.pieces, fromBorder.pieces)
        << "señalar la mesa no mejora sobre la mediana del marco. Si eso deja de ser "
           "cierto, esta ventana no se gana el sitio y hay que quitarla, no ajustarla.";
}

TEST(BackgroundPatch, PickingAPieceByMistakeTurnsTheSceneInsideOutAndIsCaught) {
    // ESTA ES LA EQUIVOCACIÓN QUE HAY QUE CAZAR.
    //
    // El recuadro cae encima de una arandela en vez de sobre la mesa. No da
    // ningún error: da una detección del revés. Y sin este aviso, el operador
    // ve un color perfectamente razonable en el botón y se va tan tranquilo.
    const cv::Mat image = photo("arandelas-1.png");
    if (image.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }

    const auto onAPiece = vision::sampleBackground(image, cv::Rect(300, 60, 64, 64));
    ASSERT_TRUE(onAPiece.valid);
    const Seen seen = seenWith(image, onAPiece.colour);
    std::printf("  [fondo] encima de una arandela (%d,%d,%d) -> %d piezas, %.1f %% "
                "(dispersión %.0f)\n",
                onAPiece.colour[2], onAPiece.colour[1], onAPiece.colour[0], seen.pieces,
                seen.coverage, onAPiece.spread);

    // Primero: que el desastre es real, para que el aviso tenga de qué avisar.
    EXPECT_EQ(seen.pieces, 0) << "esta selección ya no rompe la escena: si es así, el aviso "
                                 "de la ventana está avisando de algo que no pasa";
    EXPECT_GT(seen.coverage, 80.0)
        << "la escena ya no sale del revés; hay que volver a medir de qué avisa la ventana";

    // Y segundo: que se ve venir sin haber corrido nada.
    EXPECT_FALSE(onAPiece.looksUniform)
        << "coger una arandela entera dentro del recuadro pasa por mesa. Es el fallo que "
           "esta ventana existe para evitar, y el único momento en que se puede evitar es "
           "ANTES de aceptar.";
}

TEST(BackgroundPatch, ATrayWithNoTableInSightSaysSoInsteadOfPretending) {
    // La bandeja de cien tuercas no tiene un solo trozo de mesa que señalar: las
    // piezas llegan a los cuatro bordes. Barriendo la imagen entera con
    // recuadros de 64x64, el más uniforme de todos da 143 — muy por encima del
    // corte.
    //
    // Lo honrado ahí es decir que no hay mesa, no dejar que el operador señale
    // tuercas convencido de que señala mesa. Esta prueba fija que en esa foto
    // NINGÚN recuadro pasa por fondo.
    const cv::Mat image = photo("producto-tuercas-prueba.jpg");
    if (image.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }

    double best = 1e9;
    const int step = 64;
    for (int y = 0; y + step < image.rows; y += step) {
        for (int x = 0; x + step < image.cols; x += step) {
            const auto sample = vision::sampleBackground(image, cv::Rect(x, y, step, step));
            if (sample.valid) {
                best = std::min(best, sample.spread);
            }
        }
    }
    std::printf("  [fondo] bandeja de cien tuercas: el recuadro más uniforme varía %.0f "
                "(el corte está en %.0f)\n",
                best, vision::kBackgroundPatchIsUniform);
    EXPECT_GT(best, vision::kBackgroundPatchIsUniform)
        << "en una bandeja llena hasta los bordes aparece un recuadro que pasa por mesa: "
           "el operador lo señalaría creyendo que es fondo";
}

TEST(BackgroundPatch, AFlatStudioTableIsUniformWhereverYouPoint) {
    // La otra mitad del aviso: sobre una mesa lisa tiene que CALLARSE, mire uno
    // donde mire. Un aviso que salta también cuando todo está bien se aprende a
    // ignorar en dos días, y entonces tampoco sirve donde hacía falta.
    const cv::Mat image = photo("arandelas-5.png");
    if (image.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    const auto corner = vision::sampleBackground(image, cv::Rect(0, 0, 64, 64));
    ASSERT_TRUE(corner.valid);
    std::printf("  [fondo] esquina de mesa lisa: dispersión %.1f -> %s\n", corner.spread,
                corner.looksUniform ? "mesa" : "AVISA (mal)");
    EXPECT_TRUE(corner.looksUniform) << "sobre una mesa de estudio lisa salta el aviso";
}

TEST(BackgroundPatch, ATinySelectionIsNoSampleAtAll) {
    // Un clic sin arrastrar, o un recuadro de cuatro píxeles. El p95 de veinte
    // píxeles es «el segundo más alto de veinte», y con eso no se habla de una
    // mesa entera. Mejor no devolver nada que devolver un color inventado.
    cv::Mat flat(200, 200, CV_8UC3, cv::Scalar(30, 30, 200));
    EXPECT_FALSE(vision::sampleBackground(flat, cv::Rect(10, 10, 4, 4)).valid);
    EXPECT_FALSE(vision::sampleBackground(flat, cv::Rect(10, 10, 0, 0)).valid);
    // Y fuera de la imagen tampoco: el recuadro se recorta, y si no queda nada
    // dentro no hay muestra.
    EXPECT_FALSE(vision::sampleBackground(flat, cv::Rect(500, 500, 40, 40)).valid);

    // Pero un recuadro que se sale POR UN LADO sí vale: quien arrastra hasta el
    // borde se pasa siempre, y perder la selección entera por eso sería
    // castigar el gesto normal.
    const auto clipped = vision::sampleBackground(flat, cv::Rect(-20, -20, 80, 80));
    EXPECT_TRUE(clipped.valid);
    EXPECT_EQ(clipped.colour, cv::Vec3b(30, 30, 200));
}
