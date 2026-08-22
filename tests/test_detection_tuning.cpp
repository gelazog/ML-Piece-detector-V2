#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>

#include <opencv2/imgproc.hpp>

#include "vision/detection_tuning.h"

using pci::vision::maskAgreement;
using pci::vision::SegmentationOptions;
using pci::vision::SegmentationPolarity;
using pci::vision::suggestSegmentation;

namespace {

// Una pieza clara sobre fondo oscuro, con un lado en PENUMBRA: ni tan claro
// como la pieza ni tan oscuro como el fondo. Es la sombra que se come un lado,
// que es el caso que motivó todo esto.
cv::Mat pieceWithAShadedSide(int shade) {
    cv::Mat image(240, 320, CV_8UC1, cv::Scalar(30));
    cv::rectangle(image, cv::Rect(80, 60, 160, 120), cv::Scalar(210), cv::FILLED);
    cv::rectangle(image, cv::Rect(200, 60, 40, 120), cv::Scalar(shade), cv::FILLED);
    return image;
}

// Lo que el operador da por bueno: la pieza ENTERA, penumbra incluida.
cv::Mat theWholePiece() {
    cv::Mat truth(240, 320, CV_8UC1, cv::Scalar(0));
    cv::rectangle(truth, cv::Rect(80, 60, 160, 120), cv::Scalar(255), cv::FILLED);
    return truth;
}

}  // namespace

TEST(MaskAgreement, ItIsIoUAndNotThePercentageOfEqualPixels) {
    // Una pieza pequeña en una imagen grande: decir «todo es fondo» acierta
    // casi todos los píxeles y no detecta nada. Con IoU eso vale CERO, y esa
    // es exactamente la razón de usarlo.
    cv::Mat truth(200, 200, CV_8UC1, cv::Scalar(0));
    cv::rectangle(truth, cv::Rect(90, 90, 20, 20), cv::Scalar(255), cv::FILLED);
    const cv::Mat nothing(200, 200, CV_8UC1, cv::Scalar(0));

    const double equalPixels = 1.0 - (20.0 * 20.0) / (200.0 * 200.0);
    std::printf("  [acuerdo] «todo es fondo» acierta el %.1f%% de los pixeles, IoU %.2f\n",
                100.0 * equalPixels, maskAgreement(nothing, truth));
    // 1 - 400/40000 = 0.99 EXACTO, no «casi»: la pieza son 20x20 de 200x200.
    EXPECT_DOUBLE_EQ(equalPixels, 0.99)
        << "el porcentaje de pixeles iguales premia no detectar nada";
    EXPECT_DOUBLE_EQ(maskAgreement(nothing, truth), 0.0);

    EXPECT_DOUBLE_EQ(maskAgreement(truth, truth), 1.0);
    // Dos vacías dicen lo mismo: acuerdo, no división por cero.
    EXPECT_DOUBLE_EQ(maskAgreement(nothing, nothing), 1.0);
}

TEST(DetectionTuning, TheCorrectionRevealsTheThresholdThatWouldHaveWorked) {
    // La penumbra está en 120: por encima del fondo (30) y por debajo de la
    // pieza (210). Con un umbral alto se queda fuera, y ese es el trozo que el
    // operador tiene que pintar a mano.
    const cv::Mat image = pieceWithAShadedSide(120);
    const cv::Mat truth = theWholePiece();

    SegmentationOptions tooStrict;
    tooStrict.manualThreshold = 170;  // deja la penumbra fuera
    tooStrict.polarity = SegmentationPolarity::LightPiece;
    tooStrict.blurKernel = 0;
    tooStrict.morphKernel = 0;

    const auto suggestion = suggestSegmentation(image, truth, tooStrict);
    ASSERT_TRUE(suggestion.found);
    std::printf("  [afinar] ahora %.3f -> propuesto %.3f con umbral %d\n",
                suggestion.agreementNow, suggestion.agreementSuggested,
                suggestion.options.manualThreshold);

    EXPECT_LT(suggestion.agreementNow, 0.85)
        << "el ajuste de partida tenía que fallar: si no, no hay nada que afinar";
    EXPECT_GT(suggestion.agreementSuggested, 0.98)
        << "existía un umbral que da la pieza entera y no se encontró";
    EXPECT_TRUE(suggestion.worthApplying());

    // Y el umbral propuesto tiene que caer ENTRE el fondo y la penumbra, que es
    // el único sitio donde la penumbra cuenta como pieza y el fondo no.
    EXPECT_GT(suggestion.options.manualThreshold, 30);
    EXPECT_LT(suggestion.options.manualThreshold, 120);
}

TEST(DetectionTuning, WhenTheSettingsAreAlreadyRightItProposesNothing) {
    // Interrumpir para proponer un cambio que no arregla nada gasta la
    // confianza que hace falta para cuando sí lo arregle.
    const cv::Mat image = pieceWithAShadedSide(120);
    const cv::Mat truth = theWholePiece();

    SegmentationOptions good;
    good.manualThreshold = 75;  // entre el fondo y la penumbra: ya acierta
    good.polarity = SegmentationPolarity::LightPiece;
    good.blurKernel = 0;
    good.morphKernel = 0;

    const auto suggestion = suggestSegmentation(image, truth, good);
    ASSERT_TRUE(suggestion.found);
    std::printf("  [afinar] ya bien: ahora %.3f, mejor posible %.3f (mejora %.4f)\n",
                suggestion.agreementNow, suggestion.agreementSuggested,
                suggestion.agreementSuggested - suggestion.agreementNow);
    EXPECT_GT(suggestion.agreementNow, 0.98);
    EXPECT_FALSE(suggestion.worthApplying())
        << "propone cambiar unos ajustes que ya dan la respuesta correcta";
}

TEST(DetectionTuning, ItCatchesTheReversedPolarity) {
    // Pieza OSCURA sobre fondo claro con los ajustes puestos al revés. Es la
    // equivocación que más lejos deja el resultado, y la que un umbral solo no
    // arregla: hay que cambiar de signo.
    cv::Mat image(240, 320, CV_8UC1, cv::Scalar(220));
    cv::rectangle(image, cv::Rect(80, 60, 160, 120), cv::Scalar(40), cv::FILLED);
    const cv::Mat truth = theWholePiece();

    SegmentationOptions backwards;
    backwards.manualThreshold = 128;
    backwards.polarity = SegmentationPolarity::LightPiece;  // al revés
    backwards.blurKernel = 0;
    backwards.morphKernel = 0;

    const auto suggestion = suggestSegmentation(image, truth, backwards);
    ASSERT_TRUE(suggestion.found);
    std::printf("  [afinar] polaridad: ahora %.3f -> propuesto %.3f (polaridad %d)\n",
                suggestion.agreementNow, suggestion.agreementSuggested,
                static_cast<int>(suggestion.options.polarity));
    EXPECT_LT(suggestion.agreementNow, 0.1) << "con la polaridad al revés no se detecta la pieza";
    EXPECT_GT(suggestion.agreementSuggested, 0.98);
    EXPECT_EQ(suggestion.options.polarity, SegmentationPolarity::DarkPiece);
    EXPECT_TRUE(suggestion.worthApplying());
}

TEST(DetectionTuning, ItRefusesToGuessWhenTheInputMakesNoSense) {
    const cv::Mat image = pieceWithAShadedSide(120);
    SegmentationOptions options;

    // Máscara de otro tamaño: no se puede comparar nada.
    const cv::Mat wrongSize(100, 100, CV_8UC1, cv::Scalar(255));
    EXPECT_FALSE(suggestSegmentation(image, wrongSize, options).found);
    EXPECT_FALSE(suggestSegmentation(image, cv::Mat(), options).found);
    EXPECT_FALSE(suggestSegmentation(cv::Mat(), theWholePiece(), options).found);
    EXPECT_DOUBLE_EQ(maskAgreement(image, wrongSize), 0.0);
}

TEST(DetectionTuning, ItIsFastEnoughToRunWhileTheOperatorWaits) {
    // Cuánto cuesta, medido y no supuesto: de ello depende si esto puede correr
    // solo tras cada pincelada o tiene que ser una acción aparte.
    cv::Mat image(1080, 1920, CV_8UC1, cv::Scalar(30));
    cv::rectangle(image, cv::Rect(400, 300, 900, 500), cv::Scalar(210), cv::FILLED);
    cv::rectangle(image, cv::Rect(1100, 300, 200, 500), cv::Scalar(120), cv::FILLED);
    cv::Mat truth(1080, 1920, CV_8UC1, cv::Scalar(0));
    cv::rectangle(truth, cv::Rect(400, 300, 900, 500), cv::Scalar(255), cv::FILLED);

    SegmentationOptions options;
    options.manualThreshold = 170;
    options.polarity = SegmentationPolarity::LightPiece;

    const auto start = std::chrono::steady_clock::now();
    const auto suggestion = suggestSegmentation(image, truth, options);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();

    ASSERT_TRUE(suggestion.found);
    std::printf("  [coste] %lld ms sobre 1920x1080, %.3f -> %.3f\n", static_cast<long long>(ms),
                suggestion.agreementNow, suggestion.agreementSuggested);
    EXPECT_LT(ms, 3000) << "demasiado lento para correr con el operador esperando";
}
