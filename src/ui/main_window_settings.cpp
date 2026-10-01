#include "ui/main_window.h"
#include "ui/main_window_internal.h"

#include "camera/frame_utils.h"
#include "core/logging.h"
#include "repositories/config_io.h"
#include "repositories/detection_profile_repository.h"
#include "repositories/settings_repository.h"
#include "ui/background_patch_dialog.h"
#include "ui/configure_dialog.h"
#include "ui/detection_page.h"
#include "ui/dialog_geometry.h"
#include "ui/performance_page.h"
#include "ui/pieces_page.h"
#include "ui/preferences_page.h"
#include "ui/theme.h"
#include "vision/auto_roi.h"
#include "vision/contour_analysis.h"
#include "vision/edge_segmentation.h"
#include "vision/pipeline.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>

#include <algorithm>
#include <string>

namespace pci::ui {

// Exportar/importar la configuración de la máquina (O4): calibración, ajustes
// y perfiles de detección, atajos y preferencias. No incluye piezas ni
// plantillas a propósito (esas se comparten con el export de plantillas).
void MainWindow::onResetConfigClicked() {
    if (repos_.settings == nullptr) {
        statusBar()->showMessage(tr("No hay ajustes guardados que restablecer."));
        return;
    }

    // Se pregunta ANTES, y la pregunta dice las dos cosas que hacen falta para
    // contestarla: qué se lleva por delante y qué NO toca. Un «¿Está seguro?» a
    // secas no se puede contestar — el operador no sabe si va a perder sus
    // piezas registradas.
    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Warning);
    confirm.setWindowTitle(tr("Restablecer configuración de fábrica"));
    confirm.setText(tr("<b>Se olvidarán todos los ajustes de esta máquina.</b>"));
    confirm.setInformativeText(
        tr("Se restablecen: la calibración de escala, los ajustes y perfiles de "
           "detección, la zona de trabajo, las preferencias, los atajos de teclado, "
           "los controles de cámara guardados, las capas de la vista y el tamaño de "
           "las ventanas.\n\n"
           "No se toca: las piezas registradas, sus plantillas de herramientas ni el "
           "historial de inspecciones.\n\n"
           "Esto no se puede deshacer. Si quieres conservar la puesta a punto, "
           "cancela y usa antes «Exportar configuración…»."));
    auto* reset = confirm.addButton(tr("Restablecer"), QMessageBox::DestructiveRole);
    confirm.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    // El botón por defecto es Cancelar: en un diálogo destructivo, la tecla
    // Intro no puede ser la que borra.
    confirm.setDefaultButton(qobject_cast<QPushButton*>(confirm.buttons().last()));
    confirm.exec();
    if (confirm.clickedButton() != reset) {
        return;
    }

    const auto forgotten = repos_.settings->forget();
    if (!forgotten.isOk()) {
        QMessageBox::warning(this, tr("No se pudo restablecer"),
                             QString::fromStdString(forgotten.error().message));
        return;
    }

    // Los ajustes ya están olvidados; lo que queda en memoria es de la sesión
    // que se acaba de restablecer. Se devuelven a fábrica los que se ven en el
    // acto, para que la ventana no siga enseñando lo que ya no existe.
    pipelineConfig_ = vision::PipelineConfig{};
    zoneMode_ = vision::WorkingZoneMode::Automatic;
    autoRoi_.reset();
    calibration_ = domain::ScaleCalibration{};
    calibratedCameraKey_.clear();
    boardConfig_ = vision::BoardConfig{};
    boardVisible_ = false;
    rulerVisible_ = false;
    measureStages_ = false;
    stageStats_.clear();
    video_->setMmPerPixel(0.0);
    video_->setBoardConfig(boardConfig_);
    video_->setBoardVisible(boardVisible_);
    video_->setRulerVisible(rulerVisible_);
    if (repos_.engine != nullptr) {
        repos_.engine->setBoardConfig(boardConfig_);
    }
    updateCalibrationLabel();
    updateRoiButton();
    updateStatusIndicators();

    // Y se dice qué queda por aplicarse. Lo que se lee una sola vez al arrancar
    // —los atajos, la disposición de la ventana— no puede rehacerse sin volver
    // a abrir, y callárselo dejaría al operador creyendo que el restablecido no
    // funcionó.
    QMessageBox::information(
        this, tr("Configuración restablecida"),
        tr("Se han olvidado %n ajuste(s).\n\n"
           "La detección, la calibración, la zona y las capas de la vista ya están "
           "como de fábrica. Los atajos de teclado y la disposición de las ventanas "
           "se aplican al volver a abrir el programa.",
           nullptr, forgotten.value()));
    statusBar()->showMessage(tr("Configuración de fábrica restablecida."));
    reanalyseCurrentFrame();
}

void MainWindow::onExportConfigClicked() {
    if (repos_.settings == nullptr || repos_.detectionProfiles == nullptr) {
        QMessageBox::warning(this, tr("Sin base de datos"),
                             tr("No hay configuración que exportar sin base de datos."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar configuración"), QStringLiteral("pc_inspector_config.json"),
        tr("Configuración (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    auto result = repositories::exportConfig(path.toStdString(), *repos_.settings,
                                             *repos_.detectionProfiles);
    if (!result.isOk()) {
        QMessageBox::warning(this, tr("No se pudo exportar"),
                             QString::fromStdString(result.error().message));
        return;
    }
    statusBar()->showMessage(tr("Configuración exportada: %1 ajustes y %2 perfil(es).")
                                 .arg(result.value().settings)
                                 .arg(result.value().profiles));
}

void MainWindow::onImportConfigClicked() {
    if (repos_.settings == nullptr || repos_.detectionProfiles == nullptr) {
        QMessageBox::warning(this, tr("Sin base de datos"),
                             tr("No se puede importar configuración sin base de datos."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importar configuración"), QString(), tr("Configuración (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    // Sobrescribe ajustes: se avisa antes, y se recuerda que la calibración
    // depende de la cámara y la resolución de ESTA máquina.
    if (QMessageBox::question(
            this, tr("Importar configuración"),
            tr("Se sobrescribirán los ajustes actuales (calibración, detección, atajos "
               "y preferencias) y se añadirán los perfiles del archivo.\n\n"
               "La calibración de escala depende de la cámara y la resolución: si aquí "
               "usas otra, la app te avisará de que ya no es válida.\n\n"
               "¿Continuar?")) !=
        QMessageBox::Yes) {
        return;
    }
    auto result = repositories::importConfig(path.toStdString(), *repos_.settings,
                                             *repos_.detectionProfiles);
    if (!result.isOk()) {
        QMessageBox::warning(this, tr("No se pudo importar"),
                             QString::fromStdString(result.error().message));
        return;
    }
    QMessageBox::information(
        this, tr("Configuración importada"),
        tr("Se aplicaron %1 ajustes y %2 perfil(es).\n\nReinicia la aplicación para que "
           "todo (atajos incluidos) quede cargado.")
            .arg(result.value().settings)
            .arg(result.value().profiles));
    statusBar()->showMessage(tr("Configuración importada desde %1").arg(path));
}

void MainWindow::applyPreferencesPage(PreferencesPage* page) {
    if (page == nullptr) {
        return;
    }
    autoIntervalMs_ = page->autoIntervalMs();
    kSigma_ = page->kSigma();
    const bool wasOn = passTriggerOn_;
    passTriggerOn_ = page->passTrigger();
    vision::PassTriggerOptions passOptions;
    passOptions.settleMs = page->settleMs();
    passOptions.rearmMs = page->rearmMs();
    passTrigger_.setOptions(passOptions);
    // Al encenderlo se empieza de cero: los milisegundos que llevara contados
    // con otros tiempos no dicen nada de los nuevos.
    if (passTriggerOn_ != wasOn || passTriggerOn_) {
        passTrigger_.reset();
        passWantsMeasure_ = false;
        lastPassWhy_.clear();
    }

    // Aplicar de inmediato.
    autoTimer_.setInterval(autoIntervalMs_);
    if (repos_.engine != nullptr) {
        repos_.engine->setKSigma(kSigma_);
    }
    if (repos_.settings != nullptr) {
        repos_.settings->setInt("pref_auto_interval_ms", autoIntervalMs_);
        repos_.settings->setDouble("pref_ksigma", kSigma_);
        repos_.settings->setInt("pref_pass_trigger", passTriggerOn_ ? 1 : 0);
        repos_.settings->setInt("pref_pass_settle_ms", passOptions.settleMs);
        repos_.settings->setInt("pref_pass_rearm_ms", passOptions.rearmMs);
    }
    // El tema no se aplica aquí: se guarda y lo usa el próximo arranque.
    const theme::ThemeChoice chosenTheme = page->themeChoice();
    bool themeChanged = false;
    if (repos_.settings != nullptr) {
        const int before = repos_.settings->getInt("pref_theme", 0).valueOr(0);
        themeChanged = theme::themeChoiceFromSetting(before) != chosenTheme;
        repos_.settings->setInt("pref_theme", static_cast<int>(chosenTheme));
    }
    statusBar()->showMessage(themeChanged
                                 ? tr("Preferencias guardadas. El tema cambia al volver a "
                                      "abrir el programa.")
                                 : tr("Preferencias guardadas."));
}

void MainWindow::onConfigureClicked() {
    // Uno solo: volver a pulsar trae al frente el que ya está abierto en vez de
    // apilar paneles que se pisan entre sí.
    if (configureDialog_ != nullptr) {
        configureDialog_->raise();
        configureDialog_->activateWindow();
        return;
    }

    ConfigureDialog::Inputs inputs;
    inputs.segmentation = pipelineConfig_.segmentation;
    inputs.detectionProfileId = currentProfileId_;
    inputs.minAreaFraction = pipelineConfig_.minAreaFraction;
    inputs.subpixelEdges = pipelineConfig_.subpixelEdges;
    inputs.maxAreaFraction = pipelineConfig_.maxAreaFraction;
    inputs.profiles = repos_.detectionProfiles;
    inputs.settings = repos_.settings;
    // Las resoluciones ya sondeadas de ESTA cámara se pasan hechas: volver a
    // preguntarlas cuesta segundos y detiene el vídeo.
    inputs.controller = (streaming_ && !cameraControls_.empty()) ? &controller_ : nullptr;
    // Con una fuente de fichero no hay controles que sondear, así que la
    // pestaña de cámara cae sola en su sustituto; lo que hace falta es que ese
    // sustituto diga el motivo CORRECTO, y para eso tiene que saber qué fuente
    // hay puesta.
    inputs.sourceKind = sourceKind_;
    inputs.probedControls = cameraControls_;
    inputs.knownResolutions = knownResolutions_;
    inputs.currentResolution = currentResolution_;
    inputs.autoIntervalMs = autoIntervalMs_;
    inputs.kSigma = kSigma_;
    inputs.passTrigger = passTriggerOn_;
    inputs.settleMs = passTrigger_.options().settleMs;
    inputs.rearmMs = passTrigger_.options().rearmMs;
    inputs.themeChoice = theme::themeChoiceFromSetting(
        repos_.settings != nullptr ? repos_.settings->getInt("pref_theme", 0).valueOr(0) : 0);
    inputs.zoneMode = zoneMode_;
    inputs.expectedPieces = expectedPieces_;
    inputs.showMosaic = showMosaic_;
    // El tamaño del frame que se está viendo, para que «área mínima» se pueda
    // traducir a píxeles. Si todavía no ha llegado ninguno, va vacío y la página
    // no traduce en vez de inventarse una referencia.
    inputs.frameSize = lastFrame_.size();
    inputs.blobAreas = lastBlobAreas_;
    inputs.hasFixedZone = pipelineConfig_.roi.area() > 0;
    inputs.hasFreeZone = pipelineConfig_.roiPolygon.size() >= 3;

    auto* dialog = new ConfigureDialog(std::move(inputs), this);
    keepDialogSize(*dialog, repos_.settings, "configure", 560, 520);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    configureDialog_ = dialog;
    dialog->setCurrentTab(configureTab_);

    wireCameraPage(dialog->cameraPage());
    // El modo de zona se aplica AL MOMENTO y no al pulsar Aplicar: es un
    // conmutador, no un formulario, y su efecto se ve en el vídeo.
    if (auto* performance = dialog->performancePage(); performance != nullptr) {
        performance->setStageMeasurement(measureStages_);
        connect(performance, &PerformancePage::modeChanged, this,
                &MainWindow::setWorkingZoneMode);
        connect(performance, &PerformancePage::stageMeasurementToggled, this,
                [this](bool enabled) {
                    // La ventana anterior se tira al encender: lo que se mide
                    // ahora es lo que está pasando ahora, y arrastrar muestras
                    // de una sesión anterior daría un reparto de otra escena.
                    measureStages_ = enabled;
                    stageStats_.clear();
                    // Es una preferencia de diagnóstico, y hasta ahora había
                    // que reactivarla en cada sesión: justo cuando se está
                    // persiguiendo algo que tarda, que es cuando menos apetece
                    // volver a buscarla.
                    if (repos_.settings != nullptr) {
                        repos_.settings->setInt("measure_stages", enabled ? 1 : 0);
                    }
                });
    }
    connect(dialog, &ConfigureDialog::applied, this, [this, dialog] {
        applyDetectionPage(dialog->detectionPage());
        applyPreferencesPage(dialog->preferencesPage());
        applyPiecesPage(dialog->piecesPage());
    });
    if (auto* pieces = dialog->piecesPage(); pieces != nullptr) {
        // EL BOTON QUE NO HACIA NADA.
        //
        // Decia «pone en el campo el numero de piezas que la camara esta
        // detectando», y llamaba a `setDetectedCount`, que solo refresca el texto
        // de estado de mas abajo. El campo no se movia. Un boton que promete algo
        // y no lo hace es peor que no tenerlo: quien lo pulsa se queda creyendo
        // que el numero ya esta puesto.
        // El número cambia en la pantalla al momento; guardarlo espera a
        // Aceptar. Son dos cosas distintas y mezclarlas obligaría a escribir en
        // la base por cada número intermedio.
        connect(pieces, &PiecesPage::expectedPiecesChangedLive, this,
                &MainWindow::declareExpectedPieces);
        // El mosaico se enciende o se apaga en el momento. Es una opción de VER,
        // y una opción de ver que no se ve hasta cerrar la ventana obliga a
        // abrirla dos veces para saber si era la que querías.
        connect(pieces, &PiecesPage::showMosaicChangedLive, this,
                &MainWindow::showMosaicPanel);
        connect(pieces, &PiecesPage::useDetectedRequested, this, [this, pieces] {
            // EL MISMO NÚMERO QUE DICE EL TEXTO, no otro.
            //
            // Aquí se ponía `lastPieceCount_` —las piezas USADAS— mientras el
            // texto de al lado dice `lastPiecesSeen_` —las manchas VISTAS—. Con
            // tres manchas y dos declaradas, el panel decía «se ven 3» y el
            // botón ponía 2: dos números distintos para la misma pregunta, y el
            // «3» desaparecía al pulsar.
            //
            // Y contradecía el propósito que el propio código tenía escrito:
            // «tiene que poder SUBIR el número cuando de verdad hay más piezas
            // de las declaradas». Con las usadas nunca podía subirlo, porque
            // esas ya vienen recortadas a lo declarado.
            //
            // Si la tercera mancha es una sombra, lo que hay que arreglar es la
            // detección, no el número — y para eso está el aviso de al lado.
            pieces->setDetectedCount(lastPiecesSeen_);
            pieces->setExpectedPieces(lastPiecesSeen_);
        });
    }
    if (auto* detection = dialog->detectionPage(); detection != nullptr) {
        // LA COMPROBACIÓN DE CORTE LA HACE LA VENTANA, no la página.
        //
        // La página no tiene la imagen —ni debería tenerla: es un formulario— y
        // además esto cuesta dos análisis completos, 60 ms con cien piezas. Va
        // a petición del operador y no en cada fotograma.
        connect(detection, &DetectionPage::clippingCheckRequested, this, [this, detection] {
            const QImage frame = frameOrFile();
            if (frame.isNull()) {
                statusBar()->showMessage(
                    tr("No hay imagen que mirar: arranca la cámara o abre un fichero."));
                return;
            }
            detection->setClippingCheck(
                vision::checkThresholdClipping(camera::qImageToMat(frame)));
        });
        // SEÑALAR EL FONDO EN LA IMAGEN, por lo mismo: la imagen está aquí.
        //
        // Y si no hay ninguna se cae a la rueda de colores en vez de no hacer
        // nada. Un botón que a veces no responde y no dice por qué se lee como
        // que el programa está roto.
        connect(detection, &DetectionPage::backgroundPatchRequested, this, [this, detection] {
            const QImage frame = frameOrFile();
            if (frame.isNull()) {
                statusBar()->showMessage(
                    tr("Sin imagen no se puede señalar el fondo: se elige el color a mano."));
                detection->pickBackgroundByWheel();
                return;
            }
            BackgroundPatchDialog picker(camera::qImageToMat(frame), detection->options(), this);
            if (picker.exec() == QDialog::Accepted && picker.sample().valid) {
                detection->setChosenBackground(picker.sample().colour);
            }
        });
    }
    connect(dialog, &ConfigureDialog::scaleWizardRequested, this,
            &MainWindow::onCalibrateClicked);
    connect(dialog, &ConfigureDialog::shortcutsRequested, this,
            &MainWindow::onShowShortcuts);
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (configureDialog_ == dialog) {
            configureDialog_ = nullptr;
        }
    });
    // La pestaña abierta se recuerda: quien está peleando con la iluminación
    // vuelve diez veces a la misma.
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        configureTab_ = dialog->currentTab();
        if (repos_.settings != nullptr) {
            repos_.settings->setInt("config_last_tab", configureTab_);
        }
    });
    dialog->show();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmSaveBeforeLeaving()) {
        event->ignore();  // el operador canceló el cierre para no perder cambios
        return;
    }
    persistWindowLayout();
    persistLastSession();
    QMainWindow::closeEvent(event);
}

// Los tres eventos que pueden cambiar la geometría. No se guarda en el acto:
// arrastrar una ventana emite decenas de eventos por segundo y no hacen falta
// decenas de escrituras en la base de datos.
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    scheduleWindowLayoutSave();
}

void MainWindow::moveEvent(QMoveEvent* event) {
    QMainWindow::moveEvent(event);
    scheduleWindowLayoutSave();
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event != nullptr && event->type() == QEvent::WindowStateChange) {
        scheduleWindowLayoutSave();  // maximizar o restaurar
    }
}

void MainWindow::scheduleWindowLayoutSave() {
    if (layoutSaveTimer_.isActive()) {
        layoutSaveTimer_.stop();
    }
    layoutSaveTimer_.start();
}

void MainWindow::persistWindowLayout() {
    if (repos_.settings == nullptr) {
        return;
    }
    // `saveGeometry` lleva dentro tamaño, posición, pantalla y si estaba
    // maximizada; `saveState`, la disposición de paneles y barras. Son dos
    // cosas distintas y Qt las guarda por separado a propósito: restaurar una
    // sin la otra deja la ventana bien colocada con los paneles descuadrados,
    // o al revés.
    repos_.settings->setString("window_geometry", saveGeometry().toBase64().toStdString());
    repos_.settings->setString("window_state", saveState().toBase64().toStdString());
}

void MainWindow::restoreWindowLayout() {
    if (repos_.settings == nullptr) {
        return;
    }
    const auto restore = [this](const char* key, auto&& apply) {
        auto stored = repos_.settings->getString(key, std::string());
        if (!stored.isOk() || stored.value().empty()) {
            return;
        }
        const QByteArray base64(stored.value().data(),
                                static_cast<qsizetype>(stored.value().size()));
        apply(QByteArray::fromBase64(base64));
    };
    // La geometría PRIMERO y el estado después: `restoreState` coloca los
    // paneles dentro del tamaño que tenga la ventana, así que hacerlo al revés
    // los reparte sobre un tamaño que va a cambiar acto seguido.
    restore("window_geometry", [this](const QByteArray& data) { restoreGeometry(data); });
    restore("window_state", [this](const QByteArray& data) { restoreState(data); });
}

void MainWindow::persistLastSession() {
    if (repos_.settings == nullptr) {
        return;
    }
    // Con qué se estaba trabajando. Se recuerda la ELECCIÓN, no se reabre nada:
    // la cámara guardada tampoco arranca sola, y un programa que al abrirse se
    // pone a leer un vídeo por su cuenta hace algo que nadie le ha pedido.
    repos_.settings->setInt("last_piece_id", selectedPieceId());
    repos_.settings->setString("last_template", activeTemplate());
    repos_.settings->setString("last_source_kind",
                               std::string(camera::sourceKindKey(sourceKind_)));
    repos_.settings->setString("last_source_file", lastSourcePath_.toStdString());
}

void MainWindow::restoreCalibrationAndPreferences() {
    // Calibración de escala persistida.
    if (repos_.settings != nullptr) {
        calibration_.mmPerPixel =
            repos_.settings->getDouble("calib_mm_per_px", 0.0).valueOr(0.0);
        calibration_.cameraDistanceMm =
            repos_.settings->getDouble("calib_camera_dist_mm", 0.0).valueOr(0.0);
        calibration_.horizontalFovDeg =
            repos_.settings->getDouble("calib_fov_deg", 60.0).valueOr(60.0);
        calibration_.calibratedWidth = repos_.settings->getInt("calib_width", 0).valueOr(0);
        calibration_.calibratedHeight = repos_.settings->getInt("calib_height", 0).valueOr(0);
        calibratedCameraKey_ = QString::fromStdString(
            repos_.settings->getString("calib_camera", std::string()).valueOr(std::string()));
    }
    updateCalibrationLabel();
    video_->setMmPerPixel(calibration_.mmPerPixel);

    // Preferencias persistidas (O1): intervalo de auto-inspección y kSigma.
    if (repos_.settings != nullptr) {
        autoIntervalMs_ =
            std::clamp(repos_.settings->getInt("pref_auto_interval_ms", 1000).valueOr(1000),
                       200, 10000);
        kSigma_ = std::clamp(repos_.settings->getDouble("pref_ksigma", 3.0).valueOr(3.0), 0.5, 6.0);
        // Disparo por paso de pieza (P2). De fábrica APAGADO: encenderlo cambia
        // cuándo se mide, y quien ya tenía la auto-inspección funcionando no
        // puede encontrarse con que mide en otros momentos por actualizar.
        passTriggerOn_ = repos_.settings->getInt("pref_pass_trigger", 0).valueOr(0) != 0;
        vision::PassTriggerOptions passOptions;
        passOptions.settleMs =
            std::clamp(repos_.settings->getInt("pref_pass_settle_ms", 400).valueOr(400), 0, 10000);
        passOptions.rearmMs =
            std::clamp(repos_.settings->getInt("pref_pass_rearm_ms", 300).valueOr(300), 0, 10000);
        passTrigger_.setOptions(passOptions);
        // Pestaña del panel Configurar (C1). Sin acotar por arriba: el diálogo
        // ignora un índice que no exista, que es lo que pasará si una versión
        // futura tiene menos pestañas que la que guardó el número.
        configureTab_ = std::max(0, repos_.settings->getInt("config_last_tab", 0).valueOr(0));
        measureStages_ = repos_.settings->getInt("measure_stages", 0).valueOr(0) != 0;
        pipelineConfig_.minAreaFraction = std::clamp(
            repos_.settings->getDouble("det_min_area", 0.005).valueOr(0.005), 0.0001, 0.5);
        pipelineConfig_.maxAreaFraction = std::clamp(
            repos_.settings->getDouble("det_max_area", 0.9).valueOr(0.9), 0.1, 1.0);
        // Por defecto, IMAGEN ENTERA. Estuvo en «automática» y hubo que
        // revertirlo: el argumento para ponerla —«la automática no puede
        // cambiar ninguna respuesta»— era FALSO, y lo demostró usar la
        // aplicación.
        //
        // El recorte automático rodea a UNA pieza, la mayor, con su margen. Se
        // suelta cuando alguien «está contando», pero eso exige que el operador
        // haya declarado antes que espera varias — y no puede saber que tiene
        // que declararlo hasta que ya ha visto el problema. Con varias piezas
        // en la mesa y nada declarado, las demás quedaban fuera por
        // construcción y la aplicación decía que solo había una.
        //
        // Una optimización que cambia una respuesta no es una optimización, es
        // un fallo. Esa frase ya estaba escrita en `effectiveWorkingZone`; lo
        // que faltaba era aplicármela al elegir el valor por defecto.
        zoneMode_ = vision::workingZoneModeFromKey(
            repos_.settings->getString("work_zone_mode", "off").valueOr("off").c_str());
    }
    autoTimer_.setInterval(autoIntervalMs_);
    if (repos_.engine != nullptr) {
        repos_.engine->setKSigma(kSigma_);
    }
}

void MainWindow::restoreDetectionSettings() {
    // Ajustes de detección persistidos (umbral, polaridad, kernels y zona).
    if (repos_.settings != nullptr) {
        auto& seg = pipelineConfig_.segmentation;
        seg.manualThreshold = repos_.settings->getInt("det_threshold", -1).valueOr(-1);
        seg.polarity = static_cast<vision::SegmentationPolarity>(
            std::clamp(repos_.settings->getInt("det_polarity", 0).valueOr(0), 0, 2));
        seg.blurKernel = repos_.settings->getInt("det_blur", 5).valueOr(5);
        seg.morphKernel = repos_.settings->getInt("det_morph", 5).valueOr(5);
        // La separación de piezas que se tocan también se recuerda: es una
        // propiedad de CÓMO están colocadas las piezas en el puesto, no algo
        // que se decida cada vez.
        seg.splitTouchingPieces = repos_.settings->getInt("det_split_touching", 0).valueOr(0) != 0;
        // Se guarda el NÚMERO, no un sí/no: el día que el nivel de aflojado sea
        // ajustable, lo que ya está guardado sigue queriendo decir lo mismo.
        seg.recoverHighlightsBy = repos_.settings->getInt("det_recover_glare", 0).valueOr(0);
        // LA CLAVE DE COLOR DE FONDO ES UNA PROPIEDAD DEL PUESTO.
        //
        // El color de la mesa no cambia entre inspecciones, así que preguntarlo
        // cada vez sería preguntar por algo que ya se sabe. Se guarda el modo y
        // el color por separado: quien lo tenga en «lo busca solo» y un día pase
        // a «lo digo yo» se encuentra el último color que eligió, no un blanco.
        seg.backgroundKey = static_cast<vision::SegmentationOptions::BackgroundKey>(
            std::clamp(repos_.settings->getInt("det_background_key", 0).valueOr(0), 0, 2));
        seg.background = cv::Vec3b(
            static_cast<unsigned char>(
                std::clamp(repos_.settings->getInt("det_background_b", 255).valueOr(255), 0, 255)),
            static_cast<unsigned char>(
                std::clamp(repos_.settings->getInt("det_background_g", 255).valueOr(255), 0, 255)),
            static_cast<unsigned char>(
                std::clamp(repos_.settings->getInt("det_background_r", 255).valueOr(255), 0, 255)));
        pipelineConfig_.roi = cv::Rect(repos_.settings->getInt("det_roi_x", 0).valueOr(0),
                                       repos_.settings->getInt("det_roi_y", 0).valueOr(0),
                                       repos_.settings->getInt("det_roi_w", 0).valueOr(0),
                                       repos_.settings->getInt("det_roi_h", 0).valueOr(0));
        // Modo «fija» sin zona guardada es un estado imposible de alcanzar hoy,
        // pero sí de heredar de una versión anterior. Sin esto el programa diría
        // que trabaja en una zona y estaría mirando la imagen entera.
        //
        // Aquí NO vale `modeAfterFixedZoneChanged`: esa función es para cuando
        // el operador acaba de dibujar, y forzaría «fija» al abrir. Si guardó
        // una zona y luego se pasó a automática, el modo guardado es el que
        // manda; lo único que se corrige es la incoherencia a la baja.
        if (zoneMode_ == vision::WorkingZoneMode::Fixed &&
            pipelineConfig_.roi.area() <= 0) {
            zoneMode_ = vision::WorkingZoneMode::Off;
        }
        pipelineConfig_.roiPolygon = decodeZonePolygon(
            repos_.settings->getString(kSettingFreeZone, std::string()).valueOr(std::string()));
        // Y lo mismo para la libre, por el mismo motivo: el modo guardado puede
        // apuntar a un dibujo que ya no está.
        if (zoneMode_ == vision::WorkingZoneMode::Free &&
            pipelineConfig_.roiPolygon.size() < 3) {
            zoneMode_ = vision::WorkingZoneMode::Off;
        }
        pixelReferenceSize_ = QSize(repos_.settings->getInt("det_zone_ref_w", 0).valueOr(0),
                                   repos_.settings->getInt("det_zone_ref_h", 0).valueOr(0));
        pipelineConfig_.autoOrient = repos_.settings->getInt("track_rotation", 0).valueOr(0) != 0;
        pipelineConfig_.subpixelEdges =
            repos_.settings->getInt("det_subpixel", 0).valueOr(0) != 0;
        arucoLiveScale_ = repos_.settings->getInt("aruco_live", 0).valueOr(0) != 0;
        markerSizeMm_ = repos_.settings->getDouble("aruco_marker_mm", 30.0).valueOr(30.0);
    }
    updateRoiButton();
}

void MainWindow::restoreCameraAndViewSettings() {
    // Controles de la cámara guardados (O2): se reaplican al abrirla. Solo se
    // recuerdan los que el operador tocó alguna vez.
    if (repos_.settings != nullptr) {
        for (const camera::CameraProperty property : camera::allCameraProperties()) {
            const std::string key(camera::propertyKey(property));
            if (auto stored = repos_.settings->getDouble(key, -1e9);
                stored.isOk() && stored.value() > -1e9) {
                savedCameraControls_.push_back({property, stored.value()});
            }
        }
        setupGuided_ = repos_.settings->getInt("setup_guided", 0).valueOr(0) != 0;
        savedResolution_.width = repos_.settings->getInt("cam_width", 0).valueOr(0);
        savedResolution_.height = repos_.settings->getInt("cam_height", 0).valueOr(0);
    }

    // Tablero de referencia (T2): visibilidad y origen elegidos por el operador.
    if (repos_.settings != nullptr) {
        boardVisible_ = repos_.settings->getInt("board_visible", 0).valueOr(0) != 0;
        boardConfig_.origin = vision::originFromKey(
            repos_.settings->getString("board_origin", std::string("bounds")).valueOr(std::string("bounds")));
        boardConfig_.followPieceAngle = repos_.settings->getInt("board_follow", 0).valueOr(0) != 0;
        boardConfig_.fixedPoint = {
            static_cast<float>(repos_.settings->getDouble("board_fixed_x", 0.0).valueOr(0.0)),
            static_cast<float>(repos_.settings->getDouble("board_fixed_y", 0.0).valueOr(0.0))};
        boardConfig_.manualOffset = {
            static_cast<float>(repos_.settings->getDouble("board_offset_x", 0.0).valueOr(0.0)),
            static_cast<float>(repos_.settings->getDouble("board_offset_y", 0.0).valueOr(0.0))};
    }
    if (repos_.settings != nullptr) {
        rulerVisible_ = repos_.settings->getInt("ruler_visible", 0).valueOr(0) != 0;
        // El realce se recuerda: quien inspecciona piezas negras las inspecciona
        // todos los días, y volver a encenderlo cada mañana es un impuesto.
        // El modelo de la lente que quedara guardado. Se carga SIEMPRE; que se
        // aplique o no es otra cosa, y va en su propio ajuste.
        if (auto stored = repos_.settings->getString("lens_model", ""); stored.isOk()) {
            if (auto model = vision::parseCalibration(stored.value()); model.has_value()) {
                lensCorrector_ = vision::LensCorrector(*model);
            }
        }
        const bool lensOn = repos_.settings->getInt("lens_enabled", 0).valueOr(0) != 0;
        lensCorrectionOn_ = lensOn && lensCorrector_.isReady();
        if (lensCorrectionAction_ != nullptr) {
            lensCorrectionAction_->setEnabled(lensCorrector_.isReady());
            const QSignalBlocker block(lensCorrectionAction_);
            lensCorrectionAction_->setChecked(lensCorrectionOn_);
        }
        const bool enhance = repos_.settings->getInt("view_enhance", 0).valueOr(0) != 0;
        if (viewEnhanceAction_ != nullptr) {
            viewEnhanceAction_->setChecked(enhance);
        }
        video_->setViewEnhance(enhance);
    }
    video_->setRulerVisible(rulerVisible_);
    video_->setBoardVisible(boardVisible_);
    video_->setBoardConfig(boardConfig_);
    if (repos_.engine != nullptr) {
        repos_.engine->setBoardConfig(boardConfig_);
    }
}

void MainWindow::placeDocksMissingFromSavedLayout() {
    // Un dock NUEVO sobre un estado guardado VIEJO: `restoreState` no sabe nada
    // de él —se guardó antes de que existiera— y lo deja donde le parece, que a
    // veces es oculto. Quien ya usaba el programa abriría la versión nueva sin
    // paleta y sin forma de adivinar que le falta un panel.
    //
    // Se comprueba DESPUÉS de restaurar y se coloca a mano si hace falta. Es el
    // mismo rigor que con las migraciones de esquema: no basta con que funcione
    // en un perfil limpio.
    if (toolsDock_ != nullptr && toolsDock_->isHidden()) {
        addDockWidget(Qt::RightDockWidgetArea, toolsDock_);
        toolsDock_->show();
        core::logInfo("El dock de herramientas no estaba en la disposición guardada: "
                      "se coloca a la derecha");
    }
    // La tira de capturas es un dock NUEVO, así que cae exactamente en el caso
    // que describe el párrafo de arriba: ninguna disposición guardada hasta hoy
    // sabe de ella. Sin esto, quien ya usaba el programa actualizaría y no la
    // vería nunca — y no tendría forma de adivinar que le falta un panel.
    if (captureDock_ != nullptr && captureDock_->isHidden()) {
        addDockWidget(Qt::LeftDockWidgetArea, captureDock_);
        captureDock_->show();
        core::logInfo("La tira de capturas no estaba en la disposición guardada: "
                      "se coloca a la izquierda");
    }
    // Y la tabla de medidas, que es el dock más nuevo de todos y cayó en el
    // mismo agujero: quien ya usaba el programa la tendría oculta para siempre.
    if (measurementsDock_ != nullptr && captureDock_ != nullptr) {
        if (measurementsDock_->isHidden()) {
            addDockWidget(Qt::LeftDockWidgetArea, measurementsDock_);
            measurementsDock_->show();
            core::logInfo("La tabla de medidas no estaba en la disposición guardada: "
                          "se coloca a la izquierda");
        }
        // Comparte pestaña con las capturas para no partir la columna
        // izquierda en dos mitades estrechas. Sólo si NADIE lo ha emparejado
        // ya: una disposición que el operador colocó a mano manda sobre esto.
        if (tabifiedDockWidgets(captureDock_).isEmpty() &&
            dockWidgetArea(measurementsDock_) == Qt::LeftDockWidgetArea) {
            tabifyDockWidget(captureDock_, measurementsDock_);
            measurementsDock_->raise();
        }
    }
}

void MainWindow::restoreLastSession() {
    // Se vuelve a la pieza y a la plantilla con las que se estaba trabajando.
    // Sin esto, el combo caía siempre en la primera de la lista y en
    // «principal»: quien tiene veinte piezas registradas empezaba cada turno
    // buscando la suya.
    std::int64_t lastPiece = -1;
    QString lastTemplate;
    if (repos_.settings != nullptr) {
        lastPiece = repos_.settings->getInt("last_piece_id", -1).valueOr(-1);
        lastTemplate = QString::fromStdString(
            repos_.settings->getString("last_template", std::string()).valueOr(std::string()));
    }
    // Si la pieza se borró desde otra sesión, `loadPieceList` cae sola en la
    // primera: recordar una elección no puede impedir arrancar.
    loadPieceList(lastPiece);
    if (!lastTemplate.isEmpty()) {
        loadTemplateList(lastTemplate);
    }
    // Y la fuente elegida la última vez. Se PRESELECCIONA y nada más: la
    // cámara guardada tampoco arranca sola, y un programa que al abrirse se
    // pone a leer un fichero hace algo que nadie le ha pedido.
    if (repos_.settings != nullptr) {
        const auto kind = camera::sourceKindFromKey(
            repos_.settings->getString("last_source_kind", "camera").valueOr("camera").c_str());
        lastSourcePath_ = QString::fromStdString(
            repos_.settings->getString("last_source_file", std::string()).valueOr(std::string()));
        const int wanted = kind == camera::SourceKind::Image  ? kSourceOpenImage
                           : kind == camera::SourceKind::Video ? kSourceOpenVideo
                                                               : 0;
        if (wanted < 0) {
            if (const int index = cameraCombo_->findData(QVariant(wanted)); index >= 0) {
                cameraCombo_->setCurrentIndex(index);
            }
        }
    }
}

}  // namespace pci::ui
