#include "ui/main_window.h"
#include "ui/main_window_internal.h"

namespace pci::ui {

void MainWindow::persistPipelineConfig() {
    if (repos_.settings == nullptr) {
        return;
    }
    const auto& seg = pipelineConfig_.segmentation;
    repos_.settings->setInt("det_threshold", seg.manualThreshold);
    repos_.settings->setInt("det_polarity", static_cast<int>(seg.polarity));
    repos_.settings->setInt("det_blur", seg.blurKernel);
    repos_.settings->setInt("det_morph", seg.morphKernel);
    repos_.settings->setInt("det_split_touching", seg.splitTouchingPieces ? 1 : 0);
    repos_.settings->setInt("det_recover_glare", seg.recoverHighlightsBy);
    repos_.settings->setInt("det_background_key", static_cast<int>(seg.backgroundKey));
    repos_.settings->setInt("det_background_b", seg.background[0]);
    repos_.settings->setInt("det_background_g", seg.background[1]);
    repos_.settings->setInt("det_background_r", seg.background[2]);
    repos_.settings->setInt("det_roi_x", pipelineConfig_.roi.x);
    repos_.settings->setInt("det_roi_y", pipelineConfig_.roi.y);
    repos_.settings->setInt("det_roi_w", pipelineConfig_.roi.width);
    repos_.settings->setInt("det_roi_h", pipelineConfig_.roi.height);
    repos_.settings->setString(kSettingFreeZone,
                               encodeZonePolygon(pipelineConfig_.roiPolygon));
    // Y a qué resolución están expresados los píxeles de arriba.
    persistPixelReference();
    repos_.settings->setDouble("det_min_area", pipelineConfig_.minAreaFraction);
    repos_.settings->setDouble("det_max_area", pipelineConfig_.maxAreaFraction);
    repos_.settings->setInt("track_rotation", pipelineConfig_.autoOrient ? 1 : 0);
    repos_.settings->setInt("det_subpixel", pipelineConfig_.subpixelEdges ? 1 : 0);
}

// A qué resolución están expresados los ajustes que van en PÍXELES: la zona de
// trabajo y el cero del tablero.
//
// Sin esto, esos ajustes se guardaban en píxeles a secas y se volvían a aplicar
// tal cual. Una zona dibujada sobre 1920×1080 y reabierta con una fuente de
// 640×480 señala otro sitio: recortada contra el frame se queda en un trozo que
// nadie eligió, o desaparece entera. En silencio, que es lo peor.
//
// Va en su propia función porque lo llaman los DOS que guardan píxeles. Estaba
// sólo en el de la zona, y quien tuviera puesto un cero de tablero y ninguna
// zona no guardaba referencia alguna: el arreglo no podía ni dispararse.
//
// `isValid()` no sirve para comprobarlo: en Qt, `QSize(0, 0).isValid()` es true
// —sólo exige que no sean negativos—, así que un ajuste ausente, que se lee
// como cero, pasaba por bueno.
//
// Y sin condición sobre lo que ya hubiera: al persistir, los ajustes están
// SIEMPRE en coordenadas del frame actual —o acaban de ponerse, o acaban de
// reajustarse a él—, así que la referencia es el frame actual y punto.
void MainWindow::persistPixelReference() {
    if (repos_.settings == nullptr) {
        return;
    }
    if (!lastFrame_.isNull()) {
        pixelReferenceSize_ = lastFrame_.size();
    }
    repos_.settings->setInt("det_zone_ref_w", pixelReferenceSize_.width());
    repos_.settings->setInt("det_zone_ref_h", pixelReferenceSize_.height());
}

void MainWindow::updateRoiButton() {
    updateWorkingZoneOverlay();
    if (zoneButton_ == nullptr) {
        return;
    }
    const bool hasRect = pipelineConfig_.roi.area() > 0;
    const bool hasFree = pipelineConfig_.roiPolygon.size() >= 3;

    // Las dos acciones de dibujar NO son marcables, y eso costó un fallo.
    //
    // Marcarlas parecía buena idea —enseñar cuál está activa— pero una acción
    // marcable se marca AL PULSARLA, y pulsar «Dibujar zona rectangular» solo
    // empieza el gesto: todavía no hay zona. El menú quedaba afirmando que
    // había una, y si el operador no llegaba a arrastrar, seguía mintiendo.
    //
    // El estado se lee donde no puede desincronizarse: en el texto del propio
    // botón, que se pone desde la configuración y no desde el clic.
    rectZoneAction_->setCheckable(false);
    freeZoneAction_->setCheckable(false);
    clearZoneAction_->setEnabled(hasRect || hasFree);
    // Deshabilitado CON MOTIVO: un «Quitar» vivo sin nada que quitar enseña a
    // desconfiar de los menús.
    clearZoneAction_->setToolTip(hasRect || hasFree
                                     ? tr("Vuelve a analizar la imagen entera.")
                                     : tr("No hay ninguna zona dibujada."));
    // El botón dice qué zona está EN USO, no cuál hay guardada, y la diferencia
    // es justo el fallo que tenía: con una libre guardada y una rectangular
    // dibujada después seguía diciendo «Zona libre» mientras actuaba la otra; y
    // con el modo en automática decía que había zona cuando no se aplicaba
    // ninguna.
    //
    // Quien manda es el MODO, que es lo mismo que decide qué se recorta de
    // verdad. Leerlo de otro sitio es como se llega a que la barra afirme una
    // cosa y el análisis haga otra.
    switch (zoneMode_) {
        case vision::WorkingZoneMode::Fixed:
            zoneButton_->setText(hasRect ? tr("Zona fija") : tr("Zona"));
            break;
        case vision::WorkingZoneMode::Free:
            zoneButton_->setText(hasFree ? tr("Zona libre") : tr("Zona"));
            break;
        case vision::WorkingZoneMode::Automatic:
            // La automática no la dibuja el operador: sigue a la pieza sola. Se
            // nombra para que no parezca que no hay ninguna.
            zoneButton_->setText(tr("Zona auto"));
            break;
        case vision::WorkingZoneMode::Off:
            zoneButton_->setText(tr("Zona"));
            break;
    }
}

void MainWindow::declareExpectedPieces(int expected) {
    expectedPieces_ = std::max(0, expected);
    pipelineConfig_.expectedPieces = expectedPieces_;
    updatePiecesChip();
    reanalyseCurrentFrame();
}

bool MainWindow::countingPieces() const {
    // La pieza declara que espera varias: el recuento es parte del veredicto y
    // se cuenta siempre.
    if (expectedPieces_ > 1) {
        return true;
    }
    // UNA PIEZA DECLARADA A MANO ES UNA PIEZA, Y SE DEJA DE ENUMERAR.
    //
    // Esta línea sale de una queja de uso: «por defecto le tengo una pieza, e
    // intenta detectar más de una». Era exacto, y de dos maneras a la vez: la
    // regla del final de esta función manda contar por defecto, y con el
    // recuento en marcha cualquier sombra o reflejo que pase el filtro de área
    // sale como una segunda pieza; y como el número esperado también era 1, esa
    // sombra daba directamente NG «esperaba 1, veo 2».
    //
    // Quien pone «manual, una pieza» está diciendo que lo que hay en la mesa es
    // una pieza. Se mide la mayor y se acabó.
    //
    // Esto NO deshace la razón por la que contar venía puesto —seis piezas
    // delante y la aplicación midiendo la mayor sin decir que había otras
    // cinco—: ese caso es el modo AUTOMÁTICO, que ahora es un botón con su
    // nombre en la pestaña Piezas en vez de un cero mágico escondido dentro de
    // un campo numérico.
    if (expectedPieces_ == 1 &&
        !(configureDialog_ != nullptr && configureDialog_->showingPieceCount())) {
        return false;
    }
    // O el operador está mirando la pestaña Piezas. Antes bastaba con que el
    // panel Configurar estuviera abierto, y eso era demasiado: contar cuesta
    // una segmentación multi-pieza y además suelta el recorte automático, así
    // que abrir la pestaña de la Cámara pagaba las dos cosas para nadie. Peor
    // aún en la de Rendimiento, que es donde se enciende la zona automática:
    // el operador la encendía y la veía apagada, por su propia culpa de estar
    // mirándola.
    if (configureDialog_ != nullptr && configureDialog_->showingPieceCount()) {
        return true;
    }
    // Y, por defecto, SIEMPRE que el recorte automático no esté en juego.
    //
    // La regla de antes dejaba fuera el caso normal: seis piezas delante del
    // operador, ajustes de fábrica, y la aplicación midiendo la mayor sin decir
    // en ningún sitio que había otras cinco. «Solo detecta una» era literal.
    //
    // La justificación era que contar cuesta una segmentación multi-pieza.
    // Medido sobre 1920x1080 con seis piezas: 7,5 ms quedarse con la mayor,
    // 11,2 ms contarlas todas. Son 3,7 ms de 33 que dura un frame a 30 fps —no
    // es un coste, es ruido.
    //
    // Lo que sí es real es la otra mitad: contar SUELTA el recorte automático
    // (rodea a la pieza mayor, así que contar dentro daría siempre uno). Por eso
    // ahí se respeta la regla anterior y sólo se cuenta si alguien lo pide.
    return zoneMode_ != vision::WorkingZoneMode::Automatic;
}

cv::Rect MainWindow::effectiveWorkingZone() const {
    return vision::effectiveWorkingZone(zoneMode_, pipelineConfig_.roi, autoRoi_.roi(),
                                        countingPieces(), pipelineConfig_.roiPolygon);
}

vision::PipelineConfig MainWindow::inspectionConfig() const {
    vision::PipelineConfig config = pipelineConfig_;
    config.roiPolygon =
        vision::effectiveWorkingPolygon(zoneMode_, pipelineConfig_.roiPolygon);
    return config;
}

core::Result<vision::PieceAnalysis> MainWindow::analyseMeasuredPiece(
    const cv::Mat& image) const {
    // Con el navegador en cero se toma el camino de UNA pieza, que es el mismo
    // de siempre. No es una optimización de adorno: `analyzeFrames` analiza
    // TODAS las manchas que pasan el filtro de área, y este camino corre
    // también cuando solo hay una pieza en la mesa.
    if (focusedPiece_ < 1) {
        return vision::analyzeFrame(image, inspectionConfig());
    }
    auto all = vision::analyzeFrames(image, inspectionConfig());
    if (!all.isOk()) {
        return core::Result<vision::PieceAnalysis>::err(all.error().message);
    }
    if (all.value().empty()) {
        return core::Result<vision::PieceAnalysis>::err("No se detectó ninguna pieza");
    }
    const std::size_t chosen = vision::measuredPieceIndex(all.value(), focusedPiece_);
    return core::Result<vision::PieceAnalysis>::ok(std::move(all.value()[chosen]));
}

void MainWindow::setWorkingZoneMode(vision::WorkingZoneMode mode) {
    if (mode == zoneMode_) {
        return;
    }
    zoneMode_ = mode;
    // Al cambiar de modo se olvida lo seguido hasta ahora: reanudar con un
    // recorte viejo mediría dentro de una ventana que ya no corresponde.
    autoRoi_.reset();
    if (repos_.settings != nullptr) {
        repos_.settings->setString("work_zone_mode",
                                   vision::workingZoneModeKey(mode));
    }
    // El modo puede cambiar sin tocar el panel (al dibujar o quitar la zona),
    // así que si está abierto se le pone al día. `showMode` no reemite.
    if (configureDialog_ != nullptr) {
        if (auto* page = configureDialog_->performancePage(); page != nullptr) {
            page->showMode(zoneMode_, pipelineConfig_.roi.area() > 0,
                           pipelineConfig_.roiPolygon.size() >= 3);
        }
    }
    updateWorkingZoneOverlay();
}

// La zona que se está procesando, dibujada sobre el vídeo. Sin esto, el
// operador no tiene forma de saber por dónde está mirando el programa cuando
// algo falla.
void MainWindow::updateWorkingZoneOverlay() {
    const auto polygon =
        vision::effectiveWorkingPolygon(zoneMode_, pipelineConfig_.roiPolygon);
    const cv::Rect zone = effectiveWorkingZone();
    // Con la zona libre en uso se pinta el polígono y NO su envolvente: el
    // recuadro es solo cómo se recorta por dentro, y dibujarlo diría que se
    // analiza un rectángulo que el operador rechazó a propósito.
    video_->setFreeZone(!polygon.empty(), polygon);
    video_->setDetectionRegion(polygon.empty() && zone.area() > 0, zone);
}

void MainWindow::applyPiecesPage(PiecesPage* page) {
    if (page == nullptr) {
        return;
    }
    // POR LA MISMA PUERTA QUE TODO LO DEMÁS.
    //
    // Antes esto solo asignaba `expectedPieces_` y se olvidaba de la mitad: no
    // tocaba la configuración del pipeline ni pedía reanalizar, así que cambiar
    // el número en la ventana no cambiaba NADA hasta el siguiente fotograma que
    // llegara por otro motivo — y con una imagen parada, nunca.
    //
    // Lo peor es por qué no saltó: `declareExpectedPieces` sí lo hacía bien, y
    // era la que usaban las pruebas. Un camino de prueba que funciona mientras
    // el camino de verdad no, y las dos con el mismo nombre en la cabeza de
    // quien las escribió. Ahora hay uno solo.
    declareExpectedPieces(page->expectedPieces());
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 || repos_.pieces == nullptr) {
        statusBar()->showMessage(
            tr("Selecciona una pieza para guardar cuántas se esperan: el número va "
               "con el trabajo, no con la máquina."));
        return;
    }
    // Se lee y se reescribe la medición entera: es una sola fila y así no hay
    // dos caminos distintos para tocar las columnas de la pieza.
    auto measurement = repos_.pieces->loadMeasurement(pieceId);
    if (!measurement.isOk()) {
        core::logWarning("No se pudo leer la medición de la pieza: " +
                         measurement.error().message);
        return;
    }
    measurement.value().expectedPieces = expectedPieces_;
    showMosaic_ = page->showMosaic();
    measurement.value().showMosaic = page->showMosaic();
    if (auto saved = repos_.pieces->saveMeasurement(pieceId, measurement.value());
        !saved.isOk()) {
        core::logWarning("No se pudieron guardar las piezas esperadas: " +
                         saved.error().message);
    }
}

// Vuelca la página de detección del panel Configurar en la configuración viva.
void MainWindow::applyDetectionPage(DetectionPage* page) {
    if (page == nullptr) {
        return;
    }
    pipelineConfig_.segmentation = page->options();
    pipelineConfig_.minAreaFraction = page->minAreaFraction();
    pipelineConfig_.maxAreaFraction = page->maxAreaFraction();

    // Encender o apagar el afinado subpíxel NO es un ajuste más: cambia dónde
    // está el borde, y con él el área, el perímetro y todas las cotas de la
    // pieza a la vez. Quien tenga tolerancias ajustadas contra el borde de antes
    // vería una pieza buena salir NG por un cambio de definición y no por un
    // defecto.
    //
    // Así que se avisa, y solo cuando CAMBIA. Repetir el aviso cada vez que se
    // aceptan los ajustes de detección lo convertiría en algo que se cierra sin
    // leer, que es peor que no avisar.
    const bool subpixelBefore = pipelineConfig_.subpixelEdges;
    pipelineConfig_.subpixelEdges = page->subpixelEdges();
    if (pipelineConfig_.subpixelEdges != subpixelBefore) {
        QMessageBox::information(
            this, tr("Ha cambiado la definición del borde"),
            pipelineConfig_.subpixelEdges
                ? tr("El borde se afina a subpíxel y las medidas cambian un poco. Revisa "
                     "las tolerancias: una pieza buena podría salir NG por este cambio y "
                     "no por un defecto.")
                : tr("El borde vuelve a ser el que marca el umbral, y las medidas cambian "
                     "un poco. Revisa las tolerancias que ajustaste con el afinado "
                     "encendido."));
    }
    persistPipelineConfig();

    // El perfil elegido se guarda CON LA PIEZA: cada pieza puede necesitar una
    // iluminación distinta y así no hay que reajustar al cambiar de una a otra.
    currentProfileId_ = page->selectedProfileId();
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId >= 0 && repos_.detectionProfiles != nullptr) {
        if (auto saved = repos_.detectionProfiles->assignToPiece(pieceId, currentProfileId_);
            !saved.isOk()) {
            core::logWarning("No se pudo guardar el perfil de la pieza: " +
                             saved.error().message);
        }
    }
    statusBar()->showMessage(
        currentProfileId_ > 0
            ? tr("Ajustes de detección aplicados y guardados en el perfil de la pieza.")
            : tr("Ajustes de detección aplicados: el contorno en vivo ya los usa."));
    reanalyseCurrentFrame();
}

// Perfil de detección de la pieza seleccionada (O3): si tiene uno, sus ajustes
// mandan sobre los globales; si no, todo sigue como antes.
// LA PÁGINA DE DETECCIÓN, SI CONFIGURAR ESTÁ ABIERTO.
//
// Va aquí y no en `loadMeasurementForSelectedPiece` por el ORDEN: aquella corre
// antes y deja `pipelineConfig_` y `currentProfileId_` todavía con lo de la
// pieza anterior. Poner al día la página desde allí la llenaría con lo viejo,
// que es exactamente el fallo que se vino a quitar.
void MainWindow::refreshDetectionPageForSelectedPiece() {
    if (configureDialog_ == nullptr) {
        return;
    }
    if (auto* page = configureDialog_->detectionPage(); page != nullptr) {
        page->reloadFor(pipelineConfig_.segmentation, currentProfileId_,
                        pipelineConfig_.minAreaFraction, pipelineConfig_.maxAreaFraction,
                        pipelineConfig_.subpixelEdges);
    }
}

void MainWindow::loadDetectionProfileForSelectedPiece() {
    currentProfileId_ = 0;
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 || repos_.detectionProfiles == nullptr) {
        refreshDetectionPageForSelectedPiece();
        return;
    }
    auto assigned = repos_.detectionProfiles->profileForPiece(pieceId);
    if (!assigned.isOk() || assigned.value() <= 0) {
        // Sin perfil asignado también hay que poner al día la página: se
        // trabaja con los ajustes sueltos, y los de la pieza nueva no tienen
        // por qué ser los que se quedó enseñando de la anterior.
        refreshDetectionPageForSelectedPiece();
        return;
    }
    auto profile = repos_.detectionProfiles->load(assigned.value());
    if (!profile.isOk()) {
        refreshDetectionPageForSelectedPiece();
        return;  // perfil borrado a mano: se sigue con los ajustes globales
    }
    currentProfileId_ = profile.value().id;
    pipelineConfig_.segmentation = profile.value().options;
    refreshDetectionPageForSelectedPiece();
    statusBar()->showMessage(tr("Detección: perfil '%1' de esta pieza.")
                                 .arg(QString::fromStdString(profile.value().name)));
    reanalyseCurrentFrame();
}

// Quitar la zona que haya, sea la que sea. Va aparte porque ahora es una acción
// con su propio nombre en el menú, y no «volver a pulsar el botón de dibujar»,
// que obligaba a que la etiqueta cambiara de verbo para avisar.
void MainWindow::onClearZoneClicked() {
    const bool hadRect = pipelineConfig_.roi.area() > 0;
    const bool hadFree = pipelineConfig_.roiPolygon.size() >= 3;
    if (!hadRect && !hadFree) {
        return;
    }
    pipelineConfig_.roi = cv::Rect();
    pipelineConfig_.roiPolygon.clear();
    if (hadRect) {
        setWorkingZoneMode(vision::modeAfterFixedZoneChanged(zoneMode_, false));
    }
    if (hadFree) {
        setWorkingZoneMode(vision::modeAfterFreeZoneChanged(zoneMode_, false));
    }
    persistPipelineConfig();
    updateRoiButton();
    // Cambiar la zona sin volver a medir deja el contorno de la zona anterior:
    // sobre una foto no llega ningún frame nuevo que fuerce el recálculo.
    reanalyseCurrentFrame();
    statusBar()->showMessage(tr("Zona quitada: se vuelve a analizar la imagen entera."));
}

void MainWindow::onRoiButtonToggled(bool enabled) {
    if (!enabled) {
        video_->setRegionPickMode(false);
        return;
    }
    if (lastFrame_.isNull()) {
        statusBar()->showMessage(tr("Inicia la cámara para dibujar la zona de detección."));
        return;
    }
    video_->setRegionPickMode(true);
    statusBar()->showMessage(
        tr("Arrastra un rectángulo sobre el video: la detección se limitará a esa zona."));
}

void MainWindow::onRegionPicked(const cv::Rect& imageRect) {
    pipelineConfig_.roi = imageRect;
    // Dibujar la zona la PONE EN USO (la regla vive en `vision::auto_roi`).
    setWorkingZoneMode(vision::modeAfterFixedZoneChanged(zoneMode_, true));
    persistPipelineConfig();
    updateRoiButton();
    reanalyseCurrentFrame();
    statusBar()->showMessage(
        tr("Zona de detección activa: el contorno solo se busca dentro del recuadro."));
}

void MainWindow::onFreeZoneButtonToggled(bool enabled) {
    if (!enabled) {
        video_->setFreeZonePickMode(false);
        return;
    }
    if (lastFrame_.isNull()) {
        statusBar()->showMessage(tr("Inicia la fuente para dibujar la zona libre."));
        return;
    }
    video_->setFreeZonePickMode(true);
    statusBar()->showMessage(
        tr("Rodea la zona arrastrando el ratón, o marca las esquinas a clics y cierra "
           "sobre la primera. Botón derecho: deshacer el último vértice."));
}

void MainWindow::onFreeZonePicked(const std::vector<cv::Point>& imagePolygon) {
    pipelineConfig_.roiPolygon = imagePolygon;
    setWorkingZoneMode(vision::modeAfterFreeZoneChanged(zoneMode_, true));
    persistPipelineConfig();
    updateRoiButton();
    reanalyseCurrentFrame();
    // El número de vértices no es decoración: es lo que permite notar que un
    // trazo de doce esquinas se guardó con cuatro, o al revés.
    statusBar()->showMessage(tr("Zona libre activa (%1 vértices): se analiza solo lo de "
                                "dentro, y lo de fuera se oscurece.")
                                 .arg(imagePolygon.size()));
}

void MainWindow::onFreeZoneCancelled() {
    updateRoiButton();
    statusBar()->showMessage(tr("Zona libre cancelada: sigue la de antes."));
}

void MainWindow::updateCalibrationLabel() {
    // La tira se refresca aquí y no aparte: este método ya se llama en todos
    // los sitios donde cambia cualquiera de los cuatro datos que enseña
    // —calibrar, cambiar de cámara, tocar un automático, mover la zona—, así
    // que engancharse a él es engancharse a todos de una vez.
    updateStationStatus();
    updateSetupGuide();
    // La escala por ArUco gestiona su propia etiqueta por frame (es dinámica).
    if (arucoLiveScale_) {
        return;
    }
    if (!calibration_.valid()) {
        calibLabel_->setText(tr("Sin calibrar (medidas en px)"));
        return;
    }
    // D1: la calibración se hizo a una resolución y cámara concretas. Si el
    // frame actual no coincide, la escala en px ya no es fiable: se avisa en vez
    // de mostrar milímetros silenciosamente equivocados.
    const bool resMismatch =
        !lastFrame_.isNull() &&
        !calibration_.matchesResolution(lastFrame_.width(), lastFrame_.height());
    const bool camMismatch = !calibratedCameraKey_.isEmpty() &&
                             !currentCameraKey_.isEmpty() &&
                             calibratedCameraKey_ != currentCameraKey_;
    if (resMismatch || camMismatch) {
        // «Otra cámara» dejó de ser cierto en cuanto una imagen o un vídeo
        // pueden ser la fuente, y el motivo es lo único que le dice al operador
        // si tiene que recalibrar o si puede fiarse. La escala en px/mm depende
        // de la óptica y de la distancia al plano, y un fichero no garantiza
        // ninguna de las dos: al abrirlo, la escala de la estación deja de
        // valer, aunque la imagen tenga el mismo tamaño.
        QString why;
        if (resMismatch) {
            why = tr("otra resolución");
        } else if (sourceKind_ != camera::SourceKind::Camera &&
                   sourceKind_ != camera::SourceKind::Photo) {
            why = tr("la escala se calibró con otra fuente y un fichero no dice a qué "
                     "distancia se tomó");
        } else {
            why = tr("otra cámara");
        }
        calibLabel_->setText(tr("⚠ Calibración obsoleta (%1): recalibra con C").arg(why));
        return;
    }
    // Escala calibrada + automático encendido es la combinación que da números
    // creíbles y falsos, y el sitio donde hay que decirlo es este: junto a la
    // escala que el operador se está creyendo, no en una pestaña que no abrirá.
    const std::string warning =
        camera::automaticsWarning(true, autoExposureOn_, autoFocusOn_);
    if (!warning.empty()) {
        calibLabel_->setText(tr("⚠ Escala: %1 mm/px · %2")
                                 .arg(calibration_.mmPerPixel, 0, 'f', 4)
                                 .arg(QString::fromStdString(warning)));
        return;
    }
    calibLabel_->setText(tr("Escala: %1 mm/px · cámara ~%2 mm")
                             .arg(calibration_.mmPerPixel, 0, 'f', 4)
                             .arg(calibration_.cameraDistanceMm, 0, 'f', 0));
}

// Sella la cámara actual y persiste toda la calibración (incluida la resolución
// que el llamante ya fijó en calibration_). Base común de los dos flujos de
// calibración (diálogo y "fijar con esta medida").
void MainWindow::persistCalibration() {
    calibratedCameraKey_ = currentCameraKey_;
    if (repos_.settings == nullptr) {
        return;
    }
    repos_.settings->setDouble("calib_mm_per_px", calibration_.mmPerPixel);
    repos_.settings->setDouble("calib_camera_dist_mm", calibration_.cameraDistanceMm);
    repos_.settings->setDouble("calib_fov_deg", calibration_.horizontalFovDeg);
    repos_.settings->setInt("calib_width", calibration_.calibratedWidth);
    repos_.settings->setInt("calib_height", calibration_.calibratedHeight);
    repos_.settings->setString("calib_camera", calibratedCameraKey_.toStdString());
}

void MainWindow::onCalibrateClicked() {
    const QImage snapshot = frameOrFile();
    if (snapshot.isNull()) {
        return;
    }
    // LA REFERENCIA QUE SE ESCRIBIÓ LA VEZ ANTERIOR.
    //
    // Es lo único que hay que teclear cada vez que se calibra, y era lo único
    // que no se recordaba: el campo volvía a 100 mm aunque la regla del puesto
    // midiera 6 pulgadas. La distancia de cámara y el FOV sí se recuperaban, lo
    // que lo hacía aún más difícil de entender.
    ScaleEntry last;
    if (repos_.settings != nullptr) {
        const double saved = repos_.settings->getDouble("scale_known_length", 0.0).valueOr(0.0);
        if (saved > 0.0) {
            last.knownLength = saved;
        }
        last.unitIndex = repos_.settings->getInt("scale_known_unit", 0).valueOr(0);
    }
    CalibrationDialog dialog(snapshot, calibration_, last, currentUnit(), this);
    keepDialogSize(dialog, repos_.settings, "calibration", 1000, 640);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (repos_.settings != nullptr) {
        const ScaleEntry entered = dialog.lastEntry();
        repos_.settings->setDouble("scale_known_length", entered.knownLength);
        repos_.settings->setInt("scale_known_unit", entered.unitIndex);
    }
    calibration_ = dialog.calibration();
    calibration_.calibratedWidth = snapshot.width();
    calibration_.calibratedHeight = snapshot.height();
    persistCalibration();
    updateCalibrationLabel();
    video_->setMmPerPixel(calibration_.mmPerPixel);
    statusBar()->showMessage(
        tr("Escala calibrada: las medidas ahora se muestran también en mm."));
}


// El pincel solo se ofrece con una imagen QUIETA.
//
// En vídeo en vivo el contorno se recalcula en cada frame, así que un borde
// corregido a mano sería mentira en cuanto la pieza se moviera un píxel. Y
// apagado con su motivo, no muerto y en silencio: un control que no responde sin
// explicación se lee como que la aplicación está rota.
// El tamaño del pincel, en los tres sitios donde se ve, sin que ninguno mande
// sobre los otros.
//
// `fromCanvas` evita el ida y vuelta: si el cambio viene de la rueda del raton,
// mover el deslizador volveria a llamar al lienzo. Se bloquean las señales del
// deslizador en vez de comparar valores, que es lo unico que funciona cuando el
// valor se acota por el camino.
void MainWindow::applyBrushRadius(int radiusPx, bool fromCanvas) {
    if (brushSizeSlider_ == nullptr) {
        return;
    }
    const int wanted = std::clamp(radiusPx, brushSizeSlider_->minimum(),
                                  brushSizeSlider_->maximum());
    {
        const QSignalBlocker block(brushSizeSlider_);
        brushSizeSlider_->setValue(wanted);
    }
    if (!fromCanvas && video_ != nullptr) {
        video_->setBrushRadius(wanted);
    }
    if (brushSizeLabel_ != nullptr) {
        // El DIAMETRO, no el radio: es lo que se ve pintado, y lo que uno compara
        // con el hueco que quiere rellenar.
        brushSizeLabel_->setText(tr("%1 px").arg(wanted * 2));
    }
    if (repos_.settings != nullptr) {
        repos_.settings->setInt("brush_radius", wanted);
    }
}

// El asistente de calibracion de la lente.
//
// Come de la camara en vivo mientras esta abierto: `onFrame` le va pasando los
// fotogramas SIN corregir, que son los que hay que medir. Corregirlos antes
// seria pedirle a la lente que se mida a si misma ya enderezada.
void MainWindow::onCalibrateLensClicked() {
    if (lensDialog_ != nullptr) {
        lensDialog_->raise();
        lensDialog_->activateWindow();
        return;
    }
    auto* dialog = new LensCalibrationDialog(this);
    lensDialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QObject::destroyed, this, [this] { lensDialog_ = nullptr; });
    connect(dialog, &QDialog::accepted, this, [this, dialog] {
        if (dialog->result().has_value()) {
            applyLensCalibration(*dialog->result(), true);
        }
    });
    // Si ya hay una imagen parada —una foto o un fichero en pausa—, el asistente
    // tiene que verla igual: sin esto solo serviria con la camara en marcha.
    if (!lastFrame_.isNull()) {
        dialog->offerFrame(lastFrame_);
    }
    dialog->show();
}

// Guardar el modelo y, si se pide, encenderlo.
//
// Se GUARDA aunque no se encienda: calibrar cuesta un rato con un tablero en la
// mano, y perder ese trabajo por no querer mover las cotas hoy seria un castigo
// absurdo.
void MainWindow::applyLensCalibration(const vision::LensCalibration& calibration,
                                      bool enable) {
    lensCorrector_ = vision::LensCorrector(calibration);
    if (repos_.settings != nullptr) {
        repos_.settings->setString("lens_model", vision::serializeCalibration(calibration));
    }
    const double worst = vision::worstDisplacementPx(calibration);
    if (lensCorrectionAction_ != nullptr) {
        lensCorrectionAction_->setEnabled(lensCorrector_.isReady());
        // Se dice en PIXELES y no en coeficientes: «tu lente desplaza hasta 34 px
        // en las esquinas» se entiende, y «k1 = -0,2478» no.
        if (vision::distortionIsNegligible(calibration)) {
            statusBar()->showMessage(
                tr("Lente calibrada: desplaza %1 px como mucho, así que corregirla no "
                   "cambiaría ninguna medida. Se guarda por si cambias de cámara.")
                    .arg(worst, 0, 'f', 1));
            enable = false;
        } else {
            statusBar()->showMessage(
                tr("Lente calibrada con %1 tomas: desplaza hasta %2 px en el borde "
                   "(error de ajuste %3 px).")
                    .arg(calibration.views)
                    .arg(worst, 0, 'f', 1)
                    .arg(calibration.reprojectionError, 0, 'f', 3));
        }
        const QSignalBlocker block(lensCorrectionAction_);
        lensCorrectionAction_->setChecked(enable && lensCorrector_.isReady());
    }
    lensCorrectionOn_ = enable && lensCorrector_.isReady();
    if (repos_.settings != nullptr) {
        repos_.settings->setInt("lens_enabled", lensCorrectionOn_ ? 1 : 0);
    }
    reanalyseCurrentFrame();
}

void MainWindow::updateEdgeBrushAvailability() {
    if (edgeBrushButton_ == nullptr) {
        return;
    }
    // Una imagen QUIETA: una foto, un fichero de imagen, o un vídeo EN PAUSA.
    //
    // El vídeo pausado entra, y al principio no estaba: se dejó fuera por
    // pensar «vídeo = se mueve», pero un vídeo detenido en un frame es tan
    // quieto como una foto — y es justo donde hace falta corregir, porque es el
    // frame que uno ha elegido tras buscarlo con la barra.
    //
    // Lo que sigue fuera es el vídeo EN MARCHA y la cámara en vivo, y ahí la
    // razón se mantiene: el contorno se recalcula en cada frame, así que un
    // borde corregido a mano sería mentira en cuanto la pieza se moviera.
    const auto* video = dynamic_cast<const camera::VideoFileSource*>(fileSource_.get());
    const bool pausedVideo = video != nullptr && video->isPaused();
    const bool still = sourceKind_ == camera::SourceKind::Photo ||
                       sourceKind_ == camera::SourceKind::Image || pausedVideo;
    const bool usable = still && !lastFrame_.isNull();

    // Sin cambios, no se toca nada: esto se consulta en cada frame y reescribir
    // el tooltip sesenta veces por segundo es trabajo tirado.
    if (edgeBrushButton_->isEnabled() == usable && !edgeBrushButton_->toolTip().isEmpty()) {
        return;
    }
    edgeBrushButton_->setEnabled(usable);
    edgeBrushButton_->setToolTip(
        usable ? tr("Corrige a mano el borde cuando la detección se equivoca.")
        : sourceKind_ == camera::SourceKind::Video
            ? tr("Pausa el vídeo para corregir el borde.")
            : tr("Solo funciona con una imagen quieta: foto, fichero abierto o vídeo en "
                 "pausa."));
    edgeBrushButton_->setWhatsThis(
        usable ? tr("Verde es lo que añades, rojo lo que quitas. La rueda del ratón cambia "
                    "el tamaño del pincel. La corrección solo vale para esta imagen.")
        : sourceKind_ == camera::SourceKind::Video
            ? tr("Con el vídeo en marcha el contorno se recalcula en cada frame, así que "
                 "la corrección dejaría de valer al siguiente.")
            : tr("En vivo el contorno se recalcula en cada frame, así que un borde "
                 "corregido a mano dejaría de valer en cuanto la pieza se moviera."));
    if (!usable) {
        // Al dejar de poder usarse, el pincel se apaga solo: dejarlo encendido
        // haría que el siguiente clic sobre la imagen pintara sin que nadie lo
        // pidiera.
        if (brushAddAction_ != nullptr && brushRemoveAction_ != nullptr) {
            QSignalBlocker a(brushAddAction_);
            QSignalBlocker b(brushRemoveAction_);
            brushAddAction_->setChecked(false);
            brushRemoveAction_->setChecked(false);
        }
        video_->setEdgeBrush(inspection::EditorCanvas::EdgeBrush::Off);
    }
}

// MARCAR UNA PIEZA RODEÁNDOLA, Y DESCARTAR LO QUE NO LO ES.
//
// El trazo no se toma como si fuera la pieza, y ese es el punto entero: un
// contorno dibujado a pulso no se puede medir, así que el diámetro que saliera
// de ahí sería el pulso del operador con aspecto de cota. El trazo dice DÓNDE
// MIRAR y el borde lo busca `pieceInsideOutline` dentro, con el fondo que haya
// ahí — que es el mismo truco de la zona de trabajo, y por eso funciona con las
// piezas que el umbral global se deja fuera.
//
// Se aplica por `applyCorrectionArea` para que entre en la MISMA pila de
// deshacer que las pinceladas: dos formas de corregir el borde, una deshacible
// y otra no, se aprende perdiendo trabajo.
void MainWindow::onPieceOutlined(const std::vector<cv::Point>& polygon, bool add) {
    // El modo se apaga solo al terminar el trazo, como la zona libre: quien
    // quiera rodear otra vuelve a pulsar. Dejarlo encendido convierte el
    // siguiente arrastre —para mover, para dibujar— en una pieza inventada.
    if (outlineAddAction_ != nullptr) {
        outlineAddAction_->setChecked(false);
    }
    if (outlineDropAction_ != nullptr) {
        outlineDropAction_->setChecked(false);
    }
    if (lastFrame_.isNull() || polygon.size() < 3) {
        statusBar()->showMessage(tr("Ese trazo no encierra ninguna zona."));
        return;
    }

    const cv::Mat frame = camera::qImageToMat(lastFrame_);
    if (!add) {
        cv::Mat area(frame.size(), CV_8UC1, cv::Scalar(0));
        cv::fillPoly(area, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(255));
        video_->applyCorrectionArea(area, false);
        statusBar()->showMessage(
            tr("Descartado: %1 px pasan a ser fondo. Ctrl+Z lo deshace.")
                .arg(cv::countNonZero(area)));
        return;
    }

    const auto found = vision::pieceInsideOutline(frame, polygon, pipelineConfig_.segmentation);
    if (found.mask.empty()) {
        statusBar()->showMessage(QString::fromStdString(found.why));
        return;
    }
    video_->applyCorrectionArea(found.mask, true);
    // El motivo va entero a la barra de estado, y con él la diferencia que
    // importa: si el borde salió de la imagen, la pieza se puede medir; si salió
    // del trazo, no. Callarlo dejaría al operador aceptando cotas de su pulso.
    statusBar()->showMessage(QString::fromStdString(found.why));
}

void MainWindow::onEdgeCorrected(const cv::Mat& forcePiece, const cv::Mat& forceBackground) {
    // La corrección viene ya clonada del lienzo, pero se vuelve a clonar aquí
    // por lo mismo: estas máscaras viajan a un hilo de trabajo, y compartir un
    // búfer que otro hilo puede reasignar es la clase de fallo que se manifiesta
    // como una aplicación que se cierra sola sin decir nada.
    pipelineConfig_.forcePiece = forcePiece.clone();
    pipelineConfig_.forceBackground = forceBackground.clone();

    // Y se comprueba que la corrección CORRESPONDE a la imagen que se está
    // analizando. Si no, `applyMaskCorrection` la ignora en silencio —hace bien,
    // aplicarla desplazada sería peor— pero el operador vería su pincelada
    // pintada en pantalla y el contorno sin moverse, sin ninguna explicación.
    if (!lastFrame_.isNull() && !pipelineConfig_.forcePiece.empty() &&
        (pipelineConfig_.forcePiece.cols != lastFrame_.width() ||
         pipelineConfig_.forcePiece.rows != lastFrame_.height())) {
        const int hadCols = pipelineConfig_.forcePiece.cols;
        const int hadRows = pipelineConfig_.forcePiece.rows;
        // Y NO SE GUARDA, que es lo que faltaba.
        //
        // Estas dos líneas se asignaban ARRIBA, antes de comprobar si la
        // corrección sirve, así que al salir por aquí quedaba una máscara del
        // tamaño equivocado. `applyMaskCorrection` la ignora —hace bien,
        // aplicarla desplazada borraría un trozo cualquiera de la pieza— pero la
        // ignora EN SILENCIO.
        //
        // El resultado era el peor de los tres posibles: el operador veía la
        // pastilla «Borde corregido» encendida, el contorno sin moverse, y la
        // única explicación en un mensaje de la barra de estado que se va solo.
        // Desde fuera eso es «pasé el pincel y sigue remarcando la zona».
        pipelineConfig_.forcePiece = cv::Mat();
        pipelineConfig_.forceBackground = cv::Mat();
        updateEdgeCorrectionChip();
        statusBar()->showMessage(
            tr("La corrección es de una imagen de %1×%2 y ahora se ve una de %3×%4: no se "
               "puede aplicar. Vuelve a corregir sobre esta.")
                .arg(hadCols)
                .arg(hadRows)
                .arg(lastFrame_.width())
                .arg(lastFrame_.height()));
        return;
    }

    // Se reanaliza en el acto: el sentido de corregir es VER el borde nuevo.
    //
    // Y se apunta que, en cuanto ese analisis llegue, hay que RETIRAR el trazo.
    // No antes: entre soltar el pincel y ver el contorno nuevo hay unas decimas,
    // y quitar la mancha en ese hueco dejaria un momento en el que no se ve ni
    // lo pintado ni el resultado, que se lee como que no ha pasado nada.
    hideCorrectionWhenAnalysed_ = true;
    reanalyseCurrentFrame();
    const int added = forcePiece.empty() ? 0 : cv::countNonZero(forcePiece);
    const int removed = forceBackground.empty() ? 0 : cv::countNonZero(forceBackground);
    // Sin corrección no hay nada que aprender, y afinar con la nada devolvería
    // los ajustes de ahora presentados como un hallazgo.
    if (brushTuneAction_ != nullptr) {
        brushTuneAction_->setEnabled(added > 0 || removed > 0);
    }
    statusBar()->showMessage(
        added == 0 && removed == 0
            ? tr("Sin correcciones: el borde es el que detecta el programa.")
            : tr("Borde corregido a mano: +%1 px, −%2 px. Ctrl+Z deshace la pincelada; en "
                 "«Corregir borde» puedes afinar la detección con ella.")
                  .arg(added)
                  .arg(removed));
    updateEdgeCorrectionChip();
}

// Afinar la detección a partir de una corrección a mano.
//
// Corregir el borde arregla ESTA imagen. Pero la corrección es, literalmente,
// la respuesta correcta: dice qué es pieza y qué no en un caso que la detección
// falló. Con la respuesta correcta delante se puede buscar qué ajuste la habría
// dado solo — y si existe, dejar de corregir a mano una imagen tras otra.
//
// Se hace a petición y no tras cada pincelada: sobre un frame de 1920x1080 la
// búsqueda cuesta unos 650 ms medidos, y meterlos en cada trazo convertiría el
// pincel en algo intratable.
void MainWindow::onTuneDetectionFromEdge() {
    if (lastFrame_.isNull()) {
        statusBar()->showMessage(tr("No hay imagen sobre la que afinar."));
        return;
    }
    const cv::Mat image = camera::qImageToMat(lastFrame_);
    auto detected = vision::segmentPiece(image, pipelineConfig_.segmentation);
    if (!detected.isOk()) {
        statusBar()->showMessage(
            tr("No se pudo segmentar la imagen para compararla con tu corrección."));
        return;
    }

    // La verdad según el operador: lo que detecta el programa, con la
    // corrección aplicada encima. Mismo orden que el análisis —primero añadir,
    // después quitar— para que lo que se busca sea EXACTAMENTE lo que se ve.
    cv::Mat truth = detected.value();
    vision::applyMaskCorrection(truth, pipelineConfig_, cv::Rect(), false);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto suggestion =
        vision::suggestSegmentation(image, truth, pipelineConfig_.segmentation);
    QApplication::restoreOverrideCursor();

    if (!suggestion.found) {
        statusBar()->showMessage(tr("No se pudo evaluar ningún ajuste sobre esta imagen."));
        return;
    }

    const auto percent = [](double value) { return QString::number(100.0 * value, 'f', 1); };

    if (!suggestion.worthApplying()) {
        // Y se dice CON LAS CIFRAS. «No hay nada que cambiar» sin números es
        // indistinguible de «no lo he mirado».
        QMessageBox::information(
            this, tr("Afinar la detección"),
            tr("Con estos ajustes no se gana nada: los actuales llegan al %1 % y el "
               "mejor que encontré al %2 %. Si el borde sigue saliendo mal, revisa la "
               "iluminación, el enfoque o la zona de trabajo.")
                .arg(percent(suggestion.agreementNow))
                .arg(percent(suggestion.agreementSuggested)));
        return;
    }

    const auto& proposed = suggestion.options;
    const QString polarity = proposed.polarity == vision::SegmentationPolarity::DarkPiece
                                 ? tr("pieza oscura sobre fondo claro")
                                 : proposed.polarity == vision::SegmentationPolarity::LightPiece
                                       ? tr("pieza clara sobre fondo oscuro")
                                       : tr("automática");
    const auto answer = QMessageBox::question(
        this, tr("Afinar la detección"),
        tr("Hay un ajuste que habría detectado este borde sin corregirlo a mano: ahora "
           "coincide en un %1 %, y con umbral %3 y polaridad «%4» llegaría al %2 %. Se "
           "aplicaría a todas las piezas que se midan de aquí en adelante. ¿Lo aplico?")
            .arg(percent(suggestion.agreementNow))
            .arg(percent(suggestion.agreementSuggested))
            .arg(proposed.manualThreshold)
            .arg(polarity),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        statusBar()->showMessage(tr("Ajustes sin tocar: la corrección sigue valiendo para "
                                    "esta imagen."));
        return;
    }

    pipelineConfig_.segmentation = proposed;
    persistPipelineConfig();
    // Y se quita la corrección: si los ajustes nuevos dan el mismo borde, la
    // corrección ya no pinta nada, y dejarla puesta escondería si el ajuste
    // funciona de verdad o si lo que se ve sigue siendo la pincelada.
    video_->clearEdgeCorrection();
    reanalyseCurrentFrame();
    statusBar()->showMessage(tr("Detección afinada: umbral %1, coincidencia %2 %. La "
                                "corrección a mano se ha retirado: lo que ves ahora sale "
                                "de los ajustes.")
                                 .arg(proposed.manualThreshold)
                                 .arg(percent(suggestion.agreementSuggested)));
}

}  // namespace pci::ui
