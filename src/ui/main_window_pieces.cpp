#include "ui/main_window.h"
#include "ui/main_window_internal.h"

#include "camera/frame_utils.h"
#include "core/logging.h"
#include "inspection_editor/editor_window.h"
#include "repositories/piece_repository.h"
#include "repositories/tool_repository.h"
#include "ui/dialog_geometry.h"
#include "ui/history_dialog.h"
#include "ui/measurement_mode_dialog.h"
#include "ui/piece_manager_dialog.h"
#include "ui/registration_wizard.h"
#include "ui/template_manager_dialog.h"
#include "ui/theme.h"

#include <QComboBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QStatusBar>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <string>
#include <vector>

namespace pci::ui {

namespace {

constexpr int kCaptureMinimum = 5;

}  // namespace

void MainWindow::onManagePiecesClicked() {
    if (repos_.pieces == nullptr) {
        QMessageBox::warning(this, tr("BD no disponible"),
                             tr("La gestión de piezas necesita la base de datos."));
        return;
    }
    const std::int64_t previous = selectedPieceId();
    PieceManagerDialog dialog(repos_.pieces, repos_.tools, this);
    keepDialogSize(dialog, repos_.settings, "pieces", 560, 480);
    dialog.exec();
    if (dialog.changed()) {
        autoInspectButton_->setChecked(false);
        loadPieceList(previous);
        onPieceSelectionChanged(pieceCombo_->currentIndex());
    }
}

// Repuebla el combo de plantillas de la pieza actual. Siempre incluye
// "principal" aunque aún no tenga herramientas.
void MainWindow::loadTemplateList(const QString& selectName) {
    QSignalBlocker blocker(templateCombo_);
    const QString previous = selectName.isEmpty() ? templateCombo_->currentText()
                                                  : selectName;
    templateCombo_->clear();

    std::vector<std::string> names{"principal"};
    if (const std::int64_t pieceId = selectedPieceId();
        pieceId >= 0 && repos_.tools != nullptr) {
        if (auto listed = repos_.tools->listTemplates(pieceId); listed.isOk()) {
            for (const auto& name : listed.value()) {
                if (std::find(names.begin(), names.end(), name) == names.end()) {
                    names.push_back(name);
                }
            }
        }
    }
    for (const auto& name : names) {
        templateCombo_->addItem(QString::fromStdString(name));
    }
    const int idx = templateCombo_->findText(previous);
    templateCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
}

void MainWindow::onTemplateChanged(int index) {
    Q_UNUSED(index);
    // Igual que al cambiar de pieza: no perder cambios sin guardar en silencio.
    if (!confirmSaveBeforeLeaving()) {
        QSignalBlocker blocker(templateCombo_);
        const int idx = templateCombo_->findText(loadedTemplate_);
        if (idx >= 0) {
            templateCombo_->setCurrentIndex(idx);
        }
        return;
    }
    autoInspectButton_->setChecked(false);
    loadToolsForSelectedPiece();
}

void MainWindow::onShowHistoryClicked() {
    if (repos_.inspections == nullptr || repos_.pieces == nullptr) {
        QMessageBox::information(this, tr("Historial no disponible"),
                                 tr("El historial necesita la base de datos."));
        return;
    }
    HistoryDialog dialog(repos_.inspections, repos_.pieces, selectedPieceId(), this);
    keepDialogSize(dialog, repos_.settings, "history", 640, 460);
    dialog.exec();
}

void MainWindow::onManageTemplatesClicked() {
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 || repos_.tools == nullptr) {
        QMessageBox::information(this, tr("Sin pieza"),
                                 tr("Selecciona una pieza para gestionar sus plantillas."));
        return;
    }
    // No perder los cambios en vivo si el gestor cambia la plantilla activa.
    if (!confirmSaveBeforeLeaving()) {
        return;
    }
    TemplateManagerDialog dialog(repos_.tools, pieceId,
                                 QString::fromStdString(activeTemplate()), this);
    keepDialogSize(dialog, repos_.settings, "templates", 360, 380);
    dialog.exec();
    // Recargar el combo (pudo haber renombrados/borrados/duplicados) y activar
    // la plantilla elegida; las herramientas se recargan para la activa.
    const QString target = dialog.selectedTemplate();
    loadTemplateList(target);  // usa su propio QSignalBlocker
    if (!target.isEmpty() && templateCombo_->findText(target) < 0) {
        // Plantilla nueva y aún vacía: se añade al combo (se materializa al
        // guardar su primera herramienta), como el botón "+".
        QSignalBlocker blocker(templateCombo_);
        templateCombo_->addItem(target);
        templateCombo_->setCurrentText(target);
    }
    loadToolsForSelectedPiece();
}

void MainWindow::onNewTemplateClicked() {
    if (selectedPieceId() < 0) {
        QMessageBox::information(this, tr("Sin pieza"),
                                 tr("Selecciona o registra una pieza primero."));
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Nueva plantilla"),
                              tr("Nombre de la plantilla:"), QLineEdit::Normal,
                              tr("plantilla %1").arg(templateCombo_->count() + 1), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (templateCombo_->findText(name) >= 0) {
        QMessageBox::warning(this, tr("Ya existe"),
                             tr("Ya hay una plantilla con ese nombre."));
        return;
    }
    // La plantilla nace vacía; se materializa al guardar su primera herramienta.
    templateCombo_->addItem(name);
    templateCombo_->setCurrentText(name);  // dispara onTemplateChanged
    statusBar()->showMessage(
        tr("Plantilla '%1' activa: dibuja herramientas y se guardarán en ella.").arg(name));
}

void MainWindow::onSaveTemplateClicked() {
    saveTemplate(selectedPieceId());
}

bool MainWindow::saveTemplate(std::int64_t pieceId) {
    if (repos_.tools == nullptr) {
        statusBar()->showMessage(tr("Base de datos no disponible: no se puede guardar."));
        return false;
    }
    if (liveTools_.empty()) {
        templateDirty_ = false;  // nada que guardar: el estado queda limpio
        statusBar()->showMessage(tr("No hay herramientas dibujadas que guardar."));
        return true;
    }

    if (pieceId < 0) {
        // Sin pieza: crear una y guardar ahí (opción elegida por el usuario).
        if (repos_.pieces == nullptr) {
            statusBar()->showMessage(
                tr("No hay pieza seleccionada ni base de datos de piezas."));
            return false;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Nueva pieza"),
            tr("No hay pieza seleccionada. Nombre de la pieza nueva:"),
            QLineEdit::Normal, tr("pieza"), &ok);
        if (!ok || name.trimmed().isEmpty()) {
            return false;
        }
        auto created = repos_.pieces->createPiece(name.trimmed().toStdString());
        if (!created.isOk()) {
            QMessageBox::warning(this, tr("No se pudo crear la pieza"),
                                 QString::fromStdString(created.error().message));
            return false;
        }
        pieceId = created.value();
        seedMeasurementForNewPiece(pieceId);
        // Bloquear señales: si el combo dispara onPieceSelectionChanged,
        // loadToolsForSelectedPiece limpiaría liveTools_ ANTES del upsert.
        QSignalBlocker blocker(pieceCombo_);
        loadPieceList(pieceId);
    }

    persistTemplateTools(pieceId);
    return true;
}

void MainWindow::persistTemplateTools(std::int64_t pieceId) {
    // Upsert de todas las herramientas en vivo a la plantilla activa: inserta
    // las nuevas (id < 0) y actualiza las cambiadas. Los borrados en vivo ya se
    // persistieron al instante (deleteToolAt), así que esto cierra el ciclo.
    const std::string tmpl = activeTemplate();
    int saved = 0;
    int errors = 0;
    for (auto& tool : liveTools_) {
        tool.config.geometryJson = inspection::toJson(tool.geometry);
        if (auto result = repos_.tools->save(pieceId, tool.config, tmpl); result.isOk()) {
            tool.config.id = result.value();
            ++saved;
        } else {
            ++errors;
            core::logError(result.error().message);
        }
    }
    stableTools_ = liveTools_;  // el estado guardado pasa a ser el "limpio"
    templateDirty_ = false;
    loadedPieceId_ = pieceId;
    loadedTemplate_ = QString::fromStdString(tmpl);
    statusBar()->showMessage(
        errors == 0
            ? tr("Plantilla '%1' guardada (%2 herramienta(s)).")
                  .arg(QString::fromStdString(tmpl))
                  .arg(saved)
            : tr("Plantilla '%1' guardada con %2 error(es); %3 ok (ver log).")
                  .arg(QString::fromStdString(tmpl))
                  .arg(errors)
                  .arg(saved));
}

// Muestra el aviso Guardar/Descartar/Cancelar si hay cambios sin guardar.
// Devuelve true si se puede continuar (guardado o descartado), false si el
// operador cancela (o cancela la creación de pieza al guardar).
bool MainWindow::confirmSaveBeforeLeaving() {
    if (!templateDirty_) {
        return true;
    }
    QMessageBox box(QMessageBox::Question, tr("Cambios sin guardar"),
                    tr("La plantilla tiene herramientas con cambios sin guardar.\n\n"
                       "¿Qué quieres hacer?"),
                    QMessageBox::NoButton, this);
    auto* saveBtn = box.addButton(tr("Guardar"), QMessageBox::AcceptRole);
    auto* discardBtn = box.addButton(tr("Descartar"), QMessageBox::DestructiveRole);
    auto* cancelBtn = box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == cancelBtn) {
        return false;
    }
    if (box.clickedButton() == saveBtn) {
        // Guardar en la pieza a la que pertenecen las herramientas en vivo.
        if (!saveTemplate(loadedPieceId_)) {
            return false;  // el usuario canceló la creación de pieza
        }
    }
    (void)discardBtn;
    templateDirty_ = false;  // guardado o descartado: estado limpio
    return true;
}

void MainWindow::selectPieceById(std::int64_t pieceId) {
    QSignalBlocker blocker(pieceCombo_);
    for (int i = 0; i < pieceCombo_->count(); ++i) {
        if (pieceCombo_->itemData(i).toLongLong() == pieceId) {
            pieceCombo_->setCurrentIndex(i);
            return;
        }
    }
}

void MainWindow::onPieceSelectionChanged(int index) {
    Q_UNUSED(index);
    // Si hay cambios sin guardar, preguntar antes de abandonar la plantilla.
    // Al cancelar, restaurar el combo a la pieza cuyas herramientas están vivas.
    if (!confirmSaveBeforeLeaving()) {
        selectPieceById(loadedPieceId_);
        return;
    }
    autoInspectButton_->setChecked(false);
    video_->resetView();  // otra pieza, encuadre limpio (Z3)
    loadMeasurementForSelectedPiece();  // modo y tablero de ESTA pieza (M2)
    loadDetectionProfileForSelectedPiece();  // perfil de detección de la pieza (O3)
    // La referencia que se actualizaría es la de la pieza elegida.
    updateLearnFromCaptureAvailability();
    loadTemplateList();       // repuebla plantillas de la pieza
    loadToolsForSelectedPiece();

    // Rasgo distintivo y ajuste de orientación de la pieza seleccionada.
    currentAnchor_.reset();
    currentOrientationOffset_ = 0.0;
    video_->setAnchorMarker(false);
    if (const std::int64_t pieceId = selectedPieceId();
        pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto anchor = repos_.pieces->loadAnchor(pieceId);
            anchor.isOk() && anchor.value().has_value()) {
            currentAnchor_ = anchor.value();
            video_->setAnchorMarker(true, currentAnchor_->piecePoint);
        }
        if (auto offset = repos_.pieces->loadOrientationOffset(pieceId); offset.isOk()) {
            currentOrientationOffset_ = offset.value();
        }
    }

    // Miniatura de la pieza registrada para el panel de comparación.
    referenceThumb_ = QImage();
    refThumbLabel_->setPixmap(QPixmap());
    refThumbLabel_->setText(QStringLiteral("—"));
    similarityLabel_->clear();
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto thumb = repos_.pieces->loadThumbnail(pieceId);
            thumb.isOk() && !thumb.value().empty()) {
            referenceThumb_ = QImage::fromData(thumb.value().data(),
                                               static_cast<int>(thumb.value().size()));
            if (!referenceThumb_.isNull()) {
                refThumbLabel_->setPixmap(QPixmap::fromImage(referenceThumb_)
                                              .scaled(refThumbLabel_->size(),
                                                      Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
            }
        } else {
            refThumbLabel_->setText(tr("Sin miniatura\n(regístrala de nuevo\npara generarla)"));
        }
    }
}

void MainWindow::loadToolsForSelectedPiece() {
    liveTools_.clear();
    toolsOfOtherPieces_.clear();
    video_->setSelectedIndex(-1);
    video_->clearResults();

    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 || repos_.tools == nullptr) {
        templateDirty_ = false;
        loadedPieceId_ = pieceId;
        loadedTemplate_ = QString::fromStdString(activeTemplate());
        video_->update();
        return;
    }
    auto listed = repos_.tools->listForPiece(pieceId, activeTemplate());
    if (!listed.isOk()) {
        core::logWarning("No se pudieron cargar las herramientas: " + listed.error().message);
        return;
    }
    for (auto& config : listed.value()) {
        auto geometry = inspection::geometryFromJson(config.type, config.geometryJson);
        if (!geometry.isOk()) {
            core::logWarning("Herramienta '" + config.name +
                             "' con geometría corrupta: " + geometry.error().message);
            continue;
        }
        inspection::EditedTool tool;
        tool.config = std::move(config);
        tool.geometry = std::move(geometry.value());
        liveTools_.push_back(std::move(tool));
    }
    // Cambiar de pieza reinicia el historial de deshacer.
    undoStack_.clear();
    stableTools_ = liveTools_;
    // Estado recién cargado de la BD = limpio; recordar a quién pertenece.
    templateDirty_ = false;
    loadedPieceId_ = pieceId;
    loadedTemplate_ = QString::fromStdString(activeTemplate());
    video_->update();
}

// --- Registro en vivo -------------------------------------------------------

void MainWindow::onRegisterLiveClicked() {
    if (!streaming_ || lastFrame_.isNull()) {
        // Se nombran las tres puertas, no solo la cámara: desde que una imagen y
        // un vídeo son fuentes, «inicia la cámara» deja fuera dos caminos que
        // funcionan igual de bien y manda a buscar hardware a quien no lo tiene.
        QMessageBox::information(
            this, tr("Sin imagen"),
            tr("Elige una fuente y ponla en marcha: una cámara, una imagen o un vídeo. "
               "También puedes registrar desde imágenes sueltas con el asistente."));
        return;
    }
    if (repos_.pieces == nullptr) {
        QMessageBox::warning(this, tr("No disponible"),
                             tr("El registro necesita la base de datos."));
        return;
    }
    // Sin modelo ONNX se puede registrar igual (G1): la pieza queda como
    // medidor puro. Se avisa una vez de lo que se pierde, no se bloquea.
    if (!repos_.embedFn && !toolsOnlyAccepted_) {
        // Se pregunta UNA vez por sesión: si el operador ya dijo que sí, repetir
        // el diálogo en cada registro solo estorba.
        if (QMessageBox::question(
                this, tr("Sin modelo de apariencia"),
                tr("El modelo ONNX no está disponible, así que las piezas se "
                   "registrarán solo con herramientas: se medirán con las que dibujes, "
                   "pero no habrá comparación de apariencia que detecte defectos "
                   "inesperados.\n\n¿Registrar así durante esta sesión?")) !=
            QMessageBox::Yes) {
            return;
        }
        toolsOnlyAccepted_ = true;
    }

    // Pedir el nombre validando duplicados ANTES de capturar nada: si ya
    // existe se ofrece guardar como nueva versión de esa pieza o renombrar.
    pendingPieceId_ = -1;
    QString name;
    while (true) {
        name = QInputDialog::getText(this, tr("Registrar pieza"), tr("Nombre de la pieza:"),
                                     QLineEdit::Normal, name)
                   .trimmed();
        if (name.isEmpty()) {
            return;
        }
        const auto exists = repos_.pieces->nameExists(name.toStdString());
        if (!exists.isOk()) {
            QMessageBox::warning(this, tr("Error"),
                                 QString::fromStdString(exists.error().message));
            return;
        }
        if (!exists.value()) {
            break;  // nombre libre
        }

        QMessageBox question(QMessageBox::Question, tr("La pieza ya existe"),
                             tr("Ya existe una pieza llamada '%1'.\n\n¿Qué quieres hacer?")
                                 .arg(name),
                             QMessageBox::NoButton, this);
        auto* newVersion =
            question.addButton(tr("Guardar como nueva versión"), QMessageBox::AcceptRole);
        question.addButton(tr("Elegir otro nombre"), QMessageBox::ActionRole);
        auto* cancel = question.addButton(QMessageBox::Cancel);
        question.exec();
        if (question.clickedButton() == cancel) {
            return;
        }
        if (question.clickedButton() == newVersion) {
            if (auto pieces = repos_.pieces->listPieces(); pieces.isOk()) {
                for (const auto& piece : pieces.value()) {
                    if (piece.name == name.toStdString()) {
                        pendingPieceId_ = piece.id;
                        break;
                    }
                }
            }
            break;
        }
        // "Elegir otro nombre": vuelve a preguntar conservando el texto.
    }
    pendingPieceName_ = name;

    // Modo de medición de la pieza nueva (M2): se pregunta aquí, junto al
    // nombre, y se aplica ya mismo para que el operador capture viendo el
    // tablero con el que se va a medir. Cancelar aquí cancela el registro.
    {
        repositories::PieceMeasurement current;
        current.mode = measurementMode_;
        current.board = boardConfig_;
        current.maxOffsetPx = maxOffsetPx_;
        current.maxAngleDeg = maxAngleDeg_;
        MeasurementModeDialog modeDialog(current, name, this);
        if (modeDialog.exec() != QDialog::Accepted) {
            return;
        }
        pendingMeasurement_ = modeDialog.measurement();
        applyMeasurement(pendingMeasurement_);
    }

    // El rasgo distintivo marcado (si hay) fija la orientación de las 30
    // capturas de referencia y se guarda con la pieza.
    liveSession_ = std::make_shared<engine::RegistrationSession>(
        repos_.embedFn, kCaptureTarget, kCaptureMinimum, currentAnchor_, inspectionConfig(),
        currentOrientationOffset_);
    captureProgress_ = new QProgressDialog(
        tr("Capturando referencias de '%1'…\nMantén la pieza a la vista.")
            .arg(pendingPieceName_),
        tr("Cancelar"), 0, kCaptureTarget, this);
    captureProgress_->setWindowModality(Qt::WindowModal);
    captureProgress_->setMinimumDuration(0);
    captureProgress_->setValue(0);
    connect(captureProgress_, &QProgressDialog::canceled, this,
            &MainWindow::onCaptureCanceled);

    captureTimer_.start();
}

void MainWindow::onCaptureTick() {
    if (captureWatcher_.isRunning() || lastFrame_.isNull() || liveSession_ == nullptr) {
        return;
    }
    // shared_ptr capturado: la sesión sobrevive aunque el usuario cancele
    // mientras un frame sigue procesándose en el pool.
    auto session = liveSession_;
    const QImage frame = lastFrame_;
    captureWatcher_.setFuture(QtConcurrent::run([session, frame] {
        using ResultT = core::Result<engine::RegistrationSession::SampleFeedback>;
        try {
            return session->addFrame(camera::qImageToMat(frame));
        } catch (const std::exception& e) {
            return ResultT::err(std::string("Error interno de captura: ") + e.what());
        } catch (...) {
            return ResultT::err("Error interno de captura");
        }
    }));
}

void MainWindow::onCaptureProcessed() {
    if (liveSession_ == nullptr || captureProgress_ == nullptr) {
        return;  // registro cancelado mientras se procesaba un frame
    }
    const auto result = captureWatcher_.result();
    if (!result.isOk()) {
        stopLiveCapture();
        QMessageBox::warning(this, tr("Registro fallido"),
                             QString::fromStdString(result.error().message));
        return;
    }

    const auto& feedback = result.value();
    captureProgress_->setValue(feedback.count);
    if (!feedback.accepted) {
        captureProgress_->setLabelText(
            tr("Capturando referencias de '%1'…\nRechazada: %2")
                .arg(pendingPieceName_, QString::fromStdString(feedback.reason)));
    } else {
        captureProgress_->setLabelText(tr("Capturando referencias de '%1'…\n%2 de %3")
                                           .arg(pendingPieceName_)
                                           .arg(feedback.count)
                                           .arg(kCaptureTarget));
    }

    if (feedback.count >= kCaptureTarget) {
        finishLiveRegistration();
    }
}

void MainWindow::onCaptureCanceled() {
    stopLiveCapture();
    statusBar()->showMessage(tr("Registro cancelado."));
}

void MainWindow::stopLiveCapture() {
    captureTimer_.stop();
    liveSession_.reset();
    if (captureProgress_ != nullptr) {
        captureProgress_->deleteLater();
        captureProgress_ = nullptr;
    }
}

void MainWindow::finishLiveRegistration() {
    captureTimer_.stop();
    auto session = liveSession_;

    auto reference = session->finish();
    if (!reference.isOk()) {
        stopLiveCapture();
        QMessageBox::warning(this, tr("Registro incompleto"),
                             QString::fromStdString(reference.error().message));
        return;
    }

    // Pieza nueva, o nueva versión de una existente (elegido al pedir nombre).
    std::int64_t pieceId = pendingPieceId_;
    if (pieceId < 0) {
        auto created = repos_.pieces->createPiece(pendingPieceName_.toStdString());
        if (!created.isOk()) {
            stopLiveCapture();
            QMessageBox::warning(this, tr("No se pudo crear la pieza"),
                                 QString::fromStdString(created.error().message));
            return;
        }
        pieceId = created.value();
        seedMeasurementForNewPiece(pieceId);
    }

    // Modo "solo herramientas" (G1): la referencia viene vacía a propósito y NO
    // se guarda. Guardarla haría creer a la inspección que hay apariencia con
    // la que comparar, y daría NG sistemático.
    const bool toolsOnly = reference.value().mean.empty();
    int referenceVersion = 0;
    if (!toolsOnly) {
        const auto savedVersion = repos_.pieces->saveReference(pieceId, reference.value());
        if (!savedVersion.isOk()) {
            stopLiveCapture();
            QMessageBox::warning(this, tr("No se pudo guardar la referencia"),
                                 QString::fromStdString(savedVersion.error().message));
            return;
        }
        referenceVersion = savedVersion.value();
    }

    // Miniatura del recorte normalizado: alimenta el panel "Pieza registrada".
    const auto thumbnail = engine::encodeThumbnailJpeg(session->firstNormalized(), 256);
    if (!thumbnail.empty()) {
        if (auto saved = repos_.pieces->saveThumbnail(pieceId, thumbnail); !saved.isOk()) {
            core::logWarning("No se pudo guardar la miniatura: " + saved.error().message);
        }
    }

    // Modo de medición y tablero elegidos al empezar el registro (M2).
    if (auto saved = repos_.pieces->saveMeasurement(pieceId, pendingMeasurement_);
        !saved.isOk()) {
        core::logWarning("No se pudo guardar el modo de medición: " + saved.error().message);
    }

    // Rasgo distintivo elegido durante la sesión.
    if (currentAnchor_.has_value()) {
        if (auto saved = repos_.pieces->saveAnchor(pieceId, *currentAnchor_);
            !saved.isOk()) {
            core::logWarning("No se pudo guardar el rasgo distintivo: " +
                             saved.error().message);
        }
    }

    // Persistir las herramientas dibujadas sobre el video (en la plantilla
    // activa: una pieza puede tener varias plantillas).
    const std::string tmpl = activeTemplate();
    int toolErrors = 0;
    if (repos_.tools != nullptr) {
        for (auto& tool : liveTools_) {
            tool.config.geometryJson = inspection::toJson(tool.geometry);
            if (auto saved = repos_.tools->save(pieceId, tool.config, tmpl); saved.isOk()) {
                tool.config.id = saved.value();
            } else {
                ++toolErrors;
                core::logError(saved.error().message);
            }
        }
    }
    // Registro guardó las herramientas: el estado queda limpio (P2).
    stableTools_ = liveTools_;
    templateDirty_ = false;
    loadedPieceId_ = pieceId;
    loadedTemplate_ = QString::fromStdString(tmpl);

    stopLiveCapture();
    // Seleccionar la pieza sin recargar las herramientas recién guardadas,
    // pero sí refrescar la miniatura de referencia del panel.
    {
        QSignalBlocker blocker(pieceCombo_);
        loadPieceList(pieceId);
    }
    referenceThumb_ = QImage::fromData(thumbnail.data(), static_cast<int>(thumbnail.size()));
    if (!referenceThumb_.isNull()) {
        refThumbLabel_->setPixmap(QPixmap::fromImage(referenceThumb_)
                                      .scaled(refThumbLabel_->size(), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
    }

    if (toolErrors > 0) {
        statusBar()->showMessage(
            tr("'%1' registrada, pero %2 herramienta(s) no se guardaron (ver log).")
                .arg(pendingPieceName_)
                .arg(toolErrors));
    } else if (toolsOnly) {
        statusBar()->showMessage(
            tr("'%1' registrada solo con herramientas (%2): sin comparación de "
               "apariencia. Auto-inspección activa.")
                .arg(pendingPieceName_)
                .arg(liveTools_.size()));
    } else {
        statusBar()->showMessage(tr("'%1' registrada (referencia v%2) con %3 "
                                    "herramienta(s). Auto-inspección activa.")
                                     .arg(pendingPieceName_)
                                     .arg(referenceVersion)
                                     .arg(liveTools_.size()));
    }

    autoInspectButton_->setChecked(true);
}

// --- Flujos con diálogo -------------------------------------------------------

// Último frame de la cámara o imagen elegida por el usuario (los flujos
// completos deben poder probarse en equipos sin cámara).
QImage MainWindow::frameOrFile() {
    if (!lastFrame_.isNull()) {
        return lastFrame_;
    }
    return openImageFile();
}

QImage MainWindow::openImageFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Elegir imagen"), QString(), tr("Imágenes (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty()) {
        return {};
    }
    QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Imagen inválida"), tr("No se pudo cargar la imagen."));
        return {};
    }
    return image.convertToFormat(QImage::Format_BGR888);
}

void MainWindow::loadPieceList(std::int64_t selectId) {
    pieceCombo_->clear();
    if (repos_.pieces == nullptr) {
        pieceCombo_->addItem(tr("BD no disponible"));
        pieceCombo_->setEnabled(false);
        return;
    }
    auto pieces = repos_.pieces->listPieces();
    if (!pieces.isOk()) {
        core::logWarning("No se pudieron listar las piezas: " + pieces.error().message);
        return;
    }
    for (const auto& piece : pieces.value()) {
        pieceCombo_->addItem(QString::fromStdString(piece.name),
                             QVariant::fromValue<qlonglong>(piece.id));
        if (piece.id == selectId) {
            pieceCombo_->setCurrentIndex(pieceCombo_->count() - 1);
        }
    }
    if (pieceCombo_->count() == 0) {
        pieceCombo_->addItem(tr("(sin piezas registradas)"));
    }
}

std::int64_t MainWindow::selectedPieceId() const {
    const QVariant data = pieceCombo_->currentData();
    return data.isValid() ? data.toLongLong() : -1;
}

void MainWindow::onRegisterWizardClicked() {
    if (repos_.pieces == nullptr) {
        QMessageBox::warning(this, tr("BD no disponible"),
                             tr("No se puede registrar sin base de datos."));
        return;
    }
    if (!repos_.embedFn) {
        QMessageBox::warning(
            this, tr("Modelo no disponible"),
            tr("El registro necesita el modelo de embeddings. Ejecuta run.ps1 para "
               "descargarlo y prepararlo."));
        return;
    }

    // Con la detección configurada: el asistente SEGMENTA cada captura para sacar
    // el recorte del que nace el embedding, y sin esto aprendía la pieza con los
    // valores de fábrica mientras «Registrar y activar» —que hace lo mismo— usaba
    // los del operador.
    RegistrationWizard wizard(&controller_, repos_.embedFn, repos_.pieces,
                              inspectionConfig(), this);
    keepDialogSize(wizard, repos_.settings, "registration", 900, 640);
    if (wizard.exec() == QDialog::Accepted) {
        loadPieceList(wizard.createdPieceId());
    }
}

// Registrar un acabado admisible mas de la pieza que ya esta seleccionada.
void MainWindow::onRegisterVariantClicked() {
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0) {
        QMessageBox::information(
            this, tr("Ninguna pieza seleccionada"),
            tr("Elige primero la pieza a la que quieres añadirle un acabado."));
        return;
    }
    if (!repos_.embedFn) {
        QMessageBox::warning(
            this, tr("Modelo no disponible"),
            tr("Registrar un acabado necesita el modelo de embeddings. Ejecuta run.ps1 "
               "para descargarlo y prepararlo."));
        return;
    }
    const QString pieceName = pieceCombo_ != nullptr ? pieceCombo_->currentText() : QString();

    RegistrationWizard wizard(&controller_, repos_.embedFn, repos_.pieces, pieceId,
                              pieceName, inspectionConfig(), this);
    keepDialogSize(wizard, repos_.settings, "registration", 900, 640);
    if (wizard.exec() != QDialog::Accepted) {
        return;
    }
    // Se dice CUANTOS acabados tiene ahora la pieza. «Guardado» a secas no deja
    // comprobar que se guardo donde uno creia, y aqui el error tipico —haberlo
    // guardado encima del anterior— es invisible sin este numero.
    if (auto variants = repos_.pieces->listVariants(pieceId); variants.isOk()) {
        QStringList names;
        for (const auto& name : variants.value()) {
            names << QString::fromStdString(name);
        }
        statusBar()->showMessage(
            tr("«%1» tiene ahora %2 acabados registrados: %3.")
                .arg(pieceName)
                .arg(names.size())
                .arg(names.join(QStringLiteral(", "))));
    }
    reanalyseCurrentFrame();
}

void MainWindow::onOpenEditorClicked() {
    // E2: con vídeo en marcha, elegir explícitamente la fuente de la imagen.
    const bool live = streaming_ && !lastFrame_.isNull();
    QImage reference;
    if (live) {
        QMessageBox box(QMessageBox::Question, tr("Editor de plantilla"),
                        tr("¿Sobre qué imagen quieres editar la plantilla?"),
                        QMessageBox::NoButton, this);
        // El botón nombra la fuente que hay de verdad. «Frame actual de la
        // cámara» era cierto cuando la cámara era lo único que había; con una
        // foto congelada, una imagen o un vídeo abierto, le está diciendo al
        // operador que va a usar algo distinto de lo que ve.
        auto* current = box.addButton(currentSourceLabel(), QMessageBox::AcceptRole);
        auto* fromFile = box.addButton(tr("Abrir archivo…"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == current) {
            reference = lastFrame_;
        } else if (box.clickedButton() == fromFile) {
            reference = openImageFile();
        } else {
            return;
        }
    } else {
        reference = frameOrFile();
    }
    if (reference.isNull()) {
        return;
    }

    // CON LA CONFIGURACIÓN QUE EL OPERADOR AJUSTÓ, no con la de fábrica.
    //
    // Esta llamada no llevaba ninguna, así que el fixture con el que se abre el
    // editor salía de una detección distinta de la que se está viendo en la
    // ventana: sin recuperación de brillos, sin separar piezas que se tocan, sin
    // clave de color de fondo y con el umbral y la polaridad de fábrica.
    //
    // Lo peor no es que el fixture saliera desplazado. Es que si esa detección de
    // fábrica FALLA —y sobre una mesa de color falla: el gris del cartón rojo cae
    // en 116 y Otsu devuelve una sola mancha del 89 % del cuadro— el editor se
    // negaba a abrir con «no se pudo analizar la imagen», mientras la ventana
    // principal enseñaba la pieza perfectamente detectada al lado.
    //
    // El editor sí recibía la configuración buena y volvía a analizar con ella;
    // el que se quedaba fuera era este primer análisis, el que decide si se abre.
    //
    // Y sobre LA PIEZA SEÑALADA: el editor se abre con este fixture, así que
    // abrirlo siempre sobre la mayor dejaba al operador dibujando cotas encima
    // de una pieza distinta de la que acababa de elegir con las flechas.
    const auto analysis = analyseMeasuredPiece(camera::qImageToMat(reference));
    if (!analysis.isOk()) {
        QMessageBox::warning(this, tr("Sin pieza detectada"),
                             tr("No se pudo analizar la imagen: %1")
                                 .arg(QString::fromStdString(analysis.error().message)));
        return;
    }

    // Con pieza seleccionada se edita su plantilla; sin piezas, una "demo".
    std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 && repos_.pieces != nullptr) {
        if (auto created = repos_.pieces->createPiece("demo"); created.isOk()) {
            pieceId = created.value();
            // Señales bloqueadas: onPieceSelectionChanged recargaría de la BD y
            // vaciaría liveTools_ antes de pasarlas al editor.
            QSignalBlocker blocker(pieceCombo_);
            loadPieceList(pieceId);
        } else if (auto pieces = repos_.pieces->listPieces(); pieces.isOk()) {
            for (const auto& piece : pieces.value()) {
                if (piece.name == "demo") {
                    pieceId = piece.id;
                    break;
                }
            }
        }
    }

    // Pasar las herramientas EN VIVO actuales (incluidas las no guardadas) para
    // que el editor muestre lo mismo que la vista en vivo (P3), y la cámara en
    // marcha para el botón "Actualizar desde cámara" (E1) — solo con vídeo.
    inspection::EditorWindow editor(reference, analysis.value().fixture, pieceId,
                                    pieceId >= 0 ? repos_.tools : nullptr, calibration_,
                                    activeTemplate(), this, &liveTools_,
                                    live ? &controller_ : nullptr, inspectionConfig());
    // Para que la receta de medición se recuerde en la pieza, y para que las
    // recetas propias del operador salgan en la lista.
    editor.setPieceRepository(repos_.pieces);
    editor.setRecipeRepository(repos_.measureRecipes);
    editor.exec();

    // Devolver las herramientas editadas a la vista en vivo (ida y vuelta), en
    // vez de recargar de la BD y perder lo no guardado.
    liveTools_ = editor.editedTools();
    undoStack_.clear();
    stableTools_ = liveTools_;
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
    video_->update();
    if (editor.savedToDb()) {
        // El editor persistió: estado limpio y ligado a esta pieza/plantilla.
        templateDirty_ = false;
        loadedPieceId_ = pieceId;
        loadedTemplate_ = QString::fromStdString(activeTemplate());
    } else {
        // Hubo ediciones no guardadas: quedan como cambios pendientes (Ctrl+S).
        templateDirty_ = true;
    }
}

}  // namespace pci::ui
