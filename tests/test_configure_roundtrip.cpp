// ABRIR CONFIGURAR Y ACEPTAR SIN TOCAR NADA NO PUEDE CAMBIAR NADA.
//
// Queja del usuario: «el menú de configuración tiene bastantes fallos en sus
// características y configuraciones». Hay una familia de fallos que encaja
// exactamente con eso y que no se ve mirando la pantalla: un valor que la
// ventana enseña pero no devuelve, o que devuelve distinto de como lo recibió.
//
// El síntoma es de los peores: entras a cambiar el umbral, aceptas, y de paso
// se te ha reseteado la polaridad o el área mínima. Nadie lo relaciona con la
// ventana de ajustes; se nota semanas después como «la detección va peor desde
// hace un tiempo».
//
// Se comprueba página por página, con valores DISTINTOS de los de fábrica en
// todos los campos: si un campo se cae por el camino, vuelve el valor de
// fábrica y la comparación lo ve. Con los valores por defecto puestos, un campo
// perdido dar\u00eda el mismo resultado que uno conservado.

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QTemporaryDir>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "database/db.h"
#include "database/schema.h"
#include "repositories/settings_repository.h"
#include "ui/configure_dialog.h"
#include "ui/detection_page.h"
#include "ui/main_window.h"
#include "ui/pieces_page.h"
#include "ui/preferences_page.h"
#include "vision/segmentation.h"

TEST(ConfigureRoundTrip, TheDetectionPageGivesBackEverythingItWasGiven) {
    // Todo distinto de fábrica: método por canto, umbral manual, polaridad
    // fijada, kernels impares distintos de 5, y áreas movidas.
    pci::vision::SegmentationOptions given;
    given.method = pci::vision::SegmentationMethod::Edges;
    given.manualThreshold = 137;
    given.polarity = pci::vision::SegmentationPolarity::DarkPiece;
    given.blurKernel = 7;
    given.morphKernel = 3;

    const double minArea = 0.021;
    const double maxArea = 0.77;
    const bool subpixel = true;

    pci::ui::DetectionPage page(given, nullptr, nullptr, 0, minArea, maxArea, subpixel);
    const pci::vision::SegmentationOptions back = page.options();

    EXPECT_EQ(static_cast<int>(back.method), static_cast<int>(given.method))
        << "el método de segmentación no vuelve: aceptar te lo cambia";
    EXPECT_EQ(back.manualThreshold, given.manualThreshold)
        << "el umbral manual no vuelve";
    EXPECT_EQ(static_cast<int>(back.polarity), static_cast<int>(given.polarity))
        << "la polaridad no vuelve: aceptar la devuelve a automática";
    EXPECT_EQ(back.blurKernel, given.blurKernel) << "el suavizado no vuelve";
    EXPECT_EQ(back.morphKernel, given.morphKernel) << "la morfología no vuelve";

    // Las áreas se enseñan en porcentaje con un decimal, así que un 0,021 puede
    // volver como 0,021 exacto o redondeado a la resolución del campo; lo que
    // no puede es volver como el valor de fábrica.
    EXPECT_NEAR(page.minAreaFraction(), minArea, 0.0006) << "el área mínima no vuelve";
    EXPECT_NEAR(page.maxAreaFraction(), maxArea, 0.0006) << "el área máxima no vuelve";
    EXPECT_EQ(page.subpixelEdges(), subpixel) << "el afinado subpíxel no vuelve";
}

TEST(ConfigureRoundTrip, TheDetectionPageDoesNotConfuseAutomaticWithZero) {
    // -1 significa «umbral automático» y 0 significa «umbral manual en 0», que
    // es negro puro. Son cosas distintas y el campo las junta en un solo
    // número: si el redondeo o la casilla las mezclan, la detección pasa de
    // adaptarse a cada imagen a no encontrar nada, sin decir por qué.
    pci::vision::SegmentationOptions automatic;
    automatic.manualThreshold = -1;
    pci::ui::DetectionPage autoPage(automatic, nullptr, nullptr, 0);
    EXPECT_LT(autoPage.options().manualThreshold, 0)
        << "el umbral automático vuelve como un número manual";

    pci::vision::SegmentationOptions manualZero;
    manualZero.manualThreshold = 0;
    pci::ui::DetectionPage zeroPage(manualZero, nullptr, nullptr, 0);
    EXPECT_EQ(zeroPage.options().manualThreshold, 0)
        << "un umbral manual de 0 se convierte en automático";
}

TEST(ConfigureRoundTrip, ThePiecesPageGivesBackWhatItWasGiven) {
    // Manual con siete piezas y mosaico encendido: los dos distintos de fábrica
    // (automático y sin mosaico).
    pci::ui::PiecesPage page(7);
    page.setShowMosaic(true);
    EXPECT_EQ(page.expectedPieces(), 7) << "el número de piezas no vuelve";
    EXPECT_EQ(page.countMode(), pci::ui::PiecesPage::CountMode::Manual)
        << "el modo manual vuelve como automático";
    EXPECT_TRUE(page.showMosaic()) << "el mosaico no vuelve";

    // Y el automático se distingue del manual con una: cero no es uno.
    pci::ui::PiecesPage automatic(0);
    EXPECT_EQ(automatic.expectedPieces(), 0)
        << "el modo automático vuelve como «manual, una pieza», que es otra cosa: "
           "una apaga la enumeración y el otro cuenta lo que haya";
    EXPECT_EQ(automatic.countMode(), pci::ui::PiecesPage::CountMode::Automatic);
}

TEST(ConfigureRoundTrip, ThePreferencesPageGivesBackWhatItWasGiven) {
    const int interval = 1234;
    const double sigma = 2.75;
    pci::ui::PreferencesPage page(interval, sigma);
    EXPECT_EQ(page.autoIntervalMs(), interval) << "el intervalo de auto no vuelve";
    EXPECT_NEAR(page.kSigma(), sigma, 1e-9) << "la sensibilidad no vuelve";
}

// Y EL CAMINO COMPLETO: ventana real, abrir Configurar, aceptar sin tocar.
//
// Las pruebas de arriba miran cada página por separado, que es donde suele
// caerse un campo. Pero el recorrido de verdad tiene dos tramos más: la ventana
// RELLENA los `Inputs` con lo que tiene, y luego LEE la página para aplicarlo.
// Un campo puede volver bien de su página y perderse igual en cualquiera de los
// dos, y entonces las pruebas de página siguen verdes mientras el operador
// pierde el ajuste.
//
// Se comprueba sobre los ajustes guardados, que es donde acaba todo y lo que
// sobrevive al siguiente arranque.
TEST(ConfigureRoundTrip, AcceptingWithoutTouchingAnythingChangesNothingOnDisk) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string dbPath =
        QDir(dir.path()).filePath(QStringLiteral("cfg.db")).toStdString();
    auto opened = pci::database::Db::open(dbPath);
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::SettingsRepository settings(*db);

    // Valores distintos de fábrica en todo lo que la ventana persiste. Con los
    // de fábrica puestos, un ajuste perdido daría el mismo resultado que uno
    // conservado y la prueba pasaría sin comprobar nada.
    const std::vector<std::pair<std::string, int>> ints = {
        {"det_threshold", 137}, {"det_polarity", 1}, {"det_blur", 7},
        {"det_morph", 3},       {"det_subpixel", 1}, {"pref_auto_interval_ms", 1234},
    };
    for (const auto& [key, value] : ints) {
        ASSERT_TRUE(settings.setInt(key, value).isOk());
    }
    ASSERT_TRUE(settings.setDouble("pref_ksigma", 2.75).isOk());

    pci::ui::AppRepositories repos;
    repos.settings = &settings;

    const auto snapshot = [&] {
        std::map<std::string, std::string> all;
        auto listed = settings.listAll();
        EXPECT_TRUE(listed.isOk());
        if (listed.isOk()) {
            for (const auto& [key, value] : listed.value()) {
                all[key] = value;
            }
        }
        return all;
    };

    std::map<std::string, std::string> before;
    std::map<std::string, std::string> after;
    {
        pci::ui::MainWindow window(repos);
        window.resize(1200, 800);
        before = snapshot();

        // Abrir Configurar y aplicar, sin tocar un solo control.
        QAction* configure = nullptr;
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text().startsWith(QStringLiteral("Configurar"))) {
                configure = action;
            }
        }
        ASSERT_NE(configure, nullptr) << "no está la acción de Configurar";
        configure->trigger();

        pci::ui::ConfigureDialog* dialog = window.findChild<pci::ui::ConfigureDialog*>();
        ASSERT_NE(dialog, nullptr) << "Configurar no abrió ninguna ventana";
        emit dialog->applied();
        after = snapshot();
    }

    // Se comparan clave a clave para poder decir CUÁL se movió: un «no son
    // iguales» a secas obligaría a volver a investigarlo desde cero.
    for (const auto& [key, value] : before) {
        const auto found = after.find(key);
        ASSERT_NE(found, after.end()) << "aceptar borró el ajuste «" << key << "»";
        EXPECT_EQ(found->second, value)
            << "aceptar sin tocar nada cambió «" << key << "»: era «" << value
            << "» y quedó «" << found->second << "»";
    }
}
