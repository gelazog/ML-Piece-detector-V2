// LA VENTANA DE SEÑALAR EL FONDO: que enseñe lo que de verdad va a pasar.
//
// `tests/test_background_patch.cpp` mide el fondo; esto comprueba la mitad que
// convierte un número en una decisión que el operador puede tomar mirando.
//
// Va aparte porque necesita Qt y una plataforma sin pantalla, como el resto de
// las pruebas de widget.

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QPushButton>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>

#include "ui/background_patch_dialog.h"
#include "vision/pipeline.h"
#include "vision/segmentation.h"

using namespace pci;

namespace {

// El banco de fotos vive FUERA del repositorio: sin él, esto se salta solo.
cv::Mat photo(const std::string& name) {
    return cv::imread("C:/Users/furro/Pictures/IMG-MC/" + name, cv::IMREAD_COLOR);
}

struct Seen {
    int pieces = 0;
    double coverage = 0.0;
};

// Lo que el pipeline DE VERDAD hace con ese color de fondo. Es contra esto
// contra lo que se compara lo que la ventana enseña.
Seen seenWith(const cv::Mat& image, const cv::Vec3b& background,
              vision::SegmentationOptions options) {
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


TEST(BackgroundPatchWindow, ThePreviewIsTheRealSegmentationAndNotAnImpression) {
    // La ventana podría enseñar «lo que se parece al color elegido» con un
    // umbral inventado allí mismo: más rápido, más bonito, y enseñando una cosa
    // mientras el programa hace otra. Eso es justo el fallo que la ventana viene
    // a evitar, así que la vista previa tiene que coincidir CON EL PIPELINE.
    const cv::Mat image = photo("arandelas-1.png");
    if (image.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    vision::SegmentationOptions options;
    options.recoverHighlightsBy = 12;

    ui::BackgroundPatchDialog dialog(image, options);
    dialog.selectPatch(cv::Rect(192, 0, 64, 64));

    const Seen truth = seenWith(image, dialog.sample().colour, options);
    std::printf("  [ventana] enseña %d piezas / %.1f %%; el pipeline da %d / %.1f %%\n",
                dialog.previewPieces(), dialog.previewCoverage(), truth.pieces,
                truth.coverage);
    EXPECT_EQ(dialog.previewPieces(), truth.pieces)
        << "la ventana enseña un número de piezas distinto del que va a salir";
    EXPECT_NEAR(dialog.previewCoverage(), truth.coverage, 0.01)
        << "la ventana enseña una máscara distinta de la que va a salir";
}

TEST(BackgroundPatchWindow, ItRefusesToHandBackAColourNobodyPointedAt) {
    // Sin selección, «Usar este fondo» tiene que estar apagado. Aceptar sin
    // haber señalado dejaría el ajuste como estaba haciendo creer que se cambió
    // algo — y el operador se iría convencido de haber arreglado la detección.
    cv::Mat flat(300, 300, CV_8UC3, cv::Scalar(30, 30, 200));
    ui::BackgroundPatchDialog dialog(flat, vision::SegmentationOptions{});

    auto* ok = dialog.findChild<QPushButton*>(QStringLiteral("takeColour"));
    ASSERT_NE(ok, nullptr);
    EXPECT_FALSE(ok->isEnabled()) << "se puede aceptar sin haber señalado nada";
    EXPECT_FALSE(dialog.sample().valid);

    dialog.selectPatch(cv::Rect(20, 20, 80, 80));
    EXPECT_TRUE(ok->isEnabled()) << "señalado un trozo de mesa, sigue sin dejar aceptar";
    EXPECT_TRUE(dialog.sample().valid);
}

TEST(BackgroundPatchWindow, TheWarningSaysTheNumberAndNotJustThatSomethingIsOff) {
    // «Esto no parece mesa» a secas no le sirve a nadie: no se sabe si va por
    // poco o por mucho, ni si el siguiente recuadro irá mejor. Con la cifra
    // dentro, el operador arrastra otra vez y ve cómo baja.
    const cv::Mat image = photo("arandelas-1.png");
    if (image.empty()) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    ui::BackgroundPatchDialog dialog(image, vision::SegmentationOptions{});
    dialog.selectPatch(cv::Rect(300, 60, 64, 64));  // encima de una arandela

    auto* verdict = dialog.findChild<QLabel*>(QStringLiteral("verdict"));
    ASSERT_NE(verdict, nullptr);
    std::printf("  [ventana] %s\n", verdict->text().toStdString().c_str());
    ASSERT_FALSE(dialog.sample().looksUniform) << "esta selección ya no se considera mala";
    EXPECT_TRUE(verdict->text().contains(QString::number(
        static_cast<int>(dialog.sample().spread))))
        << "el aviso no lleva la cifra: " << verdict->text().toStdString();
    EXPECT_TRUE(verdict->text().contains(QString::number(
        static_cast<int>(vision::kBackgroundPatchIsUniform))))
        << "el aviso no dice contra qué se compara: " << verdict->text().toStdString();
}
