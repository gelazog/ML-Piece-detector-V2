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
#include <QComboBox>
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
#include "repositories/detection_profile_repository.h"
#include "repositories/piece_repository.h"
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
    given.splitTouchingPieces = true;
    given.backgroundKey = pci::vision::SegmentationOptions::BackgroundKey::Fixed;
    given.background = cv::Vec3b(77, 63, 238);  // el rojo del cartón, en BGR

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
    EXPECT_EQ(back.splitTouchingPieces, given.splitTouchingPieces)
        << "la separación de piezas que se tocan no vuelve: aceptar la apaga sola";
    EXPECT_EQ(static_cast<int>(back.backgroundKey), static_cast<int>(given.backgroundKey))
        << "la clave de color de fondo no vuelve: aceptar te devuelve a separar por "
           "claridad, y sobre un fondo de color eso deja de ver la mitad de las piezas";
    // Y el COLOR, canal a canal. Devolverlo al revés —RGB donde se esperaba BGR—
    // no se vería en la ventana, que enseñaría un color parecido, pero
    // segmentaría contra otra cosa: sobre un fondo rojo se estaría midiendo la
    // distancia a un azul.
    EXPECT_EQ(back.background[0], given.background[0]) << "el azul del fondo no vuelve";
    EXPECT_EQ(back.background[1], given.background[1]) << "el verde del fondo no vuelve";
    EXPECT_EQ(back.background[2], given.background[2])
        << "el rojo del fondo no vuelve. Ojo con el orden: OpenCV usa BGR y Qt RGB, y "
           "cruzarlos da un color parecido en pantalla y una segmentación contra otra cosa";

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

// CAMBIAR DE PIEZA CON CONFIGURAR ABIERTO.
//
// La ventana de Configurar es única: volver a pulsarla trae al frente la que ya
// está abierta. Bien pensado — pero la selección de pieza vive FUERA de ella, en
// la barra de arriba, y se puede cambiar con la ventana abierta.
//
// Lo que hay dentro es entonces de la pieza ANTERIOR. Y «piezas esperadas» y
// «ver en mosaico» se guardan CON LA PIEZA, así que aceptar escribe los ajustes
// de la bandeja encima de la pieza suelta que acabas de seleccionar. Sin avisar,
// y sin forma de notarlo hasta que esa pieza empieza a dar NG de recuento.
//
// Es la peor variante del fallo de arriba: no es que se pierda un ajuste, es
// que se le copia a un trabajo que no es el suyo.
TEST(ConfigureRoundTrip, ChangingPieceWithConfigureOpenDoesNotCopySettingsAcross) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string dbPath =
        QDir(dir.path()).filePath(QStringLiteral("piezas.db")).toStdString();
    auto opened = pci::database::Db::open(dbPath);
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::SettingsRepository settings(*db);
    pci::repositories::PieceRepository pieces(*db);

    // Una bandeja de doce con mosaico, y una pieza suelta en automático.
    auto tray = pieces.createPiece("bandeja");
    ASSERT_TRUE(tray.isOk());
    auto single = pieces.createPiece("suelta");
    ASSERT_TRUE(single.isOk());
    {
        auto m = pieces.loadMeasurement(tray.value());
        ASSERT_TRUE(m.isOk());
        m.value().expectedPieces = 12;
        m.value().showMosaic = true;
        ASSERT_TRUE(pieces.saveMeasurement(tray.value(), m.value()).isOk());
    }

    pci::ui::AppRepositories repos;
    repos.settings = &settings;
    repos.pieces = &pieces;

    {
        pci::ui::MainWindow window(repos);
        window.resize(1200, 800);

        auto* combo = window.findChild<QComboBox*>(QStringLiteral("pieceCombo"));
        if (combo == nullptr) {
            // Sin objectName no se puede identificar; se busca el que lleva los
            // nombres de las piezas.
            for (auto* candidate : window.findChildren<QComboBox*>()) {
                if (candidate->findText(QStringLiteral("bandeja")) >= 0) {
                    combo = candidate;
                }
            }
        }
        ASSERT_NE(combo, nullptr) << "no se encontró el selector de piezas";

        // Se selecciona la bandeja y se abre Configurar: la página enseña 12.
        const int trayIndex = combo->findText(QStringLiteral("bandeja"));
        ASSERT_GE(trayIndex, 0);
        combo->setCurrentIndex(trayIndex);

        QAction* configure = nullptr;
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text().startsWith(QStringLiteral("Configurar"))) {
                configure = action;
            }
        }
        ASSERT_NE(configure, nullptr);
        configure->trigger();
        auto* dialog = window.findChild<pci::ui::ConfigureDialog*>();
        ASSERT_NE(dialog, nullptr);
        ASSERT_NE(dialog->piecesPage(), nullptr);
        ASSERT_EQ(dialog->piecesPage()->expectedPieces(), 12)
            << "la página no enseña lo de la bandeja";

        // Ahora se cambia a la pieza suelta SIN cerrar la ventana, y se acepta.
        const int singleIndex = combo->findText(QStringLiteral("suelta"));
        ASSERT_GE(singleIndex, 0);
        combo->setCurrentIndex(singleIndex);
        emit dialog->applied();
    }

    // La pieza suelta tiene que seguir en automático y sin mosaico. Si se le han
    // copiado los 12 de la bandeja, empezará a dar NG de recuento sin que nadie
    // haya declarado nada para ella.
    auto after = pieces.loadMeasurement(single.value());
    ASSERT_TRUE(after.isOk());
    EXPECT_EQ(after.value().expectedPieces, 0)
        << "la pieza suelta se ha quedado con las piezas esperadas de la bandeja";
    EXPECT_FALSE(after.value().showMosaic)
        << "la pieza suelta se ha quedado con el mosaico de la bandeja";

    // Y la bandeja no puede haber perdido lo suyo por el camino.
    auto trayAfter = pieces.loadMeasurement(tray.value());
    ASSERT_TRUE(trayAfter.isOk());
    EXPECT_EQ(trayAfter.value().expectedPieces, 12);
    EXPECT_TRUE(trayAfter.value().showMosaic);
}

// Y LO MISMO CON LA DETECCIÓN, que es peor.
//
// El perfil de detección también se guarda con la pieza. Cambiar de trabajo con
// Configurar abierto y aceptar le asignaba a la pieza nueva el perfil de la
// anterior — y con él, su umbral, su polaridad y sus áreas.
//
// El síntoma es indistinguible de «la detección de esta pieza dejó de
// funcionar»: los ajustes son legítimos, solo que de otra escena.
TEST(ConfigureRoundTrip, ChangingPieceRefreshesTheDetectionPageToo) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string dbPath =
        QDir(dir.path()).filePath(QStringLiteral("perfiles.db")).toStdString();
    auto opened = pci::database::Db::open(dbPath);
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::SettingsRepository settings(*db);
    pci::repositories::PieceRepository pieces(*db);
    pci::repositories::DetectionProfileRepository profiles(*db);

    auto bright = pieces.createPiece("con perfil");
    ASSERT_TRUE(bright.isOk());
    auto plain = pieces.createPiece("sin perfil");
    ASSERT_TRUE(plain.isOk());

    // Un perfil bien distinto de fábrica, asignado solo a la primera pieza.
    pci::vision::SegmentationOptions special;
    special.manualThreshold = 199;
    special.polarity = pci::vision::SegmentationPolarity::LightPiece;
    special.blurKernel = 9;
    auto profileId = profiles.save("luz brillante", special);
    ASSERT_TRUE(profileId.isOk());
    ASSERT_TRUE(profiles.assignToPiece(bright.value(), profileId.value()).isOk());

    pci::ui::AppRepositories repos;
    repos.settings = &settings;
    repos.pieces = &pieces;
    repos.detectionProfiles = &profiles;

    pci::ui::MainWindow window(repos);
    window.resize(1200, 800);

    QComboBox* combo = nullptr;
    for (auto* candidate : window.findChildren<QComboBox*>()) {
        if (candidate->findText(QStringLiteral("con perfil")) >= 0) {
            combo = candidate;
        }
    }
    ASSERT_NE(combo, nullptr) << "no se encontró el selector de piezas";

    const int withIndex = combo->findText(QStringLiteral("con perfil"));
    const int withoutIndex = combo->findText(QStringLiteral("sin perfil"));
    ASSERT_GE(withIndex, 0);
    ASSERT_GE(withoutIndex, 0);

    combo->setCurrentIndex(withIndex);

    QAction* configure = nullptr;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->text().startsWith(QStringLiteral("Configurar"))) {
            configure = action;
        }
    }
    ASSERT_NE(configure, nullptr);
    configure->trigger();
    auto* dialog = window.findChild<pci::ui::ConfigureDialog*>();
    ASSERT_NE(dialog, nullptr);
    auto* page = dialog->detectionPage();
    ASSERT_NE(page, nullptr);
    ASSERT_EQ(page->options().manualThreshold, 199)
        << "la página no enseña el perfil de la pieza seleccionada";
    ASSERT_EQ(page->selectedProfileId(), profileId.value());

    // Se cambia a la pieza SIN perfil, con la ventana abierta.
    combo->setCurrentIndex(withoutIndex);

    // LO QUE HAY QUE PROTEGER es que el PERFIL no se contagie. El umbral sí se
    // queda, y eso es el diseño documentado: un perfil es un override, y sin
    // perfil «todo sigue como antes» — se sigue trabajando con los ajustes
    // sueltos que haya en marcha.
    //
    // (La primera versión de esta prueba exigía que el umbral cambiara. Era una
    // expectativa mía, no una promesa del programa: la página estaba enseñando
    // la verdad. Vale la pena dejarlo escrito para no «arreglar» dos veces algo
    // que no está roto.)
    EXPECT_EQ(page->selectedProfileId(), 0)
        << "la página sigue con el perfil de la otra pieza seleccionado: aceptar "
           "se lo asignaría a esta";

    // Y la página enseña lo que de verdad está aplicado, no lo que había al
    // abrirla: si se quedara con una foto vieja, aceptar escribiría esa foto.
    EXPECT_EQ(page->options().manualThreshold, 199)
        << "la página no refleja los ajustes que están de verdad en marcha";
}
