// LA PESTAÑA DETECCIÓN ENSEÑABA DOCE CONTROLES Y SE USAN SEIS.
//
// Agrupar en cuatro bloques (test_detection_groups) ordenó las veinte filas,
// pero seguían todas delante: doce controles a la vez —umbral, deslizador,
// método, polaridad, clave de color, separar pegadas, brillos, suavizado,
// limpieza, área mínima, área máxima, subpíxel— para un operador que a diario
// toca seis. Cada opción de más es un momento más de duda (ley de Hick), y la
// mitad de las que sobraban cambian lo que se mide.
//
// Ahora quedan seis a la vista y el resto en «Avanzado», plegado. Plegar trae
// dos riesgos, y esta prueba existe por ellos:
//
//   1. Que un CONSEJO encienda algo escondido. «Separar por el canto» y
//      «Separar por color» cambian el método y la clave de color, que viven
//      plegados. Si el grupo siguiera cerrado, el operador no vería qué se ha
//      tocado ni dónde deshacerlo — y la mesa roja avisaría a nadie.
//   2. Que el grupo se cierre en cada apertura aunque el técnico lo quiera
//      abierto. Se recuerda en los ajustes, pero SOLO cuando lo decide el
//      operador: que un consejo lo abra una vez no es una preferencia.

#include <gtest/gtest.h>

#include <QAbstractSlider>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>

#include <cstdio>
#include <memory>

#include "database/db.h"
#include "database/schema.h"
#include "repositories/settings_repository.h"
#include "ui/detection_page.h"

using namespace pci;

namespace {

ui::DetectionPage* freshPage(vision::SegmentationOptions options = {},
                             bool subpixel = false) {
    return new ui::DetectionPage(options, nullptr, nullptr, 0, 0.005, 0.9, subpixel);
}

// Los controles que el operador puede tocar sin abrir nada.
int visibleInputs(const ui::DetectionPage& page) {
    int visible = 0;
    const auto count = [&](const auto& widgets) {
        for (auto* widget : widgets) {
            if (widget->isVisibleTo(&page)) {
                ++visible;
            }
        }
    };
    count(page.findChildren<QComboBox*>());
    count(page.findChildren<QCheckBox*>());
    count(page.findChildren<QAbstractSpinBox*>());
    count(page.findChildren<QAbstractSlider*>());
    return visible;
}

// La mesa del puesto con la pieza recortada por el corte: el caso en que la
// lectura de la escena aconseja separar por el canto (tornillos-1.png).
vision::SceneReading clippedScene() {
    vision::SceneReading reading;
    reading.backgroundLevel = 255.0;
    reading.brightSideIsUnmeasurable = true;
    reading.darkerThanBackground = 0.41;
    reading.thresholdSwing = 0.368;
    reading.thresholdCutsThePiece = true;
    reading.aSingleCutCannotDoIt = true;
    return reading;
}

struct TempSettings {
    QTemporaryDir dir;
    std::unique_ptr<database::Db> db;
    std::unique_ptr<repositories::SettingsRepository> settings;

    TempSettings() {
        auto opened = database::Db::open(
            QDir(dir.path()).filePath(QStringLiteral("advanced.db")).toStdString());
        if (opened.isOk()) {
            db = std::move(opened.value());
            if (database::migrate(*db).isOk()) {
                settings = std::make_unique<repositories::SettingsRepository>(*db);
            }
        }
    }
};

}  // namespace

TEST(DetectionAdvanced, OnlyTheEverydayControlsAreInSightByDefault) {
    const std::unique_ptr<ui::DetectionPage> page(freshPage());
    auto* panel = page->findChild<QWidget*>(QStringLiteral("advancedPanel"));
    ASSERT_NE(panel, nullptr) << "no hay grupo «Avanzado»";
    EXPECT_FALSE(page->advancedOpen()) << "«Avanzado» nace abierto";
    EXPECT_TRUE(panel->isHidden());

    const int visible = visibleInputs(*page);
    std::printf("  [detección] controles a la vista sin abrir nada: %d (antes 12)\n",
                visible);
    EXPECT_LE(visible, 6) << "la pestaña vuelve a enseñarlo todo a la vez";

    // Lo que se toca a diario sigue delante: el área mínima y el suavizado.
    int dailyAtHand = 0;
    for (auto* box : page->findChildren<QDoubleSpinBox*>()) {
        if (box->value() < 5.0 && box->isVisibleTo(page.get())) {
            ++dailyAtHand;  // el área mínima (0,50 %)
        }
    }
    EXPECT_EQ(dailyAtHand, 1) << "el área mínima se ha quedado plegada";
    // Y lo que cambia la medida del puesto, plegado.
    auto* subpixel = page->findChild<QCheckBox*>(QStringLiteral("subpixelCheck"));
    ASSERT_NE(subpixel, nullptr);
    EXPECT_TRUE(panel->isAncestorOf(subpixel));
    EXPECT_TRUE(panel->isAncestorOf(page->findChild<QComboBox*>(QStringLiteral("backgroundKey"))));
}

TEST(DetectionAdvanced, TheColourAdviceOpensTheGroupSoItIsSeen) {
    TempSettings temp;
    ASSERT_NE(temp.settings, nullptr);
    const std::unique_ptr<ui::DetectionPage> page(freshPage());
    page->rememberAdvancedIn(temp.settings.get());
    ASSERT_FALSE(page->advancedOpen());

    page->setBackgroundColour(cv::Vec3b(77, 63, 238));  // el cartón rojo del banco
    auto* key = page->findChild<QComboBox*>(QStringLiteral("backgroundKey"));
    auto* button = page->findChild<QPushButton*>(QStringLiteral("useColourButton"));
    ASSERT_NE(key, nullptr);
    ASSERT_NE(button, nullptr);
    std::printf("  [detección] mesa roja: grupo %s, botón %s\n",
                page->advancedOpen() ? "abierto" : "CERRADO",
                button->isVisibleTo(page.get()) ? "a la vista" : "ESCONDIDO");
    EXPECT_TRUE(button->isVisibleTo(page.get()))
        << "hay consejo de color y el botón que lo aplica está plegado donde no se ve";

    button->click();
    EXPECT_NE(key->currentIndex(), 0) << "el botón del consejo no encendió la clave";
    EXPECT_TRUE(key->isVisibleTo(page.get()))
        << "el consejo encendió la clave de color y el control que lo enseña sigue "
           "escondido: el operador no sabe qué se ha tocado";

    // Que lo abra un consejo NO es la preferencia del operador.
    EXPECT_EQ(temp.settings->getInt("detection_advanced_open", 0).valueOr(-1), 0)
        << "el consejo abrió el grupo y eso se guardó como si lo hubiera elegido él";
}

TEST(DetectionAdvanced, TheEdgeAdviceShowsTheMethodItChanges) {
    const std::unique_ptr<ui::DetectionPage> page(freshPage());
    page->setSceneReading(clippedScene());
    auto* button = page->findChild<QPushButton*>(QStringLiteral("useEdgesButton"));
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(button->isVisibleTo(page.get()))
        << "la pieza recortada pide el canto y el botón que lo aplica no se ve";
    button->click();
    EXPECT_EQ(page->options().method, vision::SegmentationMethod::Edges);

    QComboBox* method = nullptr;
    for (auto* combo : page->findChildren<QComboBox*>()) {
        if (combo->count() == 2 &&
            combo->currentIndex() == static_cast<int>(vision::SegmentationMethod::Edges)) {
            method = combo;
        }
    }
    ASSERT_NE(method, nullptr) << "no se encuentra el selector de método";
    EXPECT_TRUE(method->isVisibleTo(page.get()))
        << "el consejo cambió el método y el selector sigue plegado";
}

TEST(DetectionAdvanced, TheOperatorsChoiceIsRememberedBetweenOpenings) {
    TempSettings temp;
    ASSERT_NE(temp.settings, nullptr);
    {
        const std::unique_ptr<ui::DetectionPage> page(freshPage());
        page->rememberAdvancedIn(temp.settings.get());
        auto* toggle = page->findChild<QToolButton*>(QStringLiteral("advancedToggle"));
        ASSERT_NE(toggle, nullptr);
        toggle->click();
        ASSERT_TRUE(page->advancedOpen());
    }
    {
        const std::unique_ptr<ui::DetectionPage> page(freshPage());
        page->rememberAdvancedIn(temp.settings.get());
        EXPECT_TRUE(page->advancedOpen()) << "se abrió «Avanzado» y la próxima vez sale "
                                             "cerrado otra vez";
        page->findChild<QToolButton*>(QStringLiteral("advancedToggle"))->click();
        ASSERT_FALSE(page->advancedOpen());
    }
    {
        const std::unique_ptr<ui::DetectionPage> page(freshPage());
        page->rememberAdvancedIn(temp.settings.get());
        EXPECT_FALSE(page->advancedOpen()) << "se cerró y vuelve a salir abierto";
    }
}

TEST(DetectionAdvanced, TheTitleSaysHowManyFoldedSettingsAreNotFactory) {
    // Plegado no puede querer decir invisible: el subpíxel y el método cambian
    // lo que se mide, y quien abre la pestaña tiene que saber que hay algo tocado
    // ahí dentro sin tener que desplegarlo para comprobarlo.
    vision::SegmentationOptions touched;
    touched.method = vision::SegmentationMethod::Edges;
    const std::unique_ptr<ui::DetectionPage> page(freshPage(touched, true));
    auto* toggle = page->findChild<QToolButton*>(QStringLiteral("advancedToggle"));
    ASSERT_NE(toggle, nullptr);
    std::printf("  [detección] título: «%s»\n", toggle->text().toStdString().c_str());
    EXPECT_EQ(page->advancedChanges(), 2);
    EXPECT_TRUE(toggle->text().contains(QLatin1Char('2')))
        << "hay dos ajustes plegados fuera de fábrica y el título no lo dice";

    const std::unique_ptr<ui::DetectionPage> factory(freshPage());
    EXPECT_EQ(factory->advancedChanges(), 0);
    EXPECT_FALSE(factory->findChild<QToolButton*>(QStringLiteral("advancedToggle"))
                     ->text()
                     .contains(QLatin1Char('(')))
        << "con todo de fábrica, el título avisa de cambios que no hay";
}
