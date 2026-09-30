#include "ui/main_window.h"
#include "ui/main_window_internal.h"

namespace pci::ui {

namespace {

const char* const kSettingCameraIndex = "camera_index";
const char* const kSettingLastSourceDir = "last_source_dir";

}  // namespace

// --- Cámara y análisis -----------------------------------------------------

void MainWindow::refreshCameras() {
    if (enumerationWatcher_.isRunning()) {
        return;
    }
    setControlsEnabled(false);
    cameraCombo_->clear();
    cameraCombo_->addItem(tr("Buscando cámaras…"));
    statusBar()->showMessage(tr("Buscando cámaras conectadas…"));

    enumerationWatcher_.setFuture(
        QtConcurrent::run([] { return camera::CameraEnumerator::enumerate(); }));
}

void MainWindow::onCamerasEnumerated() {
    cameras_ = enumerationWatcher_.result();

    // CON LAS SEÑALES BLOQUEADAS mientras se repuebla, y esto costó caro.
    //
    // `clear()` seguido de `addItem()` mueve el índice de -1 a 0, y Qt emite
    // `currentIndexChanged`. Desde que se puede cambiar de fuente en marcha,
    // esa señal se lee como «han elegido la cámara 0»: paraba el fichero que
    // estuviera abierto y arrancaba la cámara.
    //
    // El resultado, para quien lo sufre: abres una imagen o un vídeo nada más
    // arrancar y, cuando termina la enumeración de cámaras —que va en segundo
    // plano y tarda lo suyo—, tu fichero se cierra solo. Igual al pulsar
    // «Actualizar». Nadie eligió nada; lo eligió un índice al moverse.
    //
    // Es el mismo fallo que el del bucle del diálogo de fichero, en otro sitio:
    // repoblar una lista NO es una elección del operador.
    const QSignalBlocker repopulating(cameraCombo_);
    const QVariant previous = cameraCombo_->currentData();
    cameraCombo_->clear();

    for (std::size_t i = 0; i < cameras_.size(); ++i) {
        // La resolución solo se conoce tras conectar (la enumeración ya no abre
        // el dispositivo), así que se omite mientras sea 0.
        QString label = QString::fromStdString(cameras_[i].name);
        if (cameras_[i].width > 0 && cameras_[i].height > 0) {
            label += QStringLiteral(" (%1x%2)").arg(cameras_[i].width).arg(cameras_[i].height);
        }
        // El índice va en el DATO, no en la posición del combo. Este proyecto
        // ya pagó una vez el precio de señalar cosas por su posición: en cuanto
        // se añaden «Abrir imagen…» y «Abrir vídeo…» al final, cualquier código
        // que asumiera «índice del combo == índice en cameras_» apunta a otra
        // cosa sin avisar.
        cameraCombo_->addItem(label, QVariant(static_cast<int>(i)));
    }

    // Los ficheros son fuentes como la cámara, y van SIEMPRE, haya cámaras o
    // no. Antes, sin cámara, la aplicación entera se quedaba inservible: ni se
    // podía ajustar la detección, ni dibujar herramientas, ni probar una
    // plantilla. Con una imagen se puede hacer todo eso.
    if (!cameras_.empty()) {
        cameraCombo_->insertSeparator(cameraCombo_->count());
    }
    cameraCombo_->addItem(tr("Abrir imagen…"), QVariant(kSourceOpenImage));
    cameraCombo_->addItem(tr("Abrir vídeo…"), QVariant(kSourceOpenVideo));

    if (cameras_.empty()) {
        statusBar()->showMessage(
            tr("No se detectó ninguna cámara. Puedes conectar una y actualizar, o abrir una "
               "imagen o un vídeo para trabajar sin ella."));
        refreshAction_->setEnabled(true);
        startStopButton_->setEnabled(true);
        return;
    }

    // Si había un fichero abierto, su entrada se acaba de borrar con la lista:
    // se repone y se vuelve a seleccionar. Sin esto el desplegable diría
    // «cámara 0» mientras en pantalla se ve la imagen abierta.
    if (streaming_ && fileSource_ != nullptr) {
        cameraCombo_->insertItem(0, fileSource_->describe(), QVariant(kSourceOpenedFile));
        cameraCombo_->setCurrentIndex(0);
        statusBar()->showMessage(tr("%n cámara(s) detectada(s); sigue abierto «%1».", nullptr,
                                    static_cast<int>(cameras_.size()))
                                     .arg(fileSource_->describe()));
        setControlsEnabled(true);
        return;
    }
    // Y si la selección de antes sigue existiendo, se respeta: la enumeración
    // no es motivo para mover lo que el operador tenía elegido.
    if (previous.isValid()) {
        for (int i = 0; i < cameraCombo_->count(); ++i) {
            if (cameraCombo_->itemData(i) == previous) {
                cameraCombo_->setCurrentIndex(i);
                break;
            }
        }
    }

    // Restaurar la última cámara elegida por el usuario (si sigue conectada).
    if (repos_.settings != nullptr && !previous.isValid()) {
        const auto saved = repos_.settings->getInt(kSettingCameraIndex, -1);
        if (saved.isOk() && saved.value() >= 0) {
            for (std::size_t i = 0; i < cameras_.size(); ++i) {
                if (cameras_[i].index == saved.value()) {
                    cameraCombo_->setCurrentIndex(static_cast<int>(i));
                    break;
                }
            }
        }
    }

    statusBar()->showMessage(tr("%n cámara(s) detectada(s)", nullptr,
                                static_cast<int>(cameras_.size())));
    setControlsEnabled(true);
}

void MainWindow::onStartStopClicked() {
    if (streaming_) {
        // stop() une el hilo de captura; la UI se restablece en onStreamStopped.
        startStopButton_->setEnabled(false);
        if (fileSource_ != nullptr) {
            fileSource_->stop();
        } else {
            controller_.stop();
        }
        return;
    }

    // Qué fuente se eligió, preguntado por el DATO del elemento. Un separador no
    // tiene dato, y por eso no hace nada en vez de arrancar lo que hubiera
    // debajo.
    const QVariant choice = cameraCombo_->currentData();
    if (!choice.isValid()) {
        return;
    }
    if (choice.toInt() == kSourceOpenImage || choice.toInt() == kSourceOpenVideo) {
        startFileSource(choice.toInt() == kSourceOpenImage ? camera::SourceKind::Image
                                                           : camera::SourceKind::Video);
        return;
    }
    const int comboIndex = choice.toInt();
    if (comboIndex < 0 || comboIndex >= static_cast<int>(cameras_.size())) {
        return;
    }

    if (repos_.settings != nullptr) {
        if (auto saved =
                repos_.settings->setInt(kSettingCameraIndex, cameras_[comboIndex].index);
            !saved.isOk()) {
            core::logWarning("No se pudo guardar la cámara elegida: " + saved.error().message);
        }
    }

    streaming_ = true;
    // Identidad de la cámara en uso, para detectar si la calibración guardada
    // corresponde a otra cámara (D1).
    currentCameraKey_ = QString::fromStdString(cameras_[comboIndex].name);
    loadCachedResolutions();  // lista sondeada antes para ESTA cámara
    startStopButton_->setText(tr("Detener"));
    freezeButton_->setEnabled(true);
    // El desplegable NO se apaga: cambiar de fuente se decide mirando lo que
    // hay. Apagarlo dejaba «Abrir imagen…» inalcanzable con la cámara en
    // marcha, sin decir por qué — el operador veía la opción y no podía
    // llegar a ella.
    cameraCombo_->setEnabled(true);
    refreshAction_->setEnabled(false);
    statusBar()->showMessage(tr("Transmitiendo desde %1")
                                 .arg(QString::fromStdString(cameras_[comboIndex].name)));
    updateCalibrationLabel();  // reevalúa obsolescencia con la cámara nueva
    updateStatusIndicators();  // cámara ahora en verde (S4)
    // Los controles guardados se reaplican al abrir: la línea conserva su
    // exposición y su enfoque entre sesiones (O2).
    controller_.start(cameras_[comboIndex], savedCameraControls_);
    if (savedResolution_.valid()) {
        // La resolución elegida se reaplica al abrir, igual que los controles.
        controller_.requestResolution(savedResolution_);
    }
}

// El botón de arrancar dice lo que va a hacer: con «Abrir imagen…» o «Abrir
// vídeo…» elegido, lo siguiente es un diálogo de fichero, y «Iniciar» no lo
// anuncia. Un solo sitio para decidirlo, porque había dos y uno se olvidaba.
void MainWindow::updateStartButtonText() {
    if (streaming_) {
        return;  // en marcha dice «Detener», y eso lo pone quien arranca
    }
    const QVariant choice = cameraCombo_->currentData();
    const bool opensAFile = choice.isValid() && (choice.toInt() == kSourceOpenImage ||
                                                 choice.toInt() == kSourceOpenVideo);
    startStopButton_->setText(opensAFile ? tr("Abrir…") : tr("Iniciar"));
}

QString MainWindow::currentSourceLabel() const {
    switch (sourceKind_) {
        case camera::SourceKind::Camera: return tr("Frame actual de la cámara");
        case camera::SourceKind::Photo: return tr("La foto capturada");
        case camera::SourceKind::Image: return tr("La imagen abierta");
        case camera::SourceKind::Video: return tr("Frame actual del vídeo");
    }
    return tr("Frame actual");
}

void MainWindow::toggleFrozenPhoto() {
    if (sourceKind_ == camera::SourceKind::Photo) {
        // Soltar la foto: se para la fuente y se vuelve a escuchar la cámara,
        // que nunca dejó de transmitir. Volver cuesta cero.
        if (fileSource_ != nullptr) {
            fileSource_->stop();
            fileSource_.release()->deleteLater();
        }
        sourceKind_ = camera::SourceKind::Camera;
        cameraFrames_ = connect(&controller_, &camera::CameraController::frameReady, this,
                                &MainWindow::onFrame);
        freezeButton_->setText(tr("Capturar foto"));
        statusBar()->showMessage(tr("De vuelta al vídeo en vivo."));
        updateStatusIndicators();
        updateCalibrationLabel();
        return;
    }
    if (lastFrame_.isNull()) {
        statusBar()->showMessage(tr("Todavía no hay imagen que capturar."));
        return;
    }
    // CON UN VÍDEO O UNA IMAGEN TAMBIÉN SE CAPTURA.
    //
    // Antes esto contestaba «solo se puede capturar una foto del vídeo en vivo
    // de la cámara», y con un vídeo grabado no había forma de juntar en la tira
    // los frames buenos para medirlos después. Queja del dueño: «la toma de foto
    // solo funciona con la cámara en vivo».
    //
    // Con un fichero no hace falta cambiar de fuente —la cámara había que
    // soltarla porque seguía mandando frames—: basta con guardar en la tira lo
    // que se ve. Y el vídeo se pausa, para que la foto sea la que se está
    // mirando y no la de medio segundo después.
    if (sourceKind_ == camera::SourceKind::Video || sourceKind_ == camera::SourceKind::Image) {
        if (auto* video = dynamic_cast<camera::VideoFileSource*>(fileSource_.get());
            video != nullptr && !video->isPaused()) {
            video->setPaused(true);
            playPauseButton_->setText(tr("Seguir"));
            updateEdgeBrushAvailability();
        }
        captureTray_.add(lastFrame_, currentSourceLabel());
        refreshCaptureList();
        statusBar()->showMessage(tr("Foto guardada en Capturas. Elígela ahí para medirla."));
        return;
    }
    if (sourceKind_ != camera::SourceKind::Camera) {
        statusBar()->showMessage(tr("Todavía no hay imagen que capturar."));
        return;
    }

    // Se deja de escuchar a la cámara, pero NO se la para: resondear controles y
    // relanzar el perfil de exposición al volver costaría segundos y cambiaría
    // la imagen, que es justo lo que no se quiere de una foto.
    disconnect(cameraFrames_);
    const QString label =
        tr("Foto %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
    // La foto se queda EN LA TIRA además de congelarse. Antes cada captura
    // tiraba la anterior, así que no había forma de reunir varias para
    // comparar, guardar un historial ni alimentar el aprendizaje.
    captureTray_.add(lastFrame_, currentSourceLabel());
    refreshCaptureList();
    fileSource_ = std::make_unique<camera::StillImageSource>(lastFrame_, label,
                                                             camera::SourceKind::Photo);
    connect(fileSource_.get(), &camera::FrameSource::frameReady, this, &MainWindow::onFrame);
    connect(fileSource_.get(), &camera::FrameSource::statsUpdated, this, &MainWindow::onStats);
    connect(fileSource_.get(), &camera::FrameSource::sourceError, this,
            &MainWindow::onCameraError);
    sourceKind_ = camera::SourceKind::Photo;
    // `currentCameraKey_` NO se toca: la foto salió de esta misma cámara, con
    // esta óptica y a esta distancia, así que la calibración sigue valiendo. Si
    // se tocara, congelar dispararía el aviso de «calibración obsoleta» y en dos
    // días el operador dejaría de leer ese aviso también cuando importa.
    freezeButton_->setText(tr("Volver al vídeo"));
    statusBar()->showMessage(tr("Trabajando sobre %1. La cámara sigue conectada.").arg(label));
    updateStatusIndicators();
    fileSource_->start();
}

bool MainWindow::startFileSource(camera::SourceKind kind) {
    const bool wantsImage = kind == camera::SourceKind::Image;
    const QString path = askForSourceFile(wantsImage ? tr("Abrir imagen") : tr("Abrir vídeo"),
                                          wantsImage ? imageFileFilter() : videoFileFilter());
    if (path.isEmpty()) {
        // Cancelar no es un error: la ventana se queda exactamente como estaba.
        return false;
    }
    return startFileSourceAtPath(kind, path);
}

QString MainWindow::askForSourceFile(const QString& title, const QString& filter) {
    // Se vuelve a la última carpeta usada. Quien está revisando casos abre diez
    // ficheros de la misma carpeta, y volver a navegar cada vez es una fricción
    // tonta que se paga en cada abrir.
    QString startDir;
    if (repos_.settings != nullptr) {
        if (const auto saved = repos_.settings->getString(kSettingLastSourceDir, "");
            saved.isOk()) {
            startDir = QString::fromStdString(saved.value());
        }
    }
    const QString path = QFileDialog::getOpenFileName(this, title, startDir, filter);
    if (path.isEmpty()) {
        return path;
    }
    if (repos_.settings != nullptr) {
        if (auto saved = repos_.settings->setString(kSettingLastSourceDir,
                                                    QFileInfo(path).absolutePath().toStdString());
            !saved.isOk()) {
            core::logWarning("No se pudo guardar la carpeta de la fuente: " +
                             saved.error().message);
        }
    }
    return path;
}

void MainWindow::onOpenFileClicked() {
    const QString path = askForSourceFile(tr("Abrir imagen o vídeo"), imageOrVideoFileFilter());
    if (!path.isEmpty()) {
        openFile(path);
    }
}

// La puerta común del menú, de los recientes y de soltar un fichero. Decide
// imagen o vídeo por la extensión y entra por `startFileSourceAtPath`, igual
// que el desplegable: un segundo camino para montar la fuente sería otro sitio
// donde olvidarse de algo.
bool MainWindow::openFile(const QString& path) {
    const QString name = QFileInfo(path).fileName();
    const auto kind = sourceKindForFile(path);
    if (!kind.has_value()) {
        statusBar()->showMessage(
            tr("«%1» no se puede abrir: usa una imagen (%2) o un vídeo (%3).")
                .arg(name, describeExtensions(imageExtensions()),
                     describeExtensions(videoExtensions())));
        return false;
    }
    if (!QFileInfo(path).isFile()) {
        // Pasa con los recientes: el fichero se movió o se borró. Se quita de la
        // lista para que no vuelva a ofrecerse.
        recentFiles_ = withoutRecentFile(recentFiles_, path);
        storeRecentFiles();
        rebuildRecentMenu();
        statusBar()->showMessage(
            tr("«%1» ya no está en su carpeta. Lo he quitado de los recientes.").arg(name));
        return false;
    }
    if (sourceKind_ == camera::SourceKind::Photo) {
        // Con una foto congelada la cámara sigue abierta por debajo: primero se
        // vuelve al vídeo, y así lo que se para a continuación es la cámara.
        toggleFrozenPhoto();
    }
    if (streaming_) {
        // Parar es asíncrono con la cámara: el fichero se abre en
        // `onStreamStopped`, cuando la fuente anterior ya ha soltado.
        pendingOpenPath_ = path;
        pendingSourceChoice_.reset();
        statusBar()->showMessage(tr("Cambiando de fuente…"));
        onStartStopClicked();
        return true;
    }
    return startFileSourceAtPath(*kind, path);
}

void MainWindow::rememberRecentFile(const QString& path) {
    recentFiles_ = withRecentFile(recentFiles_, path);
    storeRecentFiles();
    rebuildRecentMenu();
}

void MainWindow::storeRecentFiles() {
    if (repos_.settings == nullptr) {
        return;  // sin base, la lista dura lo que la sesión
    }
    if (auto saved = repos_.settings->setString(
            kSettingRecentFiles, encodeRecentFiles(recentFiles_).toStdString());
        !saved.isOk()) {
        core::logWarning("No se pudo guardar la lista de recientes: " + saved.error().message);
    }
}

void MainWindow::rebuildRecentMenu() {
    if (recentMenu_ == nullptr) {
        return;
    }
    recentMenu_->clear();
    recentMenu_->setToolTipsVisible(true);
    int number = 0;
    for (const auto& path : recentFiles_) {
        ++number;
        // «&1 pieza.png»: la cifra es el acelerador, y un «&» del nombre se
        // dobla para que no se lo coma Qt.
        QString name = QFileInfo(path).fileName();
        name.replace(QLatin1Char('&'), QStringLiteral("&&"));
        auto* action = recentMenu_->addAction(QStringLiteral("&%1 %2").arg(number).arg(name));
        action->setToolTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path] { openFile(path); });
    }
    recentMenu_->addSeparator();
    auto* clear = recentMenu_->addAction(tr("Vaciar la lista"));
    clear->setObjectName(QStringLiteral("clearRecentFilesAction"));
    clear->setToolTip(tr("Borra la lista. Los ficheros se quedan donde están."));
    clear->setEnabled(!recentFiles_.isEmpty());
    connect(clear, &QAction::triggered, this, [this] {
        recentFiles_.clear();
        storeRecentFiles();
        rebuildRecentMenu();
    });
    recentMenu_->menuAction()->setEnabled(!recentFiles_.isEmpty());
}

// Se acepta cualquier fichero local al entrar, y se decide al soltar. Rechazar
// aquí un formato que no vale deja el cursor de prohibido sin decir por qué;
// al soltarlo, la barra de estado sí lo explica.
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const QMimeData* data = event->mimeData();
    if (data != nullptr && data->hasUrls()) {
        for (const auto& url : data->urls()) {
            if (url.isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* data = event->mimeData();
    if (data == nullptr || !data->hasUrls()) {
        QMainWindow::dropEvent(event);
        return;
    }
    // Uno solo: la ventana trabaja con una fuente cada vez. Si sueltan varios,
    // se abre el primero que se pueda abrir; si ninguno vale, el primero, para
    // que el mensaje diga cuál.
    QString chosen;
    for (const auto& url : data->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (chosen.isEmpty()) {
            chosen = path;
        }
        if (sourceKindForFile(path).has_value()) {
            chosen = path;
            break;
        }
    }
    if (chosen.isEmpty()) {
        QMainWindow::dropEvent(event);
        return;
    }
    event->acceptProposedAction();
    openFile(chosen);
}

// Abrir un fichero CONCRETO, sin diálogo. Separado de `startFileSource` porque
// elegir el fichero y montarlo son dos cosas distintas, y sólo la primera
// necesita a una persona delante: con el diálogo dentro, todo lo que pasa
// después —que llegue el frame, que se mida, que corregir el borde mueva el
// contorno— sólo se podía comprobar a mano, y así es como se colaron los
// últimos tres fallos.
bool MainWindow::startFileSourceAtPath(camera::SourceKind kind, const QString& path) {
    const bool wantsImage = kind == camera::SourceKind::Image;
    if (wantsImage) {
        fileSource_ = std::make_unique<camera::StillImageSource>(path);
    } else {
        fileSource_ = std::make_unique<camera::VideoFileSource>(path);
    }
    // Las mismas ranuras que la cámara. Ahí está todo el asunto: a partir de
    // aquí la ventana no sabe —ni necesita saber— de dónde vino el frame.
    connect(fileSource_.get(), &camera::FrameSource::frameReady, this, &MainWindow::onFrame);
    connect(fileSource_.get(), &camera::FrameSource::statsUpdated, this, &MainWindow::onStats);
    connect(fileSource_.get(), &camera::FrameSource::sourceError, this,
            &MainWindow::onCameraError);
    connect(fileSource_.get(), &camera::FrameSource::stopped, this,
            &MainWindow::onStreamStopped);

    if (auto* video = dynamic_cast<camera::VideoFileSource*>(fileSource_.get())) {
        connect(video, &camera::VideoFileSource::positionChanged, this,
                &MainWindow::onVideoPosition);
        playPauseButton_->setText(tr("Pausa"));
    }
    showVideoBar(kind == camera::SourceKind::Video);

    sourceKind_ = kind;
    lastSourcePath_ = path;
    streaming_ = true;
    // Todo lo que se abre acaba aquí —desplegable, menú, recientes, soltar—,
    // así que es el único sitio donde apuntarlo.
    rememberRecentFile(path);
    // El disparo por paso de pieza empieza de cero con cada fuente: los
    // milisegundos del vídeo anterior no dicen nada de éste, y si se cerró con
    // una pieza dentro el disparo quedó desarmado — la primera pieza del vídeo
    // nuevo no se mediría hasta que el encuadre se vaciara.
    passTrigger_.reset();
    passWantsMeasure_ = false;
    lastPassWhy_.clear();
    // La identidad de la fuente sirve para el aviso de calibración obsoleta: la
    // escala en px/mm depende de la óptica y de la distancia al plano, y pasar
    // de una cámara a un fichero (o entre ficheros) cambia las dos.
    currentCameraKey_ = fileSource_->describe();
    // Y el desplegable pasa a decir QUÉ está abierto. Dejarlo en «Abrir
    // imagen…» convertía la única pista sobre con qué se está trabajando en una
    // etiqueta que no dice nada.
    //
    // Con las señales BLOQUEADAS, y esto costó un bucle infinito. Insertar en la
    // posición 0 desplaza al elemento seleccionado —«Abrir imagen…»— de la
    // posición N a la N+1, y Qt emite `currentIndexChanged` porque el ÍNDICE ha
    // cambiado, aunque el elemento elegido sea exactamente el mismo.
    //
    // Desde que se puede cambiar de fuente en marcha, esa señal se lee como
    // «han elegido abrir una imagen»: paraba la fuente recién arrancada y
    // volvía a abrir el diálogo de fichero. El operador veía la carpeta
    // cerrarse y abrirse una y otra vez, sin llegar a cargar nada.
    //
    // Bloquear aquí es lo correcto y no un parche: esta selección no es una
    // elección del operador, es la consecuencia de la que acaba de hacer.
    {
        QSignalBlocker blocker(cameraCombo_);
        cameraCombo_->insertItem(0, fileSource_->describe(), QVariant(kSourceOpenedFile));
        cameraCombo_->setCurrentIndex(0);
    }
    startStopButton_->setText(tr("Cerrar"));
    // Capturar también vale con un fichero abierto: guarda el frame en la tira.
    freezeButton_->setText(tr("Capturar foto"));
    freezeButton_->setEnabled(true);
    // El desplegable NO se apaga: cambiar de fuente se decide mirando lo que
    // hay. Apagarlo dejaba «Abrir imagen…» inalcanzable con la cámara en
    // marcha, sin decir por qué — el operador veía la opción y no podía
    // llegar a ella.
    cameraCombo_->setEnabled(true);
    refreshAction_->setEnabled(false);
    statusBar()->showMessage(wantsImage ? tr("Analizando la imagen %1").arg(fileSource_->describe())
                                        : tr("Reproduciendo %1").arg(fileSource_->describe()));
    updateCalibrationLabel();
    updateStatusIndicators();
    fileSource_->start();
    return true;
}


// Barra de transporte del vídeo: reproducir/pausar, un paso, y dónde va.
//
// Un vídeo sin esto no sirve para lo que se abre un vídeo — encontrar EL frame
// en el que la pieza se ve bien y trabajar sobre él. Antes reproducía en bucle
// sin más, así que para volver a un frame había que esperar a que el bucle
// pasara otra vez por ahí.
void MainWindow::buildVideoBar(QWidget* parent, QVBoxLayout* root) {
    videoBar_ = new QWidget(parent);
    auto* row = new QHBoxLayout(videoBar_);
    row->setContentsMargins(0, 0, 0, 0);

    playPauseButton_ = new QToolButton(videoBar_);
    // Por nombre y no por su rótulo: es el único botón que CAMBIA de texto
    // —«Pausa» y «Seguir»— así que buscarlo por lo que dice obliga a probar
    // los dos, y una prueba que ya no encuentra ninguno falla lejos de aquí.
    playPauseButton_->setObjectName(QStringLiteral("playPauseButton"));
    playPauseButton_->setText(tr("Pausa"));
    playPauseButton_->setToolTip(tr("Pausa o sigue el vídeo."));
    playPauseButton_->setWhatsThis(
        tr("Con el vídeo parado se puede dibujar una herramienta sin que la pieza "
           "tiemble."));
    row->addWidget(playPauseButton_);

    stepButton_ = new QToolButton(videoBar_);
    stepButton_->setText(tr("▶|"));
    stepButton_->setToolTip(tr("Avanza un solo frame."));
    stepButton_->setWhatsThis(
        tr("Con la barra no se puede elegir el frame exacto: en un vídeo largo, un "
           "píxel de barra son varios frames."));
    row->addWidget(stepButton_);

    videoSlider_ = new QSlider(Qt::Horizontal, videoBar_);
    videoSlider_->setObjectName(QStringLiteral("videoSlider"));
    videoSlider_->setRange(0, 1000);
    videoSlider_->setToolTip(tr("Dónde va el vídeo. Arrastra para buscar."));
    row->addWidget(videoSlider_, 1);

    videoTimeLabel_ = new QLabel(videoBar_);
    videoTimeLabel_->setMinimumWidth(110);
    videoTimeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(videoTimeLabel_);

    root->addWidget(videoBar_);
    videoBar_->setVisible(false);

    connect(playPauseButton_, &QToolButton::clicked, this, [this] {
        auto* video = dynamic_cast<camera::VideoFileSource*>(fileSource_.get());
        if (video == nullptr) {
            return;
        }
        const bool pausing = !video->isPaused();
        video->setPaused(pausing);
        playPauseButton_->setText(pausing ? tr("Seguir") : tr("Pausa"));
        // Pausar habilita el pincel y seguir lo apaga: un vídeo detenido en un
        // frame es tan quieto como una foto, y en marcha no lo es.
        updateEdgeBrushAvailability();
    });
    connect(stepButton_, &QToolButton::clicked, this, [this] {
        if (auto* video = dynamic_cast<camera::VideoFileSource*>(fileSource_.get())) {
            video->stepOneFrame();
            playPauseButton_->setText(tr("Seguir"));  // el paso deja en pausa
            updateEdgeBrushAvailability();
        }
    });
    // Mientras se arrastra, la barra deja de seguir al vídeo: si no, el pulgar
    // daría saltos bajo el dedo cada vez que llega una posición nueva.
    connect(videoSlider_, &QSlider::sliderPressed, this,
            [this] { videoSliderHeld_ = true; });
    connect(videoSlider_, &QSlider::sliderReleased, this, [this] {
        videoSliderHeld_ = false;
        if (auto* video = dynamic_cast<camera::VideoFileSource*>(fileSource_.get())) {
            video->seekToFraction(videoSlider_->value() / 1000.0);
        }
    });
}

void MainWindow::showVideoBar(bool visible) {
    if (videoBar_ != nullptr) {
        videoBar_->setVisible(visible);
    }
}

void MainWindow::onVideoPosition(qint64 frame, qint64 total, double fps) {
    if (videoSlider_ == nullptr || videoTimeLabel_ == nullptr) {
        return;
    }
    // Sin total no se puede colocar el pulgar, y colocarlo donde sea sería
    // inventarse dónde va el vídeo. La barra se apaga y el rótulo lo dice.
    const bool placeable = total > 1;
    videoSlider_->setEnabled(placeable);
    if (placeable && !videoSliderHeld_) {
        QSignalBlocker blocker(videoSlider_);
        videoSlider_->setValue(static_cast<int>(1000.0 * frame / (total - 1)));
    }
    const auto asTime = [fps](qint64 f) {
        const double seconds = fps > 0.0 ? f / fps : 0.0;
        return QStringLiteral("%1:%2")
            .arg(static_cast<int>(seconds) / 60, 2, 10, QLatin1Char('0'))
            .arg(static_cast<int>(seconds) % 60, 2, 10, QLatin1Char('0'));
    };
    videoTimeLabel_->setText(placeable ? tr("%1 / %2").arg(asTime(frame), asTime(total - 1))
                                       : tr("frame %1").arg(frame));
}


// La tira de capturas, a la izquierda.
//
// «Capturar foto» congelaba el frame y ahí se quedaba: tomar la siguiente
// tiraba la anterior. Eso vale para medir UNA pieza y no vale para lo que se
// pide de un montón de fotos —historial, comparar unas con otras, alimentar el
// aprendizaje— porque las tres necesitan que coexistan.
//
// A la izquierda y no a la derecha: la derecha ya es de las herramientas, y se
// lee de izquierda a derecha — primero lo que has recogido, después sobre qué
// trabajas.
void MainWindow::buildCaptureDock() {
    captureDock_ = new QDockWidget(tr("Capturas"), this);
    captureDock_->setObjectName(QStringLiteral("captureDock"));
    auto* panel = new QWidget(captureDock_);
    auto* column = new QVBoxLayout(panel);

    captureCountLabel_ = new QLabel(panel);
    captureCountLabel_->setWordWrap(true);
    column->addWidget(captureCountLabel_);

    captureList_ = new QListWidget(panel);
    captureList_->setObjectName(QStringLiteral("captureList"));
    captureList_->setViewMode(QListView::IconMode);
    captureList_->setIconSize(QSize(112, 84));
    captureList_->setResizeMode(QListView::Adjust);
    captureList_->setMovement(QListView::Static);
    captureList_->setSpacing(4);
    captureList_->setToolTip(
        tr("Las fotos de esta sesión. Haz clic para trabajar sobre una; Supr la quita."));

    // SUPR, AQUÍ, QUITA LA FOTO — Y ANTES NO LO HACÍA.
    //
    // La ayuda de arriba lo prometía desde el principio y era falso:
    // `CaptureTray::removeAt` estaba escrita y no la llamaba NADIE. Mientras
    // tanto, Supr es un atajo de ventana atado a borrar la herramienta
    // seleccionada, y `QListWidget` no se queda con esa tecla, así que ganaba el
    // atajo: pulsar Supr con el foco en la tira borraba una cota de la plantilla
    // Y la quitaba de la base de datos.
    //
    // O sea que la propia ayuda enseñaba a pulsar la tecla que destruye trabajo
    // guardado, en silencio y mirando a otro panel.
    //
    // Se resuelve con un atajo de ámbito WIDGET: mientras el foco esté en la
    // tira, Supr es suyo; en cuanto el foco sale, vuelve a ser el de la ventana.
    auto* dropCapture = new QAction(tr("Quitar la foto de la tira"), captureList_);
    dropCapture->setShortcut(QKeySequence::Delete);
    dropCapture->setShortcutContext(Qt::WidgetShortcut);
    connect(dropCapture, &QAction::triggered, this, [this] {
        const int row = captureList_->currentRow();
        if (row < 0 || row >= captureTray_.count()) {
            return;
        }
        captureTray_.removeAt(row);
        refreshCaptureList();
        updateLearnFromCaptureAvailability();
        statusBar()->showMessage(
            tr("Foto quitada de la tira. Quedan %n.", nullptr, captureTray_.count()));
    });
    captureList_->addAction(dropCapture);
    column->addWidget(captureList_, 1);

    auto* buttons = new QHBoxLayout();
    auto* save = new QPushButton(tr("Guardar todas…"), panel);
    save->setToolTip(
        tr("Guarda todas las capturas en una carpeta, en PNG."));
    save->setWhatsThis(
        tr("El nombre lleva la pieza y la fecha por delante, para que la carpeta se "
           "ordene sola. Se usa PNG y no JPEG porque estas fotos son para volver a "
           "medir sobre ellas."));
    buttons->addWidget(save);
    auto* clear = new QPushButton(tr("Vaciar"), panel);
    clear->setToolTip(tr("Quita todas las capturas de la tira. No borra lo ya guardado."));
    buttons->addWidget(clear);
    column->addLayout(buttons);

    // Aprender de una foto: la última pieza que le faltaba a la tira.
    //
    // Hasta ahora las capturas eran fotos y nada más. La visión del proyecto
    // dice «actualizar la referencia estadística tras cada pieza buena, nunca
    // reentrenar», y eso sólo se podía hacer desde el diálogo de una inspección
    // recién corrida: las fotos que uno guarda durante la puesta a punto —que
    // son precisamente las buenas, elegidas a mano— no servían para nada.
    learnFromCaptureButton_ = new QPushButton(tr("Aprender de esta foto"), panel);
    learnFromCaptureButton_->setObjectName(QStringLiteral("learnFromCaptureButton"));
    learnFromCaptureButton_->setEnabled(false);
    column->addWidget(learnFromCaptureButton_);
    connect(learnFromCaptureButton_, &QPushButton::clicked, this,
            &MainWindow::onLearnFromCaptureClicked);

    captureDock_->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, captureDock_);

    connect(save, &QPushButton::clicked, this, &MainWindow::onSaveCapturesClicked);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (captureTray_.empty()) {
            return;
        }
        // Se pregunta: vaciar es lo único aquí que no se puede deshacer.
        const auto answer = QMessageBox::question(
            this, tr("Vaciar la tira"),
            tr("Se quitarán las %n captura(s) de la tira. Las que ya hayas guardado en "
               "disco no se tocan.", nullptr, captureTray_.count()));
        if (answer == QMessageBox::Yes) {
            captureTray_.clear();
            refreshCaptureList();
        }
    });
    connect(captureList_, &QListWidget::currentRowChanged, this,
            &MainWindow::onCaptureChosen);
    refreshCaptureList();
}

// Cuándo se puede aprender de la foto elegida, y si no se puede, POR QUÉ.
//
// Un botón apagado sin explicación se lee como que la aplicación está rota; y
// aquí hay tres motivos distintos para estarlo, que piden tres arreglos
// distintos por parte del operador.
void MainWindow::updateLearnFromCaptureAvailability() {
    if (learnFromCaptureButton_ == nullptr) {
        return;
    }
    const int row = captureList_ != nullptr ? captureList_->currentRow() : -1;
    const bool hasCapture = row >= 0 && row < captureTray_.count();
    const std::int64_t pieceId = selectedPieceId();
    const bool hasEngine = repos_.engine != nullptr && static_cast<bool>(repos_.embedFn);

    const bool usable = hasCapture && pieceId >= 0 && hasEngine;
    learnFromCaptureButton_->setEnabled(usable);
    learnFromCaptureButton_->setToolTip(
        usable ? tr("Añade esta foto a la referencia de la pieza como ejemplar bueno.")
        : !hasCapture ? tr("Elige antes una foto de la tira.")
        : pieceId < 0 ? tr("Elige antes qué pieza es: la referencia que se actualiza es la "
                           "suya.")
                      : tr("Sin el modelo ONNX no hay apariencia que aprender."));
    if (usable) {
        learnFromCaptureButton_->setWhatsThis(
            tr("La referencia no se reentrena: se le suma esta muestra y se guarda una "
               "versión nueva, conservando las anteriores. Antes de añadirla se "
               "inspecciona, y si sale NG se avisa."));
    }
}

// Aprender de una captura elegida a mano.
//
// EXPLÍCITO Y POR FOTO, nunca automático, y la decisión es deliberada: una
// referencia contaminada con piezas malas no falla ruidosamente, falla dejando
// pasar defectos. Es el peor modo de fallo de toda la aplicación, porque nadie
// lo nota hasta que llega una reclamación. Así que aprender es siempre un acto
// del operador sobre una foto concreta que él ha mirado.
//
// Y antes de sumarla se INSPECCIONA. Si el programa la considera mala, se dice
// —con el motivo— y se pregunta. El operador puede tener razón (la referencia
// era demasiado estrecha) o puede haberse equivocado de foto; lo que no puede
// es decidirlo sin la información.
void MainWindow::onLearnFromCaptureClicked() {
    const int row = captureList_ != nullptr ? captureList_->currentRow() : -1;
    if (row < 0 || row >= captureTray_.count() || repos_.engine == nullptr) {
        return;
    }
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0) {
        statusBar()->showMessage(tr("Elige antes qué pieza es."));
        return;
    }

    const cv::Mat frame = camera::qImageToMat(captureTray_.at(row).image);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto outcome = repos_.engine->inspect(frame, pieceId);
    QApplication::restoreOverrideCursor();
    if (!outcome.isOk()) {
        QMessageBox::warning(this, tr("Aprender de esta foto"),
                             tr("No se pudo inspeccionar la foto, así que tampoco añadirla "
                                "a la referencia.\n\n%1")
                                 .arg(QString::fromStdString(outcome.error().message)));
        return;
    }
    if (outcome.value().embedding.empty()) {
        QMessageBox::warning(this, tr("Aprender de esta foto"),
                             tr("De esta foto no salió ninguna huella de apariencia: sin "
                                "modelo cargado o sin pieza detectada en ella."));
        return;
    }

    const auto& verdict = outcome.value().verdict;
    if (!verdict.ok) {
        const auto answer = QMessageBox::question(
            this, tr("Esta foto sale NG"),
            tr("El programa considera mala esta pieza: %1\n\n"
               "Añadirla igualmente hará que defectos parecidos pasen como buenos a "
               "partir de ahora. ¿La añado?")
                .arg(QString::fromStdString(verdict.summary)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            statusBar()->showMessage(tr("No se añadió: la referencia sigue como estaba."));
            return;
        }
    }

    const auto version = repos_.engine->updateReference(pieceId, outcome.value().embedding);
    if (!version.isOk()) {
        QMessageBox::warning(this, tr("Aprender de esta foto"),
                             QString::fromStdString(version.error().message));
        return;
    }
    // Se dice QUÉ cambió, y con el parecido de la foto añadida: sin eso,
    // «referencia actualizada» es indistinguible de no haber hecho nada.
    const double similarity = verdict.embedding.similarity;
    statusBar()->showMessage(
        tr("Aprendido: la referencia de la pieza pasa a la versión %1 (las anteriores se "
           "conservan). Esta foto se parecía a la referencia un %2 %.")
            .arg(version.value())
            .arg(100.0 * similarity, 0, 'f', 1));
}

void MainWindow::refreshCaptureList() {
    if (captureList_ == nullptr) {
        return;
    }
    QSignalBlocker blocker(captureList_);
    captureList_->clear();
    for (int i = 0; i < captureTray_.count(); ++i) {
        const Capture& capture = captureTray_.at(i);
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(capture.image)),
                                         capture.taken.toString(QStringLiteral("HH:mm:ss")));
        // De dónde salió: sin esto, dos fotos de dos montajes distintos son
        // indistinguibles una semana después, que es cuando se miran.
        item->setToolTip(tr("%1 · %2")
                             .arg(capture.taken.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")),
                                  capture.source));
        captureList_->addItem(item);
    }
    captureCountLabel_->setText(
        captureTray_.empty()
            ? tr("Sin capturas. Pulsa «Capturar foto» y se irán juntando aquí.")
            : tr("%n captura(s) en esta sesión.", nullptr, captureTray_.count()));
    // La disponibilidad cambia con la lista: sin recalcularla aquí el botón
    // se quedaría como estuviera, que es el fallo que ya costó el pincel.
    updateLearnFromCaptureAvailability();
}

void MainWindow::onCaptureChosen(int row) {
    if (row < 0 || row >= captureTray_.count()) {
        return;
    }
    // Se trabaja sobre ella como sobre cualquier foto: la fuente pasa a ser esa
    // imagen. Así todo lo que ya funciona —medir, dibujar, inspeccionar— vale
    // igual sin un camino nuevo que mantener.
    const Capture& capture = captureTray_.at(row);

    // ANTES DE PONER LA CAPTURA, SE RETIRA LO QUE HABÍA.
    //
    // Aquí se creaba la fuente nueva encima de la que estuviera, sin pararla, y
    // eso rompía las dos situaciones en que se usa la tira:
    //
    //   - con la cámara en vivo, sus frames seguían llegando a `onFrame` y
    //     pisaban la captura al instante: se medía la cámara, no la foto;
    //   - con un vídeo o una imagen abiertos, reemplazar el puntero destruía la
    //     fuente en marcha, y su aviso de «detenida» llegaba a `onStreamStopped`,
    //     que desmontaba la fuente NUEVA.
    //
    // Queja del dueño: «la toma de mediciones no funciona con las diferentes
    // capturas, más que en cámara en vivo».
    const bool fromCamera = sourceKind_ == camera::SourceKind::Camera;
    const bool photoFromCamera = sourceKind_ == camera::SourceKind::Photo &&
                                 freezeButton_->text() == tr("Volver al vídeo");
    if (fromCamera) {
        disconnect(cameraFrames_);
    }
    if (fileSource_ != nullptr) {
        // Desconectada ANTES de pararla: su `stopped` ya no le toca a nadie.
        disconnect(fileSource_.get(), nullptr, this, nullptr);
        fileSource_->stop();
        fileSource_.release()->deleteLater();
    }
    fileSource_ = std::make_unique<camera::StillImageSource>(
        capture.image, capture.source, camera::SourceKind::Photo);
    connect(fileSource_.get(), &camera::FrameSource::frameReady, this, &MainWindow::onFrame);
    const bool backToCamera = fromCamera || photoFromCamera;
    if (backToCamera) {
        // Como al congelar: la cámara sigue conectada y se vuelve con el botón.
        // Sin `stopped`: parar la foto para volver no puede cerrar la cámara.
        freezeButton_->setText(tr("Volver al vídeo"));
        freezeButton_->setEnabled(true);
    } else {
        // Venía de un fichero: la captura pasa a ser la fuente, y «Cerrar» la
        // cierra como cerraría la imagen.
        connect(fileSource_.get(), &camera::FrameSource::stopped, this,
                &MainWindow::onStreamStopped);
        for (int i = 0; i < cameraCombo_->count(); ++i) {
            if (cameraCombo_->itemData(i).isValid() &&
                cameraCombo_->itemData(i).toInt() == kSourceOpenedFile) {
                cameraCombo_->setItemText(
                    i, tr("Captura %1").arg(capture.taken.toString(QStringLiteral("HH:mm:ss"))));
            }
        }
        freezeButton_->setText(tr("Capturar foto"));
        freezeButton_->setEnabled(false);
    }
    sourceKind_ = camera::SourceKind::Photo;
    streaming_ = true;
    showVideoBar(false);
    fileSource_->start();
    statusBar()->showMessage(tr("Trabajando sobre la captura de las %1.")
                                 .arg(capture.taken.toString(QStringLiteral("HH:mm:ss"))));
    updateLearnFromCaptureAvailability();
}

void MainWindow::onSaveCapturesClicked() {
    if (captureTray_.empty()) {
        statusBar()->showMessage(tr("No hay capturas que guardar."));
        return;
    }
    QString startDir;
    if (repos_.settings != nullptr) {
        startDir = QString::fromStdString(
            repos_.settings->getString("last_capture_dir", std::string()).valueOr(std::string()));
    }
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Guardar las capturas en…"), startDir);
    if (folder.isEmpty()) {
        return;  // cancelar no es un error
    }
    if (repos_.settings != nullptr) {
        repos_.settings->setString("last_capture_dir", folder.toStdString());
    }

    const QString piece = pieceCombo_ != nullptr && !pieceCombo_->currentText().isEmpty()
                              ? pieceCombo_->currentText()
                              : tr("pieza");
    const auto saved = captureTray_.saveAll(folder, piece);
    if (!saved.isOk()) {
        QMessageBox::warning(this, tr("No se pudieron guardar"),
                             QString::fromStdString(saved.error().message));
        return;
    }
    statusBar()->showMessage(tr("%n captura(s) guardadas en %1.", nullptr, saved.value())
                                 .arg(folder));
}

void MainWindow::onStats(double fps, int width, int height) {
    currentResolution_ = {width, height};
    lastCaptureFps_ = fps;
    updateRateReadout();
}

void MainWindow::onCameraError(const QString& message) {
    core::logError("Error de cámara: " + message.toStdString());
    // A LA BANDA Y NO A LA BARRA DE ESTADO. En la barra lo borraba el siguiente
    // mensaje a los pocos segundos, y el operador encontraba la imagen quieta
    // sin saber por qué. Se queda hasta que vuelva a llegar un fotograma.
    //
    // Esta ranura la usan la cámara y los ficheros, y lo que hay que hacer es
    // distinto: una cámara se reintenta; un fichero que no se lee se cambia
    // por otro.
    const bool fromFile = fileSource_ != nullptr && sender() == fileSource_.get();
    if (fromFile) {
        blockingNotice_->report(Blocker::Source, tr("%1. Abre otro archivo.").arg(message),
                                tr("Abrir…"));
    } else {
        blockingNotice_->report(Blocker::Source,
                                tr("%1. Revisa el cable y pulsa Reintentar.").arg(message),
                                tr("Reintentar"));
    }
}

void MainWindow::onStreamStopped() {
    streaming_ = false;
    if (fileSource_ != nullptr) {
        // `deleteLater` y no `reset()`: esta ranura puede estar corriendo
        // DENTRO de la emisión de `stopped()` de la propia fuente, y destruirla
        // ahí sería tirar el suelo mientras se está de pie encima.
        fileSource_.release()->deleteLater();
    }
    // QUÉ ERA LO QUE SE ESTABA USANDO, antes de olvidarlo.
    //
    // Hace falta unas líneas más abajo: al quitar la entrada del fichero, la
    // selección del desplegable cae en lo que quede en su sitio —la cámara
    // integrada— y eso no lo ha elegido nadie. Queja del taller: «usar imagen,
    // luego cerrarla, y que se ponga cámara integrada arruina la experiencia».
    const camera::SourceKind closedKind = sourceKind_;
    sourceKind_ = camera::SourceKind::Camera;
    freezeButton_->setText(tr("Capturar foto"));
    freezeButton_->setEnabled(false);
    // Se quita la entrada del fichero que estaba abierto. Por su DATO y no por
    // su índice: entre abrir y cerrar puede haberse reenumerado la lista.
    bool hadFileOpen = false;
    for (int i = cameraCombo_->count() - 1; i >= 0; --i) {
        if (cameraCombo_->itemData(i).isValid() &&
            cameraCombo_->itemData(i).toInt() == kSourceOpenedFile) {
            cameraCombo_->removeItem(i);
            hadFileOpen = true;
        }
    }
    // Y LA SELECCIÓN SE QUEDA EN EL MISMO TIPO QUE SE ACABA DE CERRAR.
    //
    // Quitar un elemento de un QComboBox deja la selección en el que ocupe ese
    // sitio, que aquí es la primera cámara. Nadie lo eligió: es la consecuencia
    // de borrar la entrada, y desde fuera se vive como que el programa cambia
    // de fuente solo.
    //
    // Lo que sigue a cerrar una imagen es abrir otra, casi siempre la de al
    // lado en la misma carpeta. Así que el desplegable se queda en «Abrir
    // imagen…» y basta con darle a Iniciar.
    //
    // Con las señales BLOQUEADAS: elegir en este desplegable abre el diálogo de
    // fichero, y esto no es una elección del operador. Igual que al restaurar
    // la fuente al arrancar, se PRESELECCIONA y nada más — un programa que al
    // cerrar un fichero se pone a abrir otro hace algo que nadie ha pedido.
    if (hadFileOpen) {
        const int wanted = closedKind == camera::SourceKind::Video ? kSourceOpenVideo
                                                                  : kSourceOpenImage;
        if (const int index = cameraCombo_->findData(QVariant(wanted)); index >= 0) {
            QSignalBlocker blocker(cameraCombo_);
            cameraCombo_->setCurrentIndex(index);
        }
    }
    autoInspectButton_->setChecked(false);
    stopLiveCapture();
    // Sin imagen no hay veredicto que enseñar: el de antes sería el de una
    // pieza que ya no está. Y el marcador deja de contar hasta que vuelva a
    // haber fotogramas que mirar.
    verdictBoard_->showVerdict(VerdictState::Hidden);
    markerStreak_.reset();
    blockingNotice_->resolve(Blocker::Marker);
    // EL ROTULO SIGUE AL DESPLEGABLE, TAMBIÉN AQUÍ.
    //
    // Aquí ponía «Iniciar» a secas. Pero justo arriba el desplegable se deja en
    // «Abrir imagen…» —con las señales bloqueadas, así que su propio manejador
    // no llega a cambiar el rótulo—, y el botón decía «Iniciar» sobre una
    // acción que abre un diálogo de fichero. Queja del taller: cerrar la
    // imagen, darle a «Iniciar» esperando volver a lo de antes, y encontrarse
    // con que no hay imagen que medir.
    updateStartButtonText();
    // Siempre habilitado: aunque no haya ninguna cámara, se puede abrir una
    // imagen o un vídeo.
    startStopButton_->setEnabled(true);
    cameraCombo_->setEnabled(true);
    refreshAction_->setEnabled(true);
    showVideoBar(false);
    statsLabel_->clear();
    pendingAnalysisFrame_ = QImage();
    lastFrame_ = QImage();
    video_->clearLive();
    liveFixture_.reset();
    cameraControls_.clear();
    // El panel Configurar es no modal: si sigue abierto tras detener la cámara,
    // los deslizadores de su página de cámara no harían nada. Se cierra en vez
    // de mentir; al volver a abrirlo se reconstruye con lo que haya.
    if (configureDialog_ != nullptr) {
        configureDialog_->close();
    }

    // La fuente que se pidió mientras la anterior seguía en marcha.
    //
    // Se arranca DIFERIDA y no aquí mismo, y la diferencia importa: esta ranura
    // puede estar corriendo dentro de la emisión de `stopped()` de la fuente que
    // acaba de morir, y abrir un diálogo de fichero MODAL ahí dentro es parar el
    // desmontaje a la mitad y quedarse esperando. `singleShot(0)` lo saca al
    // bucle de eventos, con el apagado ya terminado.
    if (pendingSourceChoice_.has_value()) {
        const int wanted = *pendingSourceChoice_;
        pendingSourceChoice_.reset();
        if (const int index = cameraCombo_->findData(QVariant(wanted)); index >= 0) {
            QSignalBlocker blocker(cameraCombo_);
            cameraCombo_->setCurrentIndex(index);
        }
        QTimer::singleShot(0, this, &MainWindow::onStartStopClicked);
    }
    // Y el fichero pedido por el menú, los recientes o al soltarlo. Diferido
    // por lo mismo que lo de arriba.
    if (!pendingOpenPath_.isEmpty()) {
        const QString path = pendingOpenPath_;
        pendingOpenPath_.clear();
        QTimer::singleShot(0, this, [this, path] { openFile(path); });
    }
    updateBoardReadout();      // "sin pieza detectada" al cortar la transmisión
    updateStatusIndicators();  // cámara vuelve a rojo (S4)
}

// La cámara acaba de decir qué controles soporta (O2): se habilita el menú y
// se recuerda el estado para poblar el diálogo.
// Al cambiar la resolución, todo lo que el operador definió en PÍXELES DE
// IMAGEN dejaría de señalar el mismo sitio: la zona de detección y el cero
// fijado del tablero. Se reescalan proporcionalmente en vez de dejarlos
// desplazados en silencio. Las herramientas no hacen falta: viven en
// coordenadas de pieza.
void MainWindow::rescalePixelSettings(const QSize& from, const QSize& to) {
    const cv::Size before(from.width(), from.height());
    const cv::Size after(to.width(), to.height());
    QStringList adjusted;
    // A partir de aquí la zona vive en las coordenadas NUEVAS: si no se anotara,
    // el próximo arranque volvería a reajustarla desde la resolución vieja y la
    // movería una segunda vez.
    pixelReferenceSize_ = to;

    if (pipelineConfig_.roi.area() > 0) {
        pipelineConfig_.roi = vision::rescaleRect(pipelineConfig_.roi, before, after);
        persistPipelineConfig();
        updateRoiButton();
        adjusted << tr("la zona de detección");
    }
    if (pipelineConfig_.roiPolygon.size() >= 3) {
        // Vértice a vértice, por lo mismo que el rectángulo: una zona dibujada
        // sobre 640×480 señala otro sitio en 1920×1080, y una zona que se
        // desplaza sola es peor que ninguna.
        for (auto& vertex : pipelineConfig_.roiPolygon) {
            const cv::Point2f moved = vision::rescalePoint(
                cv::Point2f(static_cast<float>(vertex.x), static_cast<float>(vertex.y)),
                before, after);
            vertex = cv::Point(cvRound(moved.x), cvRound(moved.y));
        }
        persistPipelineConfig();
        updateRoiButton();
        adjusted << tr("la zona libre");
    }
    if (boardConfig_.origin == vision::BoardOrigin::FixedPoint) {
        boardConfig_.fixedPoint = vision::rescalePoint(boardConfig_.fixedPoint, before, after);
        video_->setBoardConfig(boardConfig_);
        persistBoardConfig();
        adjusted << tr("el cero del tablero");
    }

    const QString sizes = tr("%1×%2 → %3×%4")
                              .arg(from.width())
                              .arg(from.height())
                              .arg(to.width())
                              .arg(to.height());
    statusBar()->showMessage(
        adjusted.isEmpty()
            ? tr("Resolución %1.").arg(sizes)
            : tr("Resolución %1: se reajustó %2.").arg(sizes, adjusted.join(tr(" y "))));
}

// La lista de resoluciones se recuerda POR CÁMARA en Settings: sondearla cuesta
// unos 15 s con una webcam real y detiene el vídeo, así que se paga una vez.
void MainWindow::onResolutionsProbed(
    const std::vector<camera::CameraResolution>& available,
    const camera::CameraResolution& current) {
    knownResolutions_ = available;
    currentResolution_ = current;
    if (repos_.settings == nullptr || currentCameraKey_.isEmpty()) {
        return;
    }
    QStringList encoded;
    for (const auto& resolution : available) {
        encoded << QStringLiteral("%1x%2").arg(resolution.width).arg(resolution.height);
    }
    repos_.settings->setString(resolutionCacheKey(), encoded.join(QLatin1Char(';')).toStdString());
}

std::string MainWindow::resolutionCacheKey() const {
    return "cam_res_" + currentCameraKey_.toStdString();
}

void MainWindow::loadCachedResolutions() {
    knownResolutions_.clear();
    if (repos_.settings == nullptr || currentCameraKey_.isEmpty()) {
        return;
    }
    const auto stored = repos_.settings->getString(resolutionCacheKey(), std::string());
    if (!stored.isOk() || stored.value().empty()) {
        return;
    }
    for (const QString& item :
         QString::fromStdString(stored.value()).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QStringList parts = item.split(QLatin1Char('x'));
        if (parts.size() != 2) {
            continue;
        }
        camera::CameraResolution resolution{parts[0].toInt(), parts[1].toInt()};
        if (resolution.valid()) {
            knownResolutions_.push_back(resolution);
        }
    }
}

void MainWindow::onControlsProbed(const std::vector<camera::CameraControlState>& controls) {
    cameraControls_ = controls;
    bool anySupported = false;
    for (const auto& control : controls) {
        anySupported = anySupported || control.supported;
    }
    if (!anySupported) {
        core::logInfo("La cámara no expone ningún control ajustable");
    }

    // De dónde parte cada automático. `probeControls` da 0 cuando la cámara no
    // informa, que es lo más honesto que se puede suponer, pero el estado real
    // lo van fijando el perfil y el barrido justo debajo.
    for (const auto& state : controls) {
        if (state.property == camera::CameraProperty::AutoExposure) {
            autoExposureOn_ = state.value > 0.5;
        }
        if (state.property == camera::CameraProperty::AutoFocus) {
            autoFocusOn_ = state.value > 0.5;
        }
    }

    // Perfil de medición (C1). Va aquí y no antes de abrir porque depende del
    // SONDEO: qué controles acepta esta cámara y en qué valor los tiene. Antes
    // de sondear no se sabe ni una cosa ni la otra.
    //
    // No se persiste a propósito. Guardarlo lo convertiría en «lo que el
    // operador eligió» y a partir de ahí el perfil no volvería a aplicarse ni
    // se distinguiría de un ajuste suyo. Así, `cam_*` en Settings sigue
    // significando exactamente lo que significaba: lo que el operador tocó.
    const auto defaults = camera::measurementDefaults(controls, savedCameraControls_);
    if (!defaults.empty()) {
        for (const auto& value : defaults) {
            core::logInfo(std::string("Perfil de medición: ") +
                          std::string(camera::propertyKey(value.property)) + " = " +
                          std::to_string(value.value));
        }
        controller_.requestControls(defaults);
        for (const auto& value : defaults) {
            if (value.property == camera::CameraProperty::AutoExposure) {
                autoExposureOn_ = value.value > 0.5;
            }
            if (value.property == camera::CameraProperty::AutoFocus) {
                autoFocusOn_ = value.value > 0.5;
            }
        }
        updateCalibrationLabel();
    }

    // Y la exposición se ELIGE midiendo, que es la parte que no se puede hacer
    // desde aquí: hay que leer frames entre cambio y cambio. Solo en una cámara
    // que el operador no haya configurado — si la tocó, manda él.
    const bool operatorSetExposure =
        std::any_of(savedCameraControls_.begin(), savedCameraControls_.end(),
                    [](const camera::CameraControlValue& value) {
                        return value.property == camera::CameraProperty::Exposure;
                    });
    for (const auto& state : controls) {
        if (state.property == camera::CameraProperty::Exposure && state.supported &&
            !operatorSetExposure) {
            controller_.requestExposureSweep(state.min, state.max);
        }
    }

    // «Configurar» no se deshabilita nunca: aunque la cámara no exponga nada,
    // ahí siguen estando la detección, la escala y las preferencias. La página
    // de cámara es la que dice que no hay nada que ajustar.
}

// La página de cámara aplica sola (mover y mirar); aquí solo se persiste lo
// que el operador deja puesto, para reaplicarlo en el próximo arranque.
void MainWindow::wireCameraPage(CameraImagePage* page) {
    if (page == nullptr) {
        return;
    }
    connect(page, &CameraImagePage::measurementProfileRequested, this, [this] {
        // Olvidar lo guardado es la mitad que importa: el perfil se salta a
        // propósito toda propiedad que el operador haya tocado, así que sin
        // borrarlas volvería a saltárselas y el botón no haría nada.
        for (const auto property : camera::allCameraProperties()) {
            if (repos_.settings != nullptr) {
                repos_.settings->remove(std::string(camera::propertyKey(property)));
            }
        }
        savedCameraControls_.clear();
        core::logInfo("Ajustes de cámara olvidados a petición del operador");

        const auto defaults = camera::measurementDefaults(cameraControls_, {});
        if (!defaults.empty()) {
            controller_.requestControls(defaults);
        }
        for (const auto& state : cameraControls_) {
            if (state.property == camera::CameraProperty::Exposure && state.supported) {
                controller_.requestExposureSweep(state.min, state.max);
            }
        }
    });
    connect(page, &CameraImagePage::resolutionChosen, this,
            [this](const camera::CameraResolution& resolution) {
                savedResolution_ = resolution;
                if (repos_.settings != nullptr) {
                    repos_.settings->setInt("cam_width", resolution.width);
                    repos_.settings->setInt("cam_height", resolution.height);
                }
            });
    connect(page, &CameraImagePage::controlChanged, this,
            [this](const camera::CameraControlValue& control) {
                // Si el operador vuelve a encender un automático, el aviso tiene
                // que aparecer: no es menos peligroso por haberlo pedido él.
                if (control.property == camera::CameraProperty::AutoExposure) {
                    autoExposureOn_ = control.value > 0.5;
                    updateCalibrationLabel();
                }
                if (control.property == camera::CameraProperty::AutoFocus) {
                    autoFocusOn_ = control.value > 0.5;
                    updateCalibrationLabel();
                }
                // Se recuerda el último valor de cada propiedad para reaplicarlo
                // en el próximo arranque.
                for (auto& saved : savedCameraControls_) {
                    if (saved.property == control.property) {
                        saved.value = control.value;
                        if (repos_.settings != nullptr) {
                            repos_.settings->setDouble(
                                std::string(camera::propertyKey(control.property)),
                                control.value);
                        }
                        return;
                    }
                }
                savedCameraControls_.push_back(control);
                if (repos_.settings != nullptr) {
                    repos_.settings->setDouble(
                        std::string(camera::propertyKey(control.property)), control.value);
                }
            });
}

}  // namespace pci::ui
