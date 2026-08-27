// EL AVISO DE MESA DE COLOR: QUE SALGA DONDE HACE FALTA Y SE CALLE DONDE NO.
//
// La segunda mitad importa tanto como la primera. Un aviso que aparece en todas
// las escenas se aprende a ignorar en dos días, y entonces tampoco sirve donde
// sí hacía falta. Por eso se comprueban los dos lados.
//
// El aviso existe porque la clave de color nace apagada —cambia lo que se mide,
// así que se elige— y una opción apagada que nadie sabe que existe es una opción
// que no existe. Quien la necesita es justo el que no va a ir a buscarla: está
// viendo que «no detecta bien» y no tiene por qué sospechar del color de su mesa.

#include <gtest/gtest.h>

#include <QLabel>
#include <QPushButton>

#include <cstdio>

#include "ui/detection_page.h"

using namespace pci;

namespace {

// Por NOMBRE y no por texto. Buscarlos por lo que dicen ataba estas pruebas a
// la redacción exacta de la etiqueta, y al acortarlas —que era lo que se
// pedía— se rompieron. Una prueba que se rompe al reescribir una etiqueta
// desanima a reescribir etiquetas.
QLabel* colourHintOf(const ui::DetectionPage& page) {
    return page.findChild<QLabel*>(QStringLiteral("colourHint"));
}

QPushButton* colourButtonOf(const ui::DetectionPage& page) {
    return page.findChild<QPushButton*>(QStringLiteral("useColourButton"));
}

}  // namespace

TEST(ColouredTableAdvice, ARedTableGetsToldAndOffersTheButton) {
    ui::DetectionPage page(vision::SegmentationOptions{}, nullptr, nullptr, 0, 0.005, 0.9,
                           false);
    // El rojo del cartón del banco de fotos, en BGR.
    page.setBackgroundColour(cv::Vec3b(77, 63, 238));

    auto* hint = colourHintOf(page);
    ASSERT_NE(hint, nullptr) << "con la mesa roja no aparece ningún aviso: la opción que lo "
                                "arregla sigue existiendo sin que nadie se entere";
    EXPECT_TRUE(hint->isVisible() || hint->isVisibleTo(&page))
        << "el aviso existe pero no se enseña";
    std::printf("  [mesa] %s\n", hint->text().left(90).toStdString().c_str());

    // Con la cifra dentro: «prueba esto» es una corazonada, «saturación 0,74» es
    // algo que el operador puede comprobar mirando su propia mesa.
    EXPECT_TRUE(hint->text().contains(QStringLiteral("0.74")) ||
                hint->text().contains(QStringLiteral("0,74")))
        << "el aviso no dice cuánto color tiene la mesa: " << hint->text().toStdString();

    auto* button = colourButtonOf(page);
    ASSERT_NE(button, nullptr) << "se avisa del problema y no se ofrece el arreglo";
    EXPECT_FALSE(button->autoDefault())
        << "el botón de la sugerencia se lleva el Enter de la página";
}

TEST(ColouredTableAdvice, AWhiteTableIsLeftAlone) {
    ui::DetectionPage page(vision::SegmentationOptions{}, nullptr, nullptr, 0, 0.005, 0.9,
                           false);
    // El blanco de las siete fotos de mesa clara del banco: saturación 0,00-0,02.
    page.setBackgroundColour(cv::Vec3b(248, 244, 243));

    auto* hint = colourHintOf(page);
    const bool showing = hint != nullptr && !hint->text().isEmpty() &&
                         hint->isVisibleTo(&page);
    std::printf("  [mesa] blanca -> %s\n", showing ? "AVISA (mal)" : "se calla");
    EXPECT_FALSE(showing)
        << "sobre una mesa blanca sale el aviso de color. Un aviso que aparece en todas "
           "las escenas se aprende a ignorar, y entonces tampoco sirve donde hacía falta.";
}

TEST(ColouredTableAdvice, ItStopsNaggingOnceTheKeyIsOn) {
    // Si ya está encendida, el aviso sobra: estaría proponiendo lo que ya se
    // está haciendo, que es la forma más rápida de que se deje de leer.
    vision::SegmentationOptions already;
    already.backgroundKey = vision::SegmentationOptions::BackgroundKey::Auto;
    ui::DetectionPage page(already, nullptr, nullptr, 0, 0.005, 0.9, false);
    page.setBackgroundColour(cv::Vec3b(77, 63, 238));

    auto* hint = colourHintOf(page);
    const bool showing = hint != nullptr && !hint->text().isEmpty() &&
                         hint->isVisibleTo(&page);
    std::printf("  [mesa] roja con la clave YA encendida -> %s\n",
                showing ? "sigue avisando (mal)" : "se calla");
    EXPECT_FALSE(showing) << "sigue aconsejando encender algo que ya está encendido";
}
