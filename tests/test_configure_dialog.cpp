// Pruebas del panel «Configurar» (C1). Lo que se comprueba no es que el
// diálogo abra, sino que **no haya perdido nada por el camino**: los ajustes
// venían de siete diálogos repartidos, y un refactor que unifica es justo donde
// una pestaña se queda vacía o un control deja de leer su valor sin que nadie
// lo note.
#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>

#include "ui/configure_dialog.h"
#include "ui/detection_page.h"
#include "ui/preferences_page.h"

using namespace pci::ui;

namespace {

ConfigureDialog::Inputs sampleInputs() {
    ConfigureDialog::Inputs inputs;
    inputs.segmentation.manualThreshold = 137;
    inputs.segmentation.polarity = pci::vision::SegmentationPolarity::LightPiece;
    inputs.segmentation.blurKernel = 7;
    inputs.segmentation.morphKernel = 9;
    inputs.autoIntervalMs = 1300;
    inputs.kSigma = 2.4;
    return inputs;
}

}  // namespace

TEST(ConfigureDialog, EveryTabHasANameAndSomethingInside) {
    ConfigureDialog dialog(sampleInputs());
    auto* tabs = dialog.findChild<QTabWidget*>();
    ASSERT_NE(tabs, nullptr);

    // Cámara e imagen, Detección, Rendimiento, Escala, Preferencias, Atajos.
    EXPECT_EQ(tabs->count(), 6);
    for (int i = 0; i < tabs->count(); ++i) {
        EXPECT_FALSE(tabs->tabText(i).isEmpty()) << "pestaña " << i << " sin nombre";
        auto* page = tabs->widget(i);
        ASSERT_NE(page, nullptr) << "pestaña " << i;
        // Una pestaña vacía es una funcionalidad que se quedó por el camino:
        // se exige que cada una tenga controles o, como mínimo, un texto que
        // explique por qué no los tiene ahora.
        EXPECT_FALSE(page->findChildren<QWidget*>().isEmpty())
            << tabs->tabText(i).toStdString() << " está vacía";
    }
}

TEST(ConfigureDialog, TheDetectionPageArrivesWithTheValuesItWasGiven) {
    ConfigureDialog dialog(sampleInputs());
    auto* page = dialog.detectionPage();
    ASSERT_NE(page, nullptr);

    const auto read = page->options();
    EXPECT_EQ(read.manualThreshold, 137);
    EXPECT_EQ(read.polarity, pci::vision::SegmentationPolarity::LightPiece);
    EXPECT_EQ(read.blurKernel, 7);
    EXPECT_EQ(read.morphKernel, 9);
}

TEST(ConfigureDialog, MovingTheThresholdChangesWhatThePageReports) {
    // Sin esto, la página podría estar devolviendo el valor con el que nació y
    // nadie se enteraría de que el deslizador no hace nada.
    ConfigureDialog dialog(sampleInputs());
    auto* page = dialog.detectionPage();
    ASSERT_NE(page, nullptr);
    auto* slider = page->findChild<QSlider*>();
    ASSERT_NE(slider, nullptr);

    slider->setValue(64);
    EXPECT_EQ(page->options().manualThreshold, 64);

    // Y con el umbral automático marcado se ignora el deslizador: -1 es "Otsu".
    auto* automatic = page->findChild<QCheckBox*>();
    ASSERT_NE(automatic, nullptr);
    automatic->setChecked(true);
    EXPECT_EQ(page->options().manualThreshold, -1);
}

TEST(ConfigureDialog, ThePreferencesPageRoundTripsItsValues) {
    ConfigureDialog dialog(sampleInputs());
    auto* page = dialog.preferencesPage();
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->autoIntervalMs(), 1300);
    EXPECT_DOUBLE_EQ(page->kSigma(), 2.4);

    auto* interval = page->findChild<QSpinBox*>();
    ASSERT_NE(interval, nullptr);
    interval->setValue(2500);
    EXPECT_EQ(page->autoIntervalMs(), 2500);
}

TEST(ConfigureDialog, WithoutACameraThePageExplainsInsteadOfShowingDeadSliders) {
    // Los controles se le preguntan a la cámara al abrirla: sin cámara no se
    // sabe cuáles admite, y unos deslizadores que no harían nada son peores que
    // un texto que lo diga.
    ConfigureDialog dialog(sampleInputs());
    EXPECT_EQ(dialog.cameraPage(), nullptr);

    auto* tabs = dialog.findChild<QTabWidget*>();
    ASSERT_NE(tabs, nullptr);
    auto* page = tabs->widget(0);
    ASSERT_NE(page, nullptr);
    EXPECT_TRUE(page->findChildren<QSlider*>().isEmpty());
    const auto labels = page->findChildren<QLabel*>();
    ASSERT_FALSE(labels.isEmpty());
    EXPECT_TRUE(labels.first()->text().contains(QStringLiteral("cámara")))
        << labels.first()->text().toStdString();
}

TEST(ConfigureDialog, TheOpenTabIsRememberedAcrossOpenings) {
    // Quien está peleando con la iluminación vuelve diez veces a la misma
    // pestaña; empezar siempre en la primera es una fricción tonta.
    ConfigureDialog dialog(sampleInputs());
    dialog.setCurrentTab(3);
    EXPECT_EQ(dialog.currentTab(), 3);

    // Un índice imposible no mueve nada ni revienta: el número guardado puede
    // venir de una versión con menos pestañas.
    dialog.setCurrentTab(99);
    EXPECT_EQ(dialog.currentTab(), 3);
    dialog.setCurrentTab(-1);
    EXPECT_EQ(dialog.currentTab(), 3);
}

TEST(ConfigureDialog, TheWizardTabsAskForTheirAssistant) {
    // Escala y Atajos no son formularios: su pestaña explica y abre el
    // asistente de siempre. Lo que se comprueba es que el botón esté conectado.
    ConfigureDialog dialog(sampleInputs());
    int scaleAsked = 0;
    int shortcutsAsked = 0;
    QObject::connect(&dialog, &ConfigureDialog::scaleWizardRequested,
                     [&scaleAsked] { ++scaleAsked; });
    QObject::connect(&dialog, &ConfigureDialog::shortcutsRequested,
                     [&shortcutsAsked] { ++shortcutsAsked; });

    auto* tabs = dialog.findChild<QTabWidget*>();
    ASSERT_NE(tabs, nullptr);
    for (const int index : {3, 5}) {
        auto* button = tabs->widget(index)->findChild<QPushButton*>();
        ASSERT_NE(button, nullptr) << "la pestaña " << index << " no abre nada";
        button->click();
    }
    EXPECT_EQ(scaleAsked, 1);
    EXPECT_EQ(shortcutsAsked, 1);
}

TEST(ConfigureDialog, ApplyingAsksTheWindowToReadThePages) {
    // El panel no aplica nada por su cuenta: avisa, y la ventana decide. Si esta
    // señal no llegara, tocar los ajustes no tendría efecto ninguno.
    ConfigureDialog dialog(sampleInputs());
    int applied = 0;
    QObject::connect(&dialog, &ConfigureDialog::applied, [&applied] { ++applied; });

    // Por ROL y no por texto: Qt traduce los botones estándar según el idioma
    // del sistema, y buscar "Aplicar" haría que la prueba pasara o fallara
    // según en qué máquina se ejecute.
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(buttons, nullptr);
    auto* apply = buttons->button(QDialogButtonBox::Apply);
    ASSERT_NE(apply, nullptr);
    apply->click();
    EXPECT_EQ(applied, 1);

    // Aceptar aplica también, y además cierra: sin eso, el operador que pulsa
    // Aceptar creería haber guardado y no habría guardado nada.
    auto* ok = buttons->button(QDialogButtonBox::Ok);
    ASSERT_NE(ok, nullptr);
    ok->click();
    EXPECT_EQ(applied, 2);
    EXPECT_FALSE(dialog.isVisible());
}
