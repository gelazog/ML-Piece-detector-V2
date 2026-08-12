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
#include "ui/performance_page.h"
#include "ui/preferences_page.h"
#include "ui/rate_readout.h"

using namespace pci::ui;
using pci::vision::WorkingZoneMode;

namespace {

// Las pestañas se buscan por NOMBRE y no por posición. Se probó con índices y
// se rompió dos veces seguidas al añadir páginas nuevas: un test que hay que
// reparar cada vez que crece lo que prueba no está protegiendo nada.
int tabNamed(QTabWidget* tabs, const QString& name) {
    for (int i = 0; i < tabs->count(); ++i) {
        if (tabs->tabText(i).contains(name, Qt::CaseInsensitive)) {
            return i;
        }
    }
    return -1;
}

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

    // Todas las páginas previstas tienen que estar, cada una con su nombre.
    for (const auto* expected :
         {"Cámara", "Detección", "Piezas", "Rendimiento", "Escala", "Preferencias",
          "Atajos"}) {
        EXPECT_GE(tabNamed(tabs, QString::fromUtf8(expected)), 0)
            << "falta la pestaña " << expected;
    }
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
    const int index = tabNamed(tabs, QStringLiteral("Cámara"));
    ASSERT_GE(index, 0);
    auto* page = tabs->widget(index);
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
    for (const auto* name : {"Escala", "Atajos"}) {
        const int index = tabNamed(tabs, QString::fromUtf8(name));
        ASSERT_GE(index, 0) << name;
        auto* button = tabs->widget(index)->findChild<QPushButton*>();
        ASSERT_NE(button, nullptr) << "la pestaña " << name << " no abre nada";
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

TEST(ConfigureDialog, TheAreaFractionsAreEditableAndRoundTrip) {
    // Decidían la frontera entre "no hay pieza" y "hay pieza" y estaban fijas
    // en el código: con piezas pequeñas, el 0,5 % por defecto es justo esa
    // frontera y no se podía mover sin recompilar.
    auto inputs = sampleInputs();
    inputs.minAreaFraction = 0.02;
    inputs.maxAreaFraction = 0.75;
    ConfigureDialog dialog(inputs);
    auto* page = dialog.detectionPage();
    ASSERT_NE(page, nullptr);

    EXPECT_NEAR(page->minAreaFraction(), 0.02, 1e-9);
    EXPECT_NEAR(page->maxAreaFraction(), 0.75, 1e-9);

    // Y lo que el operador cambie se lee de vuelta: se muestran en porcentaje
    // pero se entregan en fracción, que es lo que espera el pipeline.
    const auto spins = page->findChildren<QDoubleSpinBox*>();
    ASSERT_GE(spins.size(), 2);
    spins.at(0)->setValue(0.10);
    EXPECT_NEAR(page->minAreaFraction(), 0.001, 1e-9);
}

TEST(PerformancePage, SyncingTheModeFromOutsideDoesNotEmitItBack) {
    // Dibujar la zona sobre el vídeo cambia el modo por sí solo, y el panel
    // tiene que enterarse. Pero si al ponerlo al día reemitiera `modeChanged`,
    // la ventana volvería a llamar al panel y se realimentarían — el mismo
    // motivo por el que la paleta distingue `activate` de `showSelection`.
    PerformancePage page(WorkingZoneMode::Off, false);
    int announced = 0;
    QObject::connect(&page, &PerformancePage::modeChanged,
                     [&announced](WorkingZoneMode) { ++announced; });

    page.showMode(WorkingZoneMode::Fixed, true);
    EXPECT_EQ(page.mode(), WorkingZoneMode::Fixed);
    EXPECT_EQ(announced, 0) << "sincronizar no es elegir";

    page.showMode(WorkingZoneMode::Automatic, true);
    EXPECT_EQ(page.mode(), WorkingZoneMode::Automatic);
    EXPECT_EQ(announced, 0);
}

TEST(PerformancePage, TheFixedModeIsNotOfferedWithoutAZoneDrawn) {
    // Ofrecer «zona fija» sin zona sería ofrecer un modo que no hace nada.
    PerformancePage page(WorkingZoneMode::Fixed, false);
    EXPECT_EQ(page.mode(), WorkingZoneMode::Off)
        << "sin zona dibujada, «fija» no puede quedar seleccionada";

    // Y en cuanto se dibuja una, pasa a estar disponible.
    page.showMode(WorkingZoneMode::Fixed, true);
    EXPECT_EQ(page.mode(), WorkingZoneMode::Fixed);
}

// ---------------------------------------------------------------------------
// Los fps que importan (R1)
// ---------------------------------------------------------------------------

TEST(RateReadout, StaysShortWhileTheAnalysisKeepsUp) {
    // La forma corta es la de siempre, y tiene que seguir siendo la habitual:
    // un indicador que enseña tres numeros a todas horas se deja de leer, y
    // entonces tampoco avisa el dia que hay algo que ver.
    const QString text = formatRates(1280, 720, 30.0, 30.0, 0.0);
    EXPECT_EQ(text, QStringLiteral("1280x720 — 30.0 fps"));
    EXPECT_FALSE(text.contains(QStringLiteral("analiza")));
}

TEST(RateReadout, SaysWhatIsHappeningWhenTheAnalysisFallsBehind) {
    // El escenario que justifica el item entero: la camara va a 30 y el
    // analisis a 8. En pantalla se ve fluido —el video no depende del
    // analisis— y se esta midiendo uno de cada cuatro frames. Hasta ahora eso
    // no aparecia en ningun sitio.
    const QString text = formatRates(1280, 720, 30.0, 8.0, 22.0);
    EXPECT_TRUE(text.contains(QStringLiteral("30.0 fps"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("analiza 8.0"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("descarta 22"))) << text.toStdString();
}

TEST(RateReadout, AStrayDroppedFrameIsNotWorthAlarming) {
    // Dos contadores por ventana deslizante no dan lo mismo aunque el analisis
    // siga el ritmo: basta con que un frame caiga al otro lado del borde de la
    // ventana. Un descarte suelto es aliasing de la medida, no un problema, y
    // enseñarlo entrenaria al operador a ignorar el aviso de verdad.
    EXPECT_FALSE(formatRates(640, 480, 30.0, 29.0, 1.0)
                     .contains(QStringLiteral("descarta")));
    // Pero dos por segundo ya son 120 frames a la hora sin medir: eso se dice.
    EXPECT_TRUE(formatRates(640, 480, 30.0, 28.0, 2.0)
                    .contains(QStringLiteral("descarta")));
}

TEST(RateReadout, WithNothingToAnalyseItDoesNotInventADisaster) {
    // Con el contorno oculto el analisis esta parado A PROPOSITO. Enseñar
    // "analiza 0 · descarta 30" ahi seria llamar averia a lo que el operador
    // acaba de pedir, que es la forma mas rapida de que deje de creerse la
    // barra de estado.
    const QString text = formatRates(640, 480, 30.0, 0.0, -1.0);
    EXPECT_EQ(text, QStringLiteral("640x480 — 30.0 fps"));
}

TEST(FrameAccounting, EveryFrameIsEitherMeasuredOrDropped) {
    // El escenario que pedia el plan, simulado con reloj inyectado: camara a 30
    // fps, analisis a 8. Se comprueba la cuenta EXACTA, no que "suene bien".
    //
    // La invariante es la que sostiene todo el indicador: cada frame que llega
    // o se mide o se descarta. Si esa suma no cuadra, el numero que ve el
    // operador esta mintiendo.
    using Clock = pci::ui::FrameAccounting::Clock;
    pci::ui::FrameAccounting frames;
    const Clock::time_point start = Clock::time_point{} + std::chrono::seconds(100);

    // Un segundo justo: 30 frames cada 33,3 ms. El analisis tarda 125 ms, o sea
    // que solo acaba uno de cada cuatro y el resto llegan con el anterior aun
    // pendiente.
    int arrived = 0;
    int analysed = 0;
    bool pending = false;
    Clock::time_point busyUntil = start;
    for (int i = 0; i < 30; ++i) {
        const Clock::time_point now = start + std::chrono::microseconds(33333 * i);
        // Si el analisis anterior ya termino, deja de estar pendiente.
        if (pending && now >= busyUntil) {
            frames.analysisFinished(busyUntil);
            ++analysed;
            pending = false;
        }
        ++arrived;
        frames.frameArrived(/*analysing=*/true, pending, now);
        if (!pending) {
            pending = true;
            busyUntil = now + std::chrono::milliseconds(125);
        }
    }
    const Clock::time_point end = start + std::chrono::milliseconds(999);

    const double measured = frames.analysisFps(end);
    const double dropped = frames.droppedFps(end);
    EXPECT_EQ(arrived, 30);
    EXPECT_NEAR(measured, static_cast<double>(analysed), 0.001);
    // La invariante: lo medido mas lo descartado es todo lo que entro. El
    // margen de 1 es el frame que se quedo analizandose al cerrar la ventana,
    // que no esta ni en un lado ni en el otro todavia.
    EXPECT_NEAR(measured + dropped, static_cast<double>(arrived), 1.001)
        << "medidos " << measured << " + descartados " << dropped
        << " no suman los " << arrived << " que llegaron";
    // Y el reparto es el esperado de un analisis 4x mas lento que la camara.
    EXPECT_LE(measured, 9.0) << measured;
    EXPECT_GE(dropped, 20.0) << dropped;
    std::printf("  camara 30 fps, analisis 125 ms -> mide %.0f, descarta %.0f\n", measured,
                dropped);
}

TEST(FrameAccounting, NothingIsDroppedWhenTheAnalysisKeepsUp) {
    // El caso bueno tiene que dar cero descartes, o el indicador saltaria en
    // una estacion que va perfectamente.
    using Clock = pci::ui::FrameAccounting::Clock;
    pci::ui::FrameAccounting frames;
    const Clock::time_point start = Clock::time_point{} + std::chrono::seconds(100);
    for (int i = 0; i < 30; ++i) {
        const Clock::time_point now = start + std::chrono::microseconds(33333 * i);
        frames.frameArrived(/*analysing=*/true, /*previousStillPending=*/false, now);
        frames.analysisFinished(now + std::chrono::milliseconds(5));
    }
    const Clock::time_point end = start + std::chrono::milliseconds(999);
    EXPECT_DOUBLE_EQ(frames.droppedFps(end), 0.0);
    EXPECT_NEAR(frames.analysisFps(end), 30.0, 1.001);
}

TEST(FrameAccounting, FreezingTheContourIsNotDropping) {
    // Con el contorno oculto no se analiza A PROPOSITO. Contar esos frames como
    // descartados llamaria averia a lo que el operador acaba de pedir.
    using Clock = pci::ui::FrameAccounting::Clock;
    pci::ui::FrameAccounting frames;
    const Clock::time_point start = Clock::time_point{} + std::chrono::seconds(100);
    for (int i = 0; i < 30; ++i) {
        frames.frameArrived(/*analysing=*/false, /*previousStillPending=*/true,
                            start + std::chrono::microseconds(33333 * i));
    }
    EXPECT_DOUBLE_EQ(frames.droppedFps(start + std::chrono::milliseconds(999)), 0.0);
}
