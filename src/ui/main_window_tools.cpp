#include "ui/main_window.h"
#include "ui/main_window_internal.h"

#include "camera/frame_utils.h"
#include "core/logging.h"
#include "inspection_editor/reference_advice.h"
#include "repositories/piece_repository.h"
#include "repositories/settings_repository.h"
#include "repositories/tool_repository.h"
#include "ui/configure_dialog.h"
#include "ui/delete_scope.h"
#include "ui/measurement_mode_dialog.h"
#include "ui/piece_mosaic.h"
#include "ui/pieces_page.h"
#include "ui/theme.h"
#include "vision/position_fixture.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDockWidget>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolButton>

#include <algorithm>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace pci::ui {

namespace {

// El nombre corto sale de `inspection::toolTypeLabel` (lista única compartida
// con el editor); aquí solo se envuelve en QString.
QString typeLabel(inspection::ToolType type) {
    return QString::fromUtf8(inspection::toolTypeLabel(type));
}

double wrapAngleDeg(double angle) {
    while (angle >= 180.0) {
        angle -= 360.0;
    }
    while (angle < -180.0) {
        angle += 360.0;
    }
    return angle;
}

}  // namespace

// --- Deshacer / rehacer sobre las herramientas dibujadas ---

void MainWindow::commitUndoState() {
    undoStack_.push(stableTools_);
    stableTools_ = liveTools_;
    templateDirty_ = true;  // toda mutación de herramientas deja la plantilla sucia
}

void MainWindow::restoreTools(std::vector<inspection::EditedTool> tools) {
    liveTools_ = std::move(tools);
    stableTools_ = liveTools_;
    templateDirty_ = true;  // deshacer/rehacer también cambia el estado guardado
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
    video_->update();
}

void MainWindow::onUndo() {
    // Con el pincel en la mano, deshacer es deshacer la pincelada. Es lo que
    // espera quien lo está usando, y evita tener dos atajos que hay que acertar.
    if (video_ != nullptr && video_->edgeBrush() != inspection::EditorCanvas::EdgeBrush::Off &&
        video_->undoEdgeCorrection()) {
        statusBar()->showMessage(tr("Pincelada deshecha."));
        return;
    }
    if (auto previous = undoStack_.undo(liveTools_)) {
        restoreTools(std::move(*previous));
        statusBar()->showMessage(tr("Deshecho."));
    }
}

void MainWindow::onRedo() {
    if (video_ != nullptr && video_->edgeBrush() != inspection::EditorCanvas::EdgeBrush::Off &&
        video_->redoEdgeCorrection()) {
        statusBar()->showMessage(tr("Pincelada rehecha."));
        return;
    }
    if (auto next = undoStack_.redo(liveTools_)) {
        restoreTools(std::move(*next));
        statusBar()->showMessage(tr("Rehecho."));
    }
}

// --- Herramientas dibujadas sobre el video ---------------------------------

void MainWindow::onToolModeChanged(std::optional<inspection::ToolType> chosen) {
    if (!chosen.has_value()) {
        video_->setCreateType(std::nullopt);
        statusBar()->showMessage(tr("Modo mover: clic para seleccionar, arrastra para mover."));
        return;
    }
    const auto type = *chosen;
    video_->setCreateType(type);
    // Elegir herramienta exige el fixture: reactiva el análisis si estaba
    // pausado por tener el contorno oculto y la escena vacía.
    reanalyseCurrentFrame();
    // La primera línea de la descripción como guía inmediata.
    const QString description = QString::fromUtf8(inspection::toolTypeDescription(type));
    statusBar()->showMessage(description.section(QLatin1Char('\n'), 0, 1));
}

void MainWindow::onLiveToolCreated(const inspection::ToolGeometry& geometry) {
    inspection::EditedTool tool;
    tool.geometry = geometry;
    tool.config.type = inspection::typeOf(geometry);
    ++toolNameCounter_;
    tool.config.name = (typeLabel(tool.config.type) +
                        QStringLiteral(" %1").arg(toolNameCounter_))
                           .toStdString();
    tool.config.geometryJson = inspection::toJson(geometry);
    tool.config.toleranceMin = 0.0;
    tool.config.toleranceMax = 100000.0;

    // Medir la pieza actual de inmediato y sugerir tolerancias alrededor de
    // ese valor: la pieza buena define su propio rango de aceptación.
    QString hint;
    if (liveFixture_.has_value() && !lastFrame_.isNull()) {
        // Mismo tablero que está dibujado: si la herramienta es de Posición, la
        // tolerancia sugerida se calcula respecto al cero que el operador ve.
        const vision::BoardFrame board = video_->boardFrame();
        const auto result =
            inspection::runTool(camera::qImageToMat(lastFrame_), *liveFixture_, tool.config,
                                calibration_.mmPerPixel, currentUnit(), cv::Mat(), &board);
        if (result.isOk() && !result.value().detail.empty() &&
            (result.value().ok || result.value().measured > 0.0)) {
            inspection::suggestTolerances(tool.config.type, result.value().measured,
                                          tool.config.toleranceMin,
                                          tool.config.toleranceMax);
            // La unidad la decide `formatMeasure`, no esta pantalla.
            const QString measure = QString::fromStdString(inspection::formatMeasure(
                result.value(), calibration_.mmPerPixel, currentUnit()));
            hint = tr("%1: midió %2; tolerancias sugeridas [%3, %4]")
                       .arg(QString::fromStdString(tool.config.name), measure)
                       .arg(tool.config.toleranceMin, 0, 'f', 1)
                       .arg(tool.config.toleranceMax, 0, 'f', 1);
        } else {
            hint = tr("%1 creada, pero no midió en este frame (%2); ajusta su posición")
                       .arg(QString::fromStdString(tool.config.name),
                            QString::fromStdString(result.isOk() ? result.value().detail
                                                                 : result.error().message));
        }
    }
    // LA REFERENCIA QUE LE FALTA, DICHA AL DIBUJAR.
    //
    // Cinco herramientas no miden nada sin una referencia declarada, y hasta
    // ahora lo decían al MEDIR: el operador la dibujaba, seguía trabajando, y
    // se enteraba con el veredicto. Aquí las candidatas salen de la última
    // medición en vivo —lo que las demás producen de verdad— y si sólo hay una,
    // se pone sola.
    const auto needs = inspection::referenceOperandsOf(tool.geometry);
    if (needs[0] != inspection::OperandKind::Unused ||
        needs[1] != inspection::OperandKind::Unused) {
        const auto advice =
            inspection::adviseReference(tool.config, tool.geometry, lastToolResults_);
        if (!advice.first.empty()) {
            tool.config.reference = advice.first;
        }
        if (!advice.second.empty()) {
            tool.config.reference2 = advice.second;
        }
        hint = QString::fromStdString(advice.why);
    }
    liveTools_.push_back(std::move(tool));
    commitUndoState();

    video_->clearResults();
    const int newIndex = static_cast<int>(liveTools_.size()) - 1;
    video_->setSelectedIndex(newIndex);
    onLiveSelectionChanged(newIndex);  // sincroniza el spin de "Puntos"
    statusBar()->showMessage(hint.isEmpty()
                                 ? tr("%1 creada").arg(QString::fromStdString(
                                       liveTools_.back().config.name))
                                 : hint);
}

// Un movimiento terminó (arrastre en el canvas): estado nuevo, undoable.
void MainWindow::onLiveToolModified() {
    video_->clearResults();
    commitUndoState();
}

void MainWindow::onDeleteToolClicked() {
    auto indices = video_->selectedIndices();
    if (indices.empty()) {
        return;
    }
    // De mayor a menor para que los índices no se corran al borrar.
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    for (const int index : indices) {
        if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
            continue;
        }
        const auto& tool = liveTools_[static_cast<std::size_t>(index)];
        if (tool.config.id >= 0 && repos_.tools != nullptr) {
            // Si se deshace el borrado, el guardado reinsertará la fila.
            if (auto removed = repos_.tools->remove(tool.config.id); !removed.isOk()) {
                statusBar()->showMessage(QString::fromStdString(removed.error().message));
                continue;
            }
        }
        liveTools_.erase(liveTools_.begin() + index);
    }
    commitUndoState();
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
}

void MainWindow::onDeleteAllToolsClicked() {
    const int total = static_cast<int>(liveTools_.size());

    // SIN PIEZA ABIERTA TAMBIÉN HAY QUE PODER BORRAR.
    //
    // Queja de uso: «la herramienta de borrar todo no detecta nada o no me deja
    // usarla, hasta que selecciono una pieza». Era exacto, y con una ironía
    // dentro: este botón se iba EN SILENCIO cuando `liveTools_` estaba vacío, y
    // `liveTools_` solo se llena al seleccionar pieza. O sea que la salida
    // «borrar las de todas las piezas» —que se añadió justo para no tener que ir
    // pieza por pieza— vivía dentro de un diálogo que no se abría nunca si no
    // habías entrado en una.
    //
    // Ahora el recuento de TODO el programa se hace antes de decidir si hay algo
    // que hacer, y no después.
    repositories::ToolRepository::ToolTally everywhere;
    if (repos_.tools != nullptr) {
        if (auto tally = repos_.tools->tallyAll(); tally.isOk()) {
            everywhere = tally.value();
        }
    }

    const DeleteScope scope = decideDeleteScope(total, everywhere.tools);
    if (scope.nothingAnywhere) {
        // Y SI NO HAY NADA EN NINGUNA PARTE, SE DICE. Un botón que no hace nada
        // y no explica por qué se lee como un botón roto: el operador vuelve a
        // pulsarlo, y luego busca qué ha hecho mal.
        statusBar()->showMessage(
            tr("No hay ninguna herramienta que borrar, ni en esta pieza ni en las demás."));
        return;
    }

    // Se pregunta, y la pregunta DICE CUÁNTAS. «¿Seguro?» a secas no informa de
    // nada: quien lleva media hora dibujando necesita ver el número para
    // reconocer si es el trabajo que cree o el de otra pieza que abrió sin
    // darse cuenta. Es la misma regla que ya sigue el botón de insertar
    // propuestas, que dice cuántas va a añadir.
    //
    // Y se dice que hay vuelta atrás: el miedo a un botón destructivo viene de
    // no saber si se puede deshacer, y aquí se puede.
    // ¿HAY HERRAMIENTAS EN OTRAS PIEZAS?
    //
    // De esto salió una queja de uso: «el botón de borrar todas las herramientas
    // no debería de ocupar seleccionar las piezas de una en una». Y era verdad:
    // el botón borra las de la pieza ABIERTA, así que vaciar el trabajo entero
    // obligaba a ir al combo, cambiar de pieza, confirmar, y repetir.
    //
    // No se añade un botón nuevo. Se añade una SEGUNDA SALIDA al diálogo que ya
    // existe, y solo cuando de verdad hay algo en otras piezas: un botón para
    // borrarlo todo, visible siempre, sería un botón peligroso a la vista de
    // alguien que casi nunca lo necesita.
    // Hay trabajo fuera de esta pieza si el total del programa supera al de
    // aquí. Con la pieza sin abrir (`total` = 0) eso es cierto en cuanto haya
    // una sola herramienta guardada, que es justo el caso que antes se perdía.
    const bool othersHaveTools = scope.offerEverywhere;

    // EL TÍTULO DICE DE QUÉ SE HABLA. Sin pieza abierta no hay «esta pieza», y
    // un diálogo que anuncia «se van a borrar las 0 herramientas de esta pieza»
    // parece un error del programa en vez de una pregunta.
    QMessageBox box(
        QMessageBox::Warning, tr("Borrar todas las herramientas"),
        total > 0
            ? tr("Se van a borrar las %n herramienta(s) de esta pieza.", nullptr, total)
            : tr("No hay ninguna pieza abierta, pero el programa guarda %n "
                 "herramienta(s).", nullptr, everywhere.tools),
        QMessageBox::NoButton, this);
    if (othersHaveTools) {
        // LA VERDAD SOBRE EL DESHACER, que es lo delicado de esta opción.
        //
        // Ctrl+Z guarda el estado de las herramientas de la pieza ABIERTA. Puede
        // devolver las de esa y no tiene forma de devolver las de las demás,
        // porque nunca las tuvo en memoria. Un «se puede deshacer» que solo
        // funciona a medias es peor que no prometer nada, así que las dos
        // salidas dicen exactamente lo que se puede recuperar de cada una.
        box.setInformativeText(
            total > 0
                ? tr("Borrar las de esta pieza se puede deshacer con Ctrl+Z.\n\n"
                     "En el programa hay %1 herramientas repartidas en %2 piezas. "
                     "Borrarlas todas de una vez no se puede deshacer.")
                      .arg(everywhere.tools)
                      .arg(everywhere.pieces)
                // Sin pieza abierta NO se menciona Ctrl+Z, y no es un olvido:
                // la pila de deshacer guarda las herramientas de la pieza
                // abierta, y aquí no hay ninguna. Prometer una vuelta atrás que
                // no existe es peor que avisar de que no la hay.
                : tr("Están repartidas en %1 piezas. Borrarlas no se puede "
                     "deshacer.")
                      .arg(everywhere.pieces));
    } else {
        box.setInformativeText(tr("Se puede deshacer con Ctrl+Z."));
    }
    // La salida «esta pieza» solo existe si hay una pieza con algo dentro.
    QPushButton* confirm = nullptr;
    if (scope.offerThisPiece) {
        confirm = box.addButton(tr("Borrar las %n de esta pieza", nullptr, total),
                                QMessageBox::DestructiveRole);
    }
    QPushButton* confirmAll = nullptr;
    if (othersHaveTools) {
        confirmAll = box.addButton(
            tr("Borrar las %1 de las %2 piezas").arg(everywhere.tools).arg(everywhere.pieces),
            QMessageBox::DestructiveRole);
    }
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);  // el defecto NUNCA es el destructivo
    box.exec();
    if (confirmAll != nullptr && box.clickedButton() == confirmAll) {
        auto removed = repos_.tools->removeAllTools();
        if (!removed.isOk()) {
            statusBar()->showMessage(QString::fromStdString(removed.error().message));
            return;
        }
        liveTools_.clear();
        // Se limpia también la pila de deshacer: dejarla con un estado anterior
        // haría que Ctrl+Z devolviera las herramientas de ESTA pieza y ninguna de
        // las demás, que es justo el medio deshacer que se acaba de prometer que
        // no habría.
        undoStack_.clear();
        commitUndoState();
        video_->setSelectedIndex(-1);
        onLiveSelectionChanged(-1);
        video_->clearResults();
        statusBar()->showMessage(
            tr("%1 herramientas borradas de %2 piezas. Esto no se puede deshacer.")
                .arg(removed.value())
                .arg(everywhere.pieces));
        return;
    }
    if (confirm == nullptr || box.clickedButton() != confirm) {
        return;
    }

    for (const auto& tool : liveTools_) {
        if (tool.config.id >= 0 && repos_.tools != nullptr) {
            if (auto removed = repos_.tools->remove(tool.config.id); !removed.isOk()) {
                statusBar()->showMessage(QString::fromStdString(removed.error().message));
            }
        }
    }
    liveTools_.clear();
    commitUndoState();
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
    statusBar()->showMessage(
        tr("%n herramienta(s) borrada(s). Ctrl+Z las devuelve.", nullptr, total));
}

// Marcar el rasgo distintivo: el siguiente clic sobre la pieza en el video
// define el punto; se guarda de inmediato si hay una pieza seleccionada.
void MainWindow::onAnchorButtonToggled(bool enabled) {
    if (!enabled) {
        video_->setPickMode(false);
        return;
    }

    // Si la pieza ya tiene rasgo, ofrecer quitarlo o reemplazarlo.
    if (currentAnchor_.has_value()) {
        QMessageBox box(QMessageBox::Question, tr("Rasgo distintivo"),
                        tr("Esta pieza ya tiene un rasgo distintivo. ¿Qué quieres hacer?"),
                        QMessageBox::NoButton, this);
        auto* removeBtn = box.addButton(tr("Quitar rasgo"), QMessageBox::DestructiveRole);
        auto* replaceBtn = box.addButton(tr("Marcar otro"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        anchorButton_->setChecked(false);
        if (box.clickedButton() == removeBtn) {
            currentAnchor_.reset();
            video_->setAnchorMarker(false);
            if (const std::int64_t pieceId = selectedPieceId();
                pieceId >= 0 && repos_.pieces != nullptr) {
                repos_.pieces->clearAnchor(pieceId);
            }
            statusBar()->showMessage(tr("Rasgo distintivo eliminado."));
            return;
        }
        if (box.clickedButton() != replaceBtn) {
            return;
        }
        // Reemplazar: continúa al modo de selección abajo.
    }

    if (!streaming_ || !liveFixture_.has_value()) {
        statusBar()->showMessage(
            tr("Para marcar el rasgo necesitas video en vivo con la pieza detectada."));
        anchorButton_->setChecked(false);
        return;
    }
    video_->setPickMode(true);
    statusBar()->showMessage(
        tr("Haz clic sobre un punto único de la pieza (agujero, marca, esquina oscura)…"));
}

// Lectura continua "cuánto está descentrada y girada la pieza" respecto al
// tablero (T3). Con el origen en la propia pieza la desviación es cero por
// definición, así que ahí solo tiene sentido el giro: se dice explícitamente en
// vez de mostrar un 0,0 que parecería un fallo.
void MainWindow::updateBoardReadout() {
    if (boardReadoutLabel_ == nullptr) {
        return;
    }
    if (!boardVisible_) {
        boardReadoutLabel_->setVisible(false);
        return;
    }
    boardReadoutLabel_->setVisible(true);
    if (!liveFixture_.has_value()) {
        boardReadoutLabel_->setText(tr("Tablero: sin pieza detectada"));
        return;
    }

    const vision::BoardFrame frame = video_->boardFrame();
    const vision::BoardReading reading = vision::readPiece(frame, *liveFixture_);
    const double offsetDeg = vision::pieceAngleOffset(frame, *liveFixture_);

    // Misma unidad que el resto de la UI: mm/cm si hay escala, px si no.
    const inspection::LengthUnit unit = currentUnit();
    const double mmPerPixel = calibration_.mmPerPixel;
    const auto len = [unit, mmPerPixel](double px) {
        if (mmPerPixel > 0.0 && unit != inspection::LengthUnit::Pixels) {
            const double mm = px * mmPerPixel;
            return (unit == inspection::LengthUnit::Centimeters)
                       ? QStringLiteral("%1 cm").arg(mm / 10.0, 0, 'f', 2)
                       : QStringLiteral("%1 mm").arg(mm, 0, 'f', 1);
        }
        return QStringLiteral("%1 px").arg(px, 0, 'f', 0);
    };
    const auto signedLen = [&len](double px) {
        return (px > 0.0 ? QStringLiteral("+") : QString()) + len(px);
    };

    // Reglas activas (M4): se muestran junto a la lectura para que el operador
    // pueda colocar la pieza ANTES de inspeccionar, y la banda se pone en rojo
    // cuando la posición actual daría NG.
    QString limits;
    bool outOfTolerance = false;
    if (measurementMode_ == domain::MeasurementMode::Special) {
        if (maxOffsetPx_ > 0.0) {
            limits += tr("  ·  máx %1").arg(len(maxOffsetPx_));
            outOfTolerance = outOfTolerance || reading.radius > maxOffsetPx_;
        }
        if (maxAngleDeg_ > 0.0) {
            limits += tr("  ·  máx %1°").arg(maxAngleDeg_, 0, 'f', 1);
            outOfTolerance = outOfTolerance || std::abs(offsetDeg) > maxAngleDeg_;
        }
    }
    // setStyleSheet reanaliza el CSS y repule el widget: llamarlo en cada
    // análisis (unas 30 veces por segundo) costaba CPU y hacía parpadear la
    // banda. Solo se cambia cuando cambia de estado.
    if (outOfTolerance != boardReadoutAlarm_) {
        boardReadoutAlarm_ = outOfTolerance;
        // El mismo estilo que al crearla, y de la misma fuente: estaba tecleado
        // dos veces, y así es como una copia acaba con otra cifra.
        boardReadoutLabel_->setStyleSheet(
            outOfTolerance
                ? theme::bandStyle(theme::kInkOnBandAlarm, theme::kBandAlarm, true)
                : theme::bandStyle(theme::kInkOnBand, theme::kBandField));
    }

    const bool zeroOnPiece = boardConfig_.origin == vision::BoardOrigin::PieceCenter ||
                             boardConfig_.origin == vision::BoardOrigin::PieceBounds;
    if (zeroOnPiece && boardConfig_.manualOffset.x == 0.0F &&
        boardConfig_.manualOffset.y == 0.0F) {
        // El cero está sobre la pieza: su desviación es 0 por definición.
        boardReadoutLabel_->setText(
            tr("Tablero: el cero viaja con la pieza · giro %1°%2")
                .arg(offsetDeg, 0, 'f', 1)
                .arg(limits));
        return;
    }
    boardReadoutLabel_->setText(tr("Tablero: dx %1 · dy %2 · radio %3 · giro %4°%5")
                                    .arg(signedLen(reading.dx), signedLen(reading.dy),
                                         len(reading.radius))
                                    .arg(offsetDeg, 0, 'f', 1)
                                    .arg(limits));
}

// Aplica a toda la UI el modo y el tablero de una pieza. Decisión del usuario
// (2026-07-27): manda la pieza — en modo Especial el tablero se enciende con su
// configuración y en modo Real se apaga.
void MainWindow::applyMeasurement(const repositories::PieceMeasurement& measurement) {
    measurementMode_ = measurement.mode;
    boardConfig_ = measurement.board;
    maxOffsetPx_ = measurement.maxOffsetPx;
    maxAngleDeg_ = measurement.maxAngleDeg;
    boardVisible_ = measurement.mode == domain::MeasurementMode::Special;

    video_->setBoardConfig(boardConfig_);
    video_->setBoardVisible(boardVisible_);
    if (repos_.engine != nullptr) {
        repos_.engine->setBoardConfig(boardConfig_);
    }

    // Menús al día sin re-disparar sus señales (evita guardados en cascada).
    if (boardAction_ != nullptr) {
        QSignalBlocker blocker(boardAction_);
        boardAction_->setChecked(boardVisible_);
    }
    if (boardFollowAction_ != nullptr) {
        QSignalBlocker blocker(boardFollowAction_);
        boardFollowAction_->setChecked(boardConfig_.followPieceAngle);
    }
    if (boardOriginGroup_ != nullptr) {
        for (auto* action : boardOriginGroup_->actions()) {
            if (action->data().toInt() == static_cast<int>(boardConfig_.origin)) {
                QSignalBlocker blocker(action);
                action->setChecked(true);
            }
        }
    }
    updateModeChip();
    updateBoardReadout();
}

// Etiqueta del modo activo (M3). Cambia de color para distinguirse de un
// vistazo y el tooltip explica qué implica el modo y dónde se cambia.
// El recuento de piezas, donde se trabaja.
//
// Se destaca cuando hay MÁS de una, porque es el único caso en que cambia lo
// que el operador debe hacer: con varias en el encuadre, las herramientas miden
// la mayor y las demás quedan sin medir. Con una sola, el aviso sería ruido.
void MainWindow::updatePiecesChip() {
    if (piecesChip_ == nullptr) {
        return;
    }
    if (lastPieceCount_ < 0 || !countingPieces()) {
        piecesChip_->setVisible(false);
        // Y el selector de pieza con el. Sin esta linea se quedaba a la vista
        // diciendo «pieza 1/2» despues de que el operador declarara que hay una
        // sola: estado viejo en pantalla, que es peor que no enseñar nada.
        updatePieceNavigator();
        return;
    }
    const bool several = lastPieceCount_ > 1;
    const bool someLeftOut = lastPiecesSeen_ > lastPieceCount_;
    piecesChip_->setVisible(true);
    // Sin `%n`: el plural de Qt sólo se resuelve con un traductor cargado, y sin
    // él esto se queda en «6 pieza(s)» de forma permanente en pantalla.
    // Cuando sobran manchas se dicen LAS DOS cifras. Enseñar solo las usadas
    // haria desaparecer del informe una sombra de mas sin dejar rastro; enseñar
    // solo las vistas contradiria al selector, que numera las usadas.
    // Y LAS QUE SE CAYERON POR PEQUEÑAS, EN EL PROPIO ROTULO.
    //
    // El aviso con su ajuste ya existía, pero vivía SOLO en el emergente del
    // chip, y un emergente hay que ir a buscarlo con el ratón: mientras tanto la
    // pantalla dice «4 piezas» sobre una foto que tiene dieciséis.
    //
    // Medido sobre el banco, con el área mínima de fábrica: `arandelas-4.png`
    // enseña 4 de 16 —los cuatro anillos, y los doce tornillos fuera—, y
    // `arandelas-1.png`, 5 de unas veinte. No es un caso raro de una foto rara:
    // es lo que pasa en cuanto la bandeja mezcla tamaños.
    //
    // El «+12» no cabe explicado en el rótulo y no hace falta que quepa: dice
    // que hay algo más y cuánto, que es lo que hace mirar. El porqué y el ajuste
    // que lo arregla siguen en el emergente, que es donde se va a mirar cuando
    // el número extraña.
    const QString dropped =
        lastPiecesTooSmall_ > 0 ? tr("+%1 pequeñas ").arg(lastPiecesTooSmall_) : QString();
    piecesChip_->setText(
        (someLeftOut ? tr(" %1 de %2 ").arg(lastPieceCount_).arg(lastPiecesSeen_)
                     : (several ? tr(" %1 piezas ").arg(lastPieceCount_)
                                : tr(" 1 pieza "))) +
        dropped);
    piecesChip_->setStyleSheet(
        (several || someLeftOut || lastPiecesTooSmall_ > 0)
            ? theme::noticeStyle(theme::kWarn, theme::kWarnField) +
                  QStringLiteral(" border-radius:8px; padding:1px 6px; font-weight:bold;")
            : QStringLiteral("color:%1; background:%2; border-radius:8px;"
                             " padding:1px 6px;")
                  .arg(QString(theme::kInkMuted), QString(theme::kSurfaceSunken)));
    // LO QUE SE CAYÓ POR PEQUEÑO, DICHO.
    //
    // Estas manchas se descartan ANTES de contarse, así que no aparecen en
    // ninguna de las dos cifras de arriba. Sin esta frase el operador ve «1
    // pieza» sobre una foto con dieciséis y no tiene ni el número ni idea de qué
    // tocar. Medido sobre la foto de catálogo `arandelas-2`: el área mínima de
    // fábrica deja UNA de dieciséis arandelas.
    //
    // Va con el ajuste que lo arregla dentro del texto, porque un aviso que dice
    // que algo pasa y no dice dónde se toca obliga a buscarlo.
    const QString tooSmall =
        lastPiecesTooSmall_ > 0
            ? tr("Además, %1 mancha(s) más se quedaron fuera por no llegar al área "
                 "mínima. Si son piezas tuyas, baja «Área mínima» en Configurar ▸ "
                 "Detección.")
                  .arg(lastPiecesTooSmall_)
            : QString();

    // El emergente lleva el ajuste con el que se arregla (probado por nombre:
    // «Área mínima»), así que se queda en el tooltip y no en el «Shift+F1»,
    // que aquí nadie va a pulsar sobre una pastilla de estado.
    const auto withTooSmall = [&](const QString& base) {
        return tooSmall.isEmpty() ? base : base + QStringLiteral("\n\n") + tooSmall;
    };
    if (someLeftOut) {
        piecesChip_->setToolTip(withTooSmall(
            tr("Se ven %1 manchas y has declarado %2 piezas: se trabaja con las %2 "
               "mayores.")
                .arg(lastPiecesSeen_)
                .arg(lastPieceCount_)));
        updatePieceNavigator();
        return;
    }
    piecesChip_->setToolTip(withTooSmall(
        several ? tr("Se ven %1 piezas en el encuadre.").arg(lastPieceCount_)
                : tr("Se ve una sola pieza en el encuadre.")));
    if (several) {
        piecesChip_->setWhatsThis(
            tr("Las herramientas miden una sola: la que dice el selector de al lado. "
               "Las demás se cuentan y se pueden mirar con las flechas."));
    }
    updatePieceNavigator();
}

// Cuál de las piezas se está midiendo, y cómo pasar a otra.
//
// El número va en ORDEN DE LECTURA —filas de arriba abajo, izquierda a derecha—
// que es el mismo con el que salen de la detección, y por eso «la tercera»
// significa lo mismo en la pantalla, en el informe y en la mesa.
// EL MOSAICO COME DE LO MISMO QUE EL VÍDEO.
//
// Los contornos ya los calculó el análisis; volver a segmentar para pintar un
// panel sería pagar dos veces por la misma respuesta, y con el riesgo de que
// las dos no coincidieran — que es peor que no tener panel: el operador
// elegiría la pieza 3 del mosaico y se le mediría otra.
//
// El recorte sale de `analysedFrame_` y no de `lastFrame_` por lo mismo: el
// análisis va por detrás del vídeo y los contornos son de aquel frame.
void MainWindow::showPiecesInMosaic(const AnalysisOverlay& overlay) {
    if (mosaic_ == nullptr || mosaicDock_ == nullptr) {
        return;
    }
    // Con una sola pieza el panel no añade nada: el vídeo ya la enseña entera y
    // más grande. Se deja lo último que hubo en vez de vaciarlo, porque vaciar
    // en cada fotograma sin varias piezas haría parpadear el panel abierto.
    if (overlay.pieceContours.size() <= 1) {
        return;
    }
    if (!mosaicOffered_) {
        mosaicOffered_ = true;
        mosaicDock_->setVisible(true);
    }
    // CON EL PANEL CERRADO NO SE PINTA NADA.
    //
    // Reconstruirlo cuesta 12,3 ms con la bandeja de cien tuercas (medido sobre
    // la imagen real). Eso entra de sobra en un fotograma —techo de 81 por
    // segundo— pero solo si alguien lo está mirando: gastarlo en cada análisis
    // con el panel cerrado es tirar un tercio del presupuesto de fotograma para
    // pintar algo que nadie ve.
    if (!mosaicDock_->isVisible()) {
        return;
    }
    mosaic_->setPieces(analysedFrame_, overlay.pieceContours, overlay.measuredPiece);
}

void MainWindow::showMosaicPanel(bool on) {
    if (mosaicDock_ == nullptr) {
        return;
    }
    // Pedirlo a mano cuenta como haberlo decidido: ya no se le vuelve a ofrecer
    // solo la próxima vez que aparezcan varias piezas. Ofrecerle un panel a
    // quien acaba de apagarlo es no haberle escuchado.
    mosaicOffered_ = true;
    mosaicDock_->setVisible(on);
}

void MainWindow::updatePieceNavigator() {
    if (pieceNav_ == nullptr) {
        return;
    }
    const bool several = lastPieceCount_ > 1 && countingPieces();
    pieceNav_->setVisible(several);
    if (!several) {
        return;
    }
    const int shown = lastMeasuredPiece_ > 0 ? lastMeasuredPiece_ : 1;
    // Se dice cuándo la elección es del programa y cuándo es del operador. Sin
    // eso, «3 / 6» no distingue «he elegido la 3» de «te ha tocado la 3».
    pieceNavLabel_->setText(focusedPiece_ == 0
                                ? tr(" pieza %1/%2 (la mayor) ").arg(shown).arg(lastPieceCount_)
                                : tr(" pieza %1/%2 ").arg(shown).arg(lastPieceCount_));
    // En reposo mide la mayor —lo decide la aplicación— y elegida la señaló el
    // operador. El azul de «elegida» era aquí #7fd1ff y en el indicador de modo
    // #7fd6ff: dos azules para lo mismo, que es exactamente lo que la paleta
    // viene a impedir.
    pieceNavLabel_->setStyleSheet(focusedPiece_ == 0 ? theme::chipRestStyle()
                                                     : theme::chipChosenStyle());
    const QString tip = tr("Qué pieza del encuadre están midiendo las herramientas.");
    pieceNavLabel_->setToolTip(tip);
    piecePrevButton_->setToolTip(tip);
    pieceNextButton_->setToolTip(tip);
    const QString detail =
        tr("Numeradas en orden de lectura, por filas de arriba abajo. Sin elegir "
           "ninguna se mide la mayor; pasa de la última a «la mayor» para volver a ese "
           "modo.");
    pieceNavLabel_->setWhatsThis(detail);
    piecePrevButton_->setWhatsThis(detail);
    pieceNextButton_->setWhatsThis(detail);
}

// Pasar a la pieza siguiente o a la anterior.
//
// El recorrido incluye el estado «la mayor» como si fuera una posición más, al
// final: así se sale del modo manual con el mismo gesto con el que se entró, y
// no hace falta descubrir otro control para volver.
void MainWindow::stepFocusedPiece(int delta) {
    if (lastPieceCount_ <= 1) {
        return;
    }
    const int positions = lastPieceCount_ + 1;  // 1..N, más «la mayor» en el 0
    // UN RECORRIDO LLANO: 0 (la mayor), 1, 2, ... N, y vuelta a empezar.
    //
    // La primera versión intentaba ser lista: avanzar desde «la mayor» llevaba a
    // la siguiente de la que se estaba midiendo, para que el salto fuera al
    // vecino de lo que el operador tiene delante. La prueba lo tumbó, y con
    // razón — al volver a «la mayor» se recalculaba lo mismo, así que se quedaba
    // rebotando entre esas dos posiciones y las demás piezas eran INALCANZABLES
    // avanzando. Con tres piezas, dos no se podían mirar.
    //
    // Un recorrido predecible en el que todas las posiciones salen antes o
    // después vale más que uno que acierta el atajo y pierde piezas.
    focusedPiece_ = ((focusedPiece_ + delta) % positions + positions) % positions;
    if (focusedPiece_ == 0) {
        statusBar()->showMessage(tr("Midiendo la pieza mayor del encuadre."));
    } else {
        statusBar()->showMessage(
            tr("Midiendo la pieza %1 de %2, en orden de lectura.")
                .arg(focusedPiece_)
                .arg(lastPieceCount_));
    }
    updatePieceNavigator();
    reanalyseCurrentFrame();
}

// El aviso de que el borde lleva una correccion a mano.
//
// Existe porque el trazo se retira una vez aplicado: sin el, una correccion
// activa seria estado invisible. Dice cuantos pixeles y como quitarla.
void MainWindow::updateEdgeCorrectionChip() {
    if (edgeChip_ == nullptr || video_ == nullptr) {
        return;
    }
    const int corrected = video_->correctedPixelCount();
    if (corrected <= 0) {
        edgeChip_->setVisible(false);
        if (brushUndoAction_ != nullptr) {
            brushUndoAction_->setEnabled(video_->canUndoEdgeCorrection());
        }
        if (brushRedoAction_ != nullptr) {
            brushRedoAction_->setEnabled(video_->canRedoEdgeCorrection());
        }
        return;
    }
    // ¿SE ESTÁ APLICANDO DE VERDAD?
    //
    // La pastilla miraba solo lo que tiene el LIENZO pintado, y eso no es lo
    // mismo que lo que usa el análisis. Con una corrección que no encaja con el
    // frame actual, la pastilla decía «Borde corregido» mientras el contorno
    // salía sin corregir — y el operador no tenía forma de saber cuál de las dos
    // cosas creerse.
    //
    // Una etiqueta que afirma algo que no está pasando es peor que no tener
    // etiqueta: la primera se cree.
    const bool applied = !pipelineConfig_.forcePiece.empty() &&
                         (lastFrame_.isNull() ||
                          (pipelineConfig_.forcePiece.cols == lastFrame_.width() &&
                           pipelineConfig_.forcePiece.rows == lastFrame_.height()));
    edgeChip_->setVisible(true);
    if (!applied) {
        edgeChip_->setText(tr(" Borde corregido: sin aplicar "));
        edgeChip_->setStyleSheet(theme::noticeStyle(theme::kWarn, theme::kWarnField) +
                                 QStringLiteral(" border-radius:8px; padding:1px 6px;"));
        edgeChip_->setToolTip(
            tr("Hay %1 px corregidos a mano, pero son de otra imagen: no se aplican.")
                .arg(corrected));
        edgeChip_->setWhatsThis(tr("Vuelve a corregir sobre la imagen que tienes delante."));
        if (brushUndoAction_ != nullptr) {
            brushUndoAction_->setEnabled(video_->canUndoEdgeCorrection());
        }
        if (brushRedoAction_ != nullptr) {
            brushRedoAction_->setEnabled(video_->canRedoEdgeCorrection());
        }
        return;
    }
    edgeChip_->setText(tr(" Borde corregido "));
    edgeChip_->setStyleSheet(theme::chipChosenStyle(theme::kChipEdited));
    edgeChip_->setToolTip(tr("%1 px del borde están puestos a mano.").arg(corrected));
    edgeChip_->setWhatsThis(
        tr("El trazo ya no se pinta: lo que ves es el contorno que sale de la "
           "corrección. Con el pincel activo, Ctrl+Z deshace la última; en "
           "\u00abCorregir borde\u00bb puedes quitarlas todas o afinar la detección con "
           "ellas."));
    if (brushUndoAction_ != nullptr) {
        brushUndoAction_->setEnabled(video_->canUndoEdgeCorrection());
    }
    if (brushRedoAction_ != nullptr) {
        brushRedoAction_->setEnabled(video_->canRedoEdgeCorrection());
    }
}

void MainWindow::updateModeChip() {
    if (modeChip_ == nullptr) {
        return;
    }
    const bool special = measurementMode_ == domain::MeasurementMode::Special;
    modeChip_->setText(special ? tr(" Especial (tablero) ") : tr(" Posición real "));
    modeChip_->setStyleSheet(special ? theme::chipChosenStyle()
                                     : theme::chipRestStyle());
    modeChip_->setToolTip(QString::fromUtf8(domain::modeDescription(measurementMode_)) +
                          tr("\n\nSe cambia en Pieza ▸ Modo de medición…"));
}

// Herramientas de Posición dibujadas ahora mismo. Sus tolerancias se sugieren
// respecto al cero del tablero, así que cambiar el origen las deja midiendo
// otra cosa: hay que avisar en vez de invalidarlas en silencio (revisión de
// diseño previa a M4).
int MainWindow::positionToolCount() const {
    int count = 0;
    for (const auto& tool : liveTools_) {
        if (!tool.deleted && tool.config.type == inspection::ToolType::Position) {
            ++count;
        }
    }
    return count;
}

void MainWindow::warnIfPositionToolsAffected(vision::BoardOrigin previousOrigin) {
    const int count = positionToolCount();
    if (count == 0 || previousOrigin == boardConfig_.origin) {
        return;
    }
    if (positionWarningShown_) {
        // Ya se avisó en esta sesión: repetir el diálogo cada vez que se toca el
        // origen es molesto y deja de leerse. Basta la barra de estado.
        statusBar()->showMessage(
            tr("Cambió el cero: revisa las %n herramienta(s) de Posición.", nullptr, count));
        return;
    }
    positionWarningShown_ = true;
    QMessageBox::information(
        this, tr("El cero del tablero ha cambiado"),
        tr("Hay %n herramienta(s) de Posición dibujada(s). Sus tolerancias se "
           "calcularon respecto al cero anterior, así que ahora miden otra cosa: "
           "revísalas (o vuelve a crearlas) antes de dar por buena la inspección.\n\n"
           "(Este aviso no se repetirá en esta sesión.)",
           nullptr, count));
}

void MainWindow::loadMeasurementForSelectedPiece() {
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 || repos_.pieces == nullptr) {
        return;  // sin pieza: se conserva el ajuste de la sesión
    }
    if (auto loaded = repos_.pieces->loadMeasurement(pieceId); loaded.isOk()) {
        applyMeasurement(loaded.value());
        // Las piezas esperadas viajan con la pieza (C5): al cambiar de trabajo
        // se recupera su recuento, y el de la anterior no se arrastra.
        expectedPieces_ = loaded.value().expectedPieces;
        // Y si esta pieza se mira en mosaico. Va con el trabajo: quien pasa de
        // una bandeja a una pieza suelta no tiene por qué acordarse del panel.
        showMosaic_ = loaded.value().showMosaic;
        showMosaicPanel(showMosaic_);
        // Y AL PIPELINE, que es quien decide con cuantas manchas se trabaja.
        // Sin esta linea, cambiar de pieza recuperaba su recuento en la ventana
        // y dejaba a la deteccion con el de la pieza anterior.
        pipelineConfig_.expectedPieces = expectedPieces_;
        lastPieceCount_ = -1;
        lastPiecesSeen_ = -1;
        // Y LA VENTANA DE CONFIGURAR, SI ESTÁ ABIERTA, TAMBIÉN.
        //
        // Es única y vive fuera del selector de piezas, así que se puede cambiar
        // de trabajo con ella abierta. Lo que había dentro era entonces de la
        // pieza ANTERIOR — y «piezas esperadas» y «ver en mosaico» se guardan
        // con la pieza, así que aceptar escribía los ajustes de la bandeja
        // encima de la pieza suelta recién seleccionada. Sin avisar, y sin
        // forma de notarlo hasta que esa pieza empieza a dar NG de recuento.
        //
        // No es que se pierda un ajuste: es que se le copia a un trabajo que no
        // es el suyo, que es peor.
        if (configureDialog_ != nullptr) {
            if (auto* page = configureDialog_->piecesPage(); page != nullptr) {
                page->setExpectedPieces(expectedPieces_);
                page->setShowMosaic(showMosaic_);
            }
        }
        // Y la eleccion de pieza no se arrastra de un trabajo a otro: «la
        // tercera» de la bandeja anterior no significa nada en esta.
        focusedPiece_ = 0;
    }
}

void MainWindow::onMeasurementModeClicked() {
    repositories::PieceMeasurement current;
    current.mode = measurementMode_;
    current.board = boardConfig_;
    current.maxOffsetPx = maxOffsetPx_;
    current.maxAngleDeg = maxAngleDeg_;

    const std::int64_t pieceId = selectedPieceId();
    MeasurementModeDialog dialog(current, pieceCombo_->currentText(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const vision::BoardOrigin previousOrigin = boardConfig_.origin;
    applyMeasurement(dialog.measurement());
    persistBoardConfig();  // guarda en la pieza (si hay) y en el ajuste global
    warnIfPositionToolsAffected(previousOrigin);

    const QString modeName = QString::fromUtf8(domain::modeLabel(measurementMode_));
    statusBar()->showMessage(
        pieceId < 0
            ? tr("Modo de medición de la sesión: %1 (aún sin pieza seleccionada).")
                  .arg(modeName)
            : tr("La pieza %1 medirá en modo %2.")
                  .arg(pieceCombo_->currentText(), modeName));
}

void MainWindow::onBoardOriginChanged(QAction* action) {
    if (action == nullptr) {
        return;
    }
    const vision::BoardOrigin previousOrigin = boardConfig_.origin;
    boardConfig_.origin = static_cast<vision::BoardOrigin>(action->data().toInt());
    video_->setBoardConfig(boardConfig_);
    persistBoardConfig();
    updateBoardReadout();
    warnIfPositionToolsAffected(previousOrigin);
    if (boardConfig_.origin == vision::BoardOrigin::FixedPoint) {
        // El punto se marca con el ratón; el canvas sale del modo al primer clic.
        boardPointPick_ = true;
        video_->setPickMode(true);
        statusBar()->showMessage(tr("Haz clic en el punto que será el cero del tablero."));
        return;
    }
    switch (boardConfig_.origin) {
        case vision::BoardOrigin::PieceBounds:
            statusBar()->showMessage(
                tr("Centrado automático en el centro del contorno de la pieza."));
            break;
        case vision::BoardOrigin::PieceCenter:
            statusBar()->showMessage(
                tr("Centrado automático en el centro de masa (puede no verse centrado en "
                   "piezas asimétricas)."));
            break;
        case vision::BoardOrigin::ImageCenter:
            statusBar()->showMessage(
                tr("Tablero centrado en la imagen: el cero queda fijo en pantalla."));
            break;
        case vision::BoardOrigin::FixedPoint:
            break;  // gestionado arriba
    }
}

vision::BoardConfig MainWindow::defaultBoardConfig() const {
    vision::BoardConfig config;
    if (repos_.settings == nullptr) {
        return config;
    }
    config.origin = vision::originFromKey(
        repos_.settings->getString("board_origin", std::string("bounds")).valueOr(std::string("bounds")));
    config.followPieceAngle = repos_.settings->getInt("board_follow", 0).valueOr(0) != 0;
    config.fixedPoint = {
        static_cast<float>(repos_.settings->getDouble("board_fixed_x", 0.0).valueOr(0.0)),
        static_cast<float>(repos_.settings->getDouble("board_fixed_y", 0.0).valueOr(0.0))};
    config.manualOffset = {
        static_cast<float>(repos_.settings->getDouble("board_offset_x", 0.0).valueOr(0.0)),
        static_cast<float>(repos_.settings->getDouble("board_offset_y", 0.0).valueOr(0.0))};
    return config;
}

void MainWindow::seedMeasurementForNewPiece(std::int64_t pieceId) {
    if (pieceId < 0 || repos_.pieces == nullptr) {
        return;
    }
    // Se lee y se reescribe la fila entera, como en todas partes: construir un
    // `PieceMeasurement` desde cero pondría a su valor por defecto todo lo que
    // esta función no toca.
    auto measurement = repos_.pieces->loadMeasurement(pieceId);
    if (!measurement.isOk()) {
        core::logWarning("No se pudo leer la medición de la pieza nueva: " +
                         measurement.error().message);
        return;
    }
    measurement.value().board = defaultBoardConfig();
    if (auto saved = repos_.pieces->saveMeasurement(pieceId, measurement.value());
        !saved.isOk()) {
        core::logWarning("No se pudo sembrar el tablero de la pieza nueva: " +
                         saved.error().message);
    }
}

void MainWindow::persistBoardConfig() {
    // El cero del tablero es un punto en coordenadas de imagen, así que su
    // resolución de referencia se guarda igual que la de la zona.
    persistPixelReference();
    // El motor de inspección juzga las herramientas de Posición con este mismo
    // tablero: si no se le pasa, el veredicto no coincidiría con lo que se ve.
    if (repos_.engine != nullptr) {
        repos_.engine->setBoardConfig(boardConfig_);
    }
    // La regla, una sola y escrita: **el ajuste global es solo la plantilla
    // para piezas nuevas**. Con una pieza seleccionada mandan sus columnas y
    // ahí va todo cambio; sin pieza, se guarda en `Settings` como valor por
    // defecto de la próxima.
    if (const std::int64_t pieceId = selectedPieceId();
        pieceId >= 0 && repos_.pieces != nullptr) {
        // Leer, modificar, escribir. `saveMeasurement` escribe la FILA ENTERA:
        // construir un `PieceMeasurement` nuevo aquí ponía a su valor por
        // defecto todo lo que esta función no toca — y así, cambiar el origen
        // del tablero borraba en silencio las piezas esperadas de la pieza.
        auto measurement = repos_.pieces->loadMeasurement(pieceId);
        if (!measurement.isOk()) {
            core::logWarning("No se pudo leer la medición de la pieza: " +
                             measurement.error().message);
            return;
        }
        measurement.value().mode = measurementMode_;
        measurement.value().board = boardConfig_;
        measurement.value().maxOffsetPx = maxOffsetPx_;
        measurement.value().maxAngleDeg = maxAngleDeg_;
        if (auto saved = repos_.pieces->saveMeasurement(pieceId, measurement.value());
            !saved.isOk()) {
            core::logWarning("No se pudo guardar el modo de medición: " +
                             saved.error().message);
        }
        // Con pieza seleccionada NO se toca el global: es la plantilla de las
        // piezas nuevas, y pisarla con los ajustes de esta haría que la
        // siguiente naciera con el tablero de la anterior.
        return;
    }
    if (repos_.settings == nullptr) {
        return;
    }
    repos_.settings->setString("board_origin",
                               std::string(vision::originKey(boardConfig_.origin)));
    repos_.settings->setInt("board_follow", boardConfig_.followPieceAngle ? 1 : 0);
    repos_.settings->setDouble("board_fixed_x", boardConfig_.fixedPoint.x);
    repos_.settings->setDouble("board_fixed_y", boardConfig_.fixedPoint.y);
    repos_.settings->setDouble("board_offset_x", boardConfig_.manualOffset.x);
    repos_.settings->setDouble("board_offset_y", boardConfig_.manualOffset.y);
}

void MainWindow::onAnchorPicked(const cv::Point2f& imagePoint) {
    // El mismo gesto de "elegir un punto" sirve para fijar el cero del tablero.
    if (boardPointPick_) {
        boardPointPick_ = false;
        boardConfig_.fixedPoint = imagePoint;
        video_->setBoardConfig(boardConfig_);
        persistBoardConfig();
        updateBoardReadout();
        if (!boardVisible_ && boardAction_ != nullptr) {
            boardAction_->setChecked(true);  // mostrar lo que se acaba de fijar
        }
        statusBar()->showMessage(tr("Cero del tablero fijado en (%1, %2) px.")
                                     .arg(qRound(imagePoint.x))
                                     .arg(qRound(imagePoint.y)));
        return;
    }
    anchorButton_->setChecked(false);
    if (!liveFixture_.has_value() || lastFrame_.isNull()) {
        return;
    }

    vision::OrientationAnchor anchor;
    anchor.piecePoint = vision::toPieceCoords(*liveFixture_, imagePoint);
    anchor.intensity = vision::sampleIntensity(camera::qImageToMat(lastFrame_), imagePoint);
    currentAnchor_ = anchor;
    video_->setAnchorMarker(true, anchor.piecePoint);

    const std::int64_t pieceId = selectedPieceId();
    if (pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto saved = repos_.pieces->saveAnchor(pieceId, anchor); saved.isOk()) {
            statusBar()->showMessage(
                tr("Rasgo distintivo guardado: la pieza se detectará en cualquier rotación."));
        } else {
            statusBar()->showMessage(QString::fromStdString(saved.error().message));
        }
    } else {
        statusBar()->showMessage(
            tr("Rasgo marcado: se guardará con la pieza al registrar."));
    }
}

// Sincroniza el spin de "Puntos" con la herramienta seleccionada en el video.
void MainWindow::onLiveSelectionChanged(int index) {
    liveParamSpin_->setEnabled(false);
    liveParamLabel_->setText(tr("Puntos:"));
    const bool valid = index >= 0 && index < static_cast<int>(liveTools_.size());
    // Los dos botones de borrar saben si tienen algo que hacer. Se pasa el
    // recuento de SELECCIONADAS y no un booleano porque el marco de selección
    // múltiple puede llevarse varias, y el tooltip lo dice.
    toolPalette_->setDeletable(static_cast<int>(video_->selectedIndices().size()),
                               static_cast<int>(liveTools_.size()));
    // Calibrar con la medida: solo tiene sentido en herramientas de longitud.
    // Calibrar con la medida requiere una LONGITUD; Blob (conteo) y
    // Línea-Línea (grados) no sirven.
    calibrateFromToolButton_->setEnabled(
        valid && liveTools_[static_cast<std::size_t>(index)].config.type !=
                     inspection::ToolType::Blob &&
        liveTools_[static_cast<std::size_t>(index)].config.type !=
            inspection::ToolType::LineToLine &&
        liveTools_[static_cast<std::size_t>(index)].config.type !=
            inspection::ToolType::Angle &&
        liveTools_[static_cast<std::size_t>(index)].config.type !=
            inspection::ToolType::PolyBlob &&
        liveTools_[static_cast<std::size_t>(index)].config.type !=
            inspection::ToolType::Position);
    if (!valid) {
        return;
    }
    QSignalBlocker blocker(liveParamSpin_);
    // Doce herramientas comparten "puntos de medida" y lo decide el MODELO
    // (`pointCountOf`), igual que en el editor de plantilla: una sola fuente
    // de verdad para el rango y el rótulo, o los dos paneles podrían acabar
    // ofreciendo un rango distinto para la misma herramienta.
    const auto& selectedTool = liveTools_[static_cast<std::size_t>(index)];
    const inspection::PointCountSpec pointSpec = inspection::pointCountOf(selectedTool.geometry);
    std::visit(
        [this, &pointSpec, &selectedTool](const auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, inspection::CaliperGeometry>) {
                liveParamLabel_->setText(tr("Banda (px):"));
                liveParamSpin_->setObjectName(QStringLiteral("spinCaliperBanda"));
                liveParamSpin_->setRange(1, 1000);
                liveParamSpin_->setToolTip(
                    tr("Grosor perpendicular promediado del calibre (px)."));
                liveParamSpin_->setValue(static_cast<int>(g.bandWidth));
                liveParamSpin_->setEnabled(true);
            } else if constexpr (std::is_same_v<T, inspection::BlobGeometry>) {
                liveParamLabel_->setText(tr("Área mín:"));
                liveParamSpin_->setObjectName(QStringLiteral("spinBlobAreaMinima"));
                liveParamSpin_->setRange(1, 1000);
                liveParamSpin_->setToolTip(
                    tr("Área mínima para contar una mancha como blob (px²)."));
                liveParamSpin_->setValue(static_cast<int>(g.minArea));
                liveParamSpin_->setEnabled(true);
            } else if constexpr (std::is_same_v<T, inspection::PositionGeometry>) {
                liveParamLabel_->setText(tr("Eje:"));
                liveParamSpin_->setObjectName(QStringLiteral("spinPosicionEje"));
                liveParamSpin_->setRange(1, 1000);
                liveParamSpin_->setToolTip(
                    tr("Eje sobre el que se juzga la desviación (1 radial, 2 X, 3 Y)."));
                liveParamSpin_->setValue(static_cast<int>(g.axis) + 1);
                liveParamSpin_->setEnabled(true);
            } else if constexpr (std::is_same_v<T, inspection::RegionGeometry>) {
                // El editor tiene un desplegable con los nombres; aquí, donde
                // solo hay este spin, se numeran. Los números salen de la misma
                // lista, así que no pueden desordenarse respecto al editor.
                QString tip = tr("Qué mide esta Región:");
                const auto& measures = inspection::allRegionMeasures();
                for (std::size_t i = 0; i < measures.size(); ++i) {
                    tip += QStringLiteral("%1%2 = %3")
                               .arg(i == 0 ? QStringLiteral(" ") : QStringLiteral(", "))
                               .arg(i + 1)
                               .arg(QString::fromUtf8(
                                   inspection::regionMeasureLabel(measures[i])));
                }
                liveParamLabel_->setText(tr("Medida:"));
                liveParamSpin_->setObjectName(QStringLiteral("spinRegionMedida"));
                liveParamSpin_->setRange(1, 1000);
                liveParamSpin_->setToolTip(tip);
                liveParamSpin_->setValue(static_cast<int>(g.measure) + 1);
                liveParamSpin_->setEnabled(true);
            } else if (pointSpec.editable) {
                liveParamLabel_->setText(tr("Puntos de medida:"));
                liveParamSpin_->setObjectName(QStringLiteral("spinPuntosMedida"));
                liveParamSpin_->setRange(pointSpec.minValue, pointSpec.maxValue);
                liveParamSpin_->setToolTip(QString::fromStdString(
                    inspection::pointCountTooltip(inspection::typeOf(selectedTool.geometry))));
                liveParamSpin_->setValue(pointSpec.value);
                liveParamSpin_->setEnabled(true);
            }
            // Punto-Línea y el resto sin número de puntos se quedan sin
            // parámetro editable aquí (el spin ya está deshabilitado).
        },
        selectedTool.geometry);
}

void MainWindow::onLiveParamChanged(int value) {
    const int index = video_->selectedIndex();
    if (!liveParamSpin_->isEnabled() || index < 0 ||
        index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    auto& geometry = liveTools_[static_cast<std::size_t>(index)].geometry;
    // Los doce con puntos de medida los escribe el modelo; Calibre, Blob,
    // Posición y Región siguen siendo casos propios de este panel.
    if (!inspection::setPointCount(geometry, value)) {
        std::visit(
            [value](auto& g) {
                using T = std::decay_t<decltype(g)>;
                if constexpr (std::is_same_v<T, inspection::CaliperGeometry>) {
                    g.bandWidth = static_cast<float>(value);
                } else if constexpr (std::is_same_v<T, inspection::BlobGeometry>) {
                    g.minArea = static_cast<float>(value);
                } else if constexpr (std::is_same_v<T, inspection::PositionGeometry>) {
                    g.axis = (value == 2)   ? inspection::PositionAxis::X
                             : (value == 3) ? inspection::PositionAxis::Y
                                            : inspection::PositionAxis::Radial;
                } else if constexpr (std::is_same_v<T, inspection::RegionGeometry>) {
                    const auto& measures = inspection::allRegionMeasures();
                    const int index = std::clamp(value, 1,
                                                 static_cast<int>(measures.size())) - 1;
                    g.measure = measures[static_cast<std::size_t>(index)];
                }
            },
            geometry);
    }
    commitUndoState();
    video_->update();
}

// Calibración fácil: la herramienta seleccionada se mide ahora mismo en
// píxeles y el usuario dice cuánto mide de verdad → escala px→mm.
void MainWindow::onCalibrateFromToolClicked() {
    const int index = video_->selectedIndex();
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    if (!liveFixture_.has_value() || lastFrame_.isNull()) {
        statusBar()->showMessage(
            tr("Necesito ver la pieza para medir la herramienta y calibrar."));
        return;
    }

    auto& tool = liveTools_[static_cast<std::size_t>(index)];
    tool.config.geometryJson = inspection::toJson(tool.geometry);
    const auto result = inspection::runTool(camera::qImageToMat(lastFrame_), *liveFixture_,
                                            tool.config);
    if (!result.isOk() || result.value().measured <= 0.0) {
        statusBar()->showMessage(
            tr("La herramienta no midió nada aquí; ajústala sobre la pieza y reintenta."));
        return;
    }
    const double measuredPx = result.value().measured;

    bool ok = false;
    const double knownMm = QInputDialog::getDouble(
        this, tr("Fijar escala con la medida"),
        tr("'%1' mide ahora %2 px.\n¿Cuánto mide de verdad? (mm)")
            .arg(QString::fromStdString(tool.config.name))
            .arg(measuredPx, 0, 'f', 1),
        10.0, 0.01, 100000.0, 2, &ok);
    if (!ok || knownMm <= 0.0) {
        return;
    }

    calibration_ = domain::calibrationFromKnownLength(
        measuredPx, knownMm, lastFrame_.width(), calibration_.horizontalFovDeg);
    calibration_.calibratedWidth = lastFrame_.width();
    calibration_.calibratedHeight = lastFrame_.height();
    persistCalibration();
    updateCalibrationLabel();
    video_->setMmPerPixel(calibration_.mmPerPixel);
    statusBar()->showMessage(
        tr("Escala fijada: %1 mm/px (%2 px = %3 mm). Todas las medidas ya están en mm.")
            .arg(calibration_.mmPerPixel, 0, 'f', 4)
            .arg(measuredPx, 0, 'f', 1)
            .arg(knownMm, 0, 'f', 2));
}

void MainWindow::rotatePieceView(double deltaDeg) {
    currentOrientationOffset_ = wrapAngleDeg(currentOrientationOffset_ + deltaDeg);
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto saved =
                repos_.pieces->saveOrientationOffset(pieceId, currentOrientationOffset_);
            !saved.isOk()) {
            statusBar()->showMessage(QString::fromStdString(saved.error().message));
            return;
        }
        statusBar()->showMessage(
            tr("Orientación de la pieza girada a %1° (guardada).")
                .arg(currentOrientationOffset_, 0, 'f', 0));
    } else {
        statusBar()->showMessage(
            tr("Vista girada a %1°: se guardará con la pieza al registrar.")
                .arg(currentOrientationOffset_, 0, 'f', 0));
    }
}

void MainWindow::onToolRightClicked(int index) {
    deleteToolAt(index);
}

// EL MENÚ DEL CLIC DERECHO SOBRE EL VÍDEO.
//
// Petición de uso: «agrega alguna función al clic derecho». Y al ir a hacerlo
// apareció algo peor que un hueco: el clic derecho sobre una cota LA BORRABA en
// el acto, sin menú y sin preguntar. En cualquier otro programa ese gesto
// significa «enséñame qué puedo hacer aquí»; aquí era el único que destruía
// trabajo, y bastaba errar el botón del ratón una vez.
//
// Tres reglas al montarlo, y las tres se notan:
//
//   - SOLO SALE LO QUE APLICA. Un menú con la mitad de las entradas en gris
//     obliga a leerlas todas para descubrir que no servían. Sobre una cota se
//     ofrecen las de la cota; sobre el vacío, las del vídeo.
//   - LO DESTRUCTIVO, AL FINAL Y SEPARADO. Borrar comparte menú con duplicar, y
//     un gesto de más con el ratón no puede costar el trabajo de media hora.
//   - CADA ENTRADA DICE SOBRE QUÉ ACTÚA. «Borrar» a secas no distingue entre la
//     cota de debajo del cursor y todas; el nombre va dentro.
// RENOMBRAR LA COTA. El nombre es lo que sale en el informe y en el parte, así
// que «Ø exterior» vale y «Círculo 7» no.
//
// Se rechaza el nombre repetido, y no por pulcritud: la ventana empareja
// herramienta y resultado POR NOMBRE en varios sitios, y dos cotas llamadas
// igual harían que una enseñara el valor de la otra.
void MainWindow::renameToolAt(int index) {
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    auto& tool = liveTools_[static_cast<std::size_t>(index)];
    bool accepted = false;
    const QString proposed = QInputDialog::getText(
        this, tr("Renombrar la cota"),
        tr("Cómo quieres que se llame en el informe:"), QLineEdit::Normal,
        QString::fromStdString(tool.config.name), &accepted);
    if (!accepted) {
        return;
    }
    const QString wanted = proposed.trimmed();
    if (wanted.isEmpty()) {
        statusBar()->showMessage(tr("Una cota sin nombre no se puede leer en un parte."));
        return;
    }
    const std::string newName = wanted.toStdString();
    if (newName == tool.config.name) {
        return;
    }
    for (const auto& other : liveTools_) {
        if (other.config.name == newName) {
            statusBar()->showMessage(
                tr("Ya hay una cota llamada «%1». Dos con el mismo nombre acaban "
                   "enseñando el valor la una de la otra.")
                    .arg(wanted));
            return;
        }
    }
    tool.config.name = newName;
    commitUndoState();
    video_->update();
    statusBar()->showMessage(tr("Ahora se llama «%1».").arg(wanted));
}

// COPIAR LO QUE MIDE, para pegarlo en un correo o en una hoja.
//
// Se copia la ÚLTIMA lectura guardada y no una nueva: volver a ejecutar la
// herramienta ahora daría un número medido en otro instante, con la pieza ya
// movida, y el operador creería estar copiando lo que tiene delante.
void MainWindow::copyReadingAt(int index) {
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    const auto& tool = liveTools_[static_cast<std::size_t>(index)];
    for (const auto& result : lastToolResults_) {
        if (result.name != tool.config.name) {
            continue;
        }
        const QString text =
            QStringLiteral("%1\t%2")
                .arg(QString::fromStdString(tool.config.name))
                .arg(QString::fromStdString(inspection::formatMeasure(
                    result, calibration_.mmPerPixel, currentUnit(), true)));
        QGuiApplication::clipboard()->setText(text);
        statusBar()->showMessage(tr("Copiado: %1").arg(text));
        return;
    }
    // Y SI NO HAY LECTURA, SE DICE. Copiar en silencio un portapapeles vacío
    // hace que el operador pegue lo que copió antes sin enterarse.
    statusBar()->showMessage(
        tr("«%1» todavía no ha medido nada: inspecciona o mide la pieza primero.")
            .arg(QString::fromStdString(tool.config.name)));
}

// MARCAR EL RASGO EN EL PUNTO DONDE SE PULSÓ.
//
// Es la misma operación de siempre, sin el modo: antes había que pulsar un
// botón, dejar el programa esperando, y acertar con el siguiente clic. Aquí el
// operador ya ha señalado dónde lo quiere.
void MainWindow::markAnchorAt(const cv::Point2f& imagePoint) {
    boardPointPick_ = false;  // este camino es el del rasgo, no el del cero
    video_->setPickMode(false);
    onAnchorPicked(imagePoint);
}

void MainWindow::onCanvasContextMenu(int tool, const QPoint& globalPos,
                                     const cv::Point2f& imagePoint) {
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    const bool onATool = tool >= 0 && tool < static_cast<int>(liveTools_.size());
    if (onATool) {
        const auto& hit = liveTools_[static_cast<std::size_t>(tool)];
        const QString name = QString::fromStdString(hit.config.name);

        // Un encabezado que dice sobre QUÉ va el menú. Sin él, con dos cotas
        // pegadas no hay forma de saber cuál se ha cogido hasta ejecutar algo.
        auto* header = menu.addAction(tr("%1 · %2")
                                          .arg(typeLabel(hit.config.type))
                                          .arg(name));
        header->setEnabled(false);
        menu.addSeparator();

        auto* rename = menu.addAction(tr("Renombrar…"));
        rename->setToolTip(tr("El nombre es lo que sale en el informe y en el parte:\n"
                              "«Ø exterior» se lee, «Círculo 7» no."));
        connect(rename, &QAction::triggered, this, [this, tool] { renameToolAt(tool); });

        auto* duplicate = menu.addAction(tr("Duplicar"));
        duplicate->setToolTip(tr("Una copia con la misma tolerancia, desplazada un poco\n"
                                 "para que se vea que son dos."));
        connect(duplicate, &QAction::triggered, this, [this, tool] {
            video_->setSelectedIndex(tool);
            onDuplicateToolClicked();
        });

        auto* copy = menu.addAction(tr("Copiar lo que mide"));
        copy->setToolTip(tr("El último valor medido, al portapapeles, para pegarlo\n"
                            "en un correo o en una hoja de cálculo."));
        connect(copy, &QAction::triggered, this, [this, tool] { copyReadingAt(tool); });

        // LO DESTRUCTIVO, AL FINAL Y DETRÁS DE UNA SEPARACIÓN.
        menu.addSeparator();
        auto* remove = menu.addAction(tr("Borrar «%1»").arg(name));
        remove->setToolTip(tr("Ctrl+Z la devuelve."));
        connect(remove, &QAction::triggered, this, [this, tool] { deleteToolAt(tool); });
    } else {
        auto* header = menu.addAction(tr("Aquí no hay ninguna cota"));
        header->setEnabled(false);
        menu.addSeparator();

        // MARCAR EL RASGO AQUÍ, en el punto exacto donde se ha pulsado.
        //
        // Hasta ahora marcar el rasgo distintivo era un MODO: se pulsaba un
        // botón, el programa se quedaba esperando, y el siguiente clic contaba.
        // Dos gestos y un estado invisible en medio para poner un punto. Aquí ya
        // se sabe dónde quiere ponerlo el operador: ha pulsado justo ahí.
        auto* anchor = menu.addAction(tr("Marcar aquí el rasgo distintivo"));
        anchor->setToolTip(
            tr("Fija la orientación cuando la pieza es simétrica o puede llegar girada "
               "180°."));
        anchor->setWhatsThis(
            tr("Elige un punto que solo exista en un sitio: un agujero, una marca, una "
               "esquina achaflanada. Solo se aplica si «seguir la rotación» está "
               "encendido."));
        anchor->setEnabled(streaming_ && liveFixture_.has_value());
        if (!anchor->isEnabled()) {
            anchor->setToolTip(tr("Necesita vídeo en vivo con la pieza detectada."));
        }
        connect(anchor, &QAction::triggered, this,
                [this, imagePoint] { markAnchorAt(imagePoint); });

        menu.addSeparator();
        auto* fit = menu.addAction(tr("Ajustar a la ventana"));
        connect(fit, &QAction::triggered, video_, &inspection::EditorCanvas::resetView);
        auto* actual = menu.addAction(tr("Píxeles reales (100 %)"));
        actual->setToolTip(tr("Un píxel de la imagen, un píxel de la pantalla: es la\n"
                              "única vista en la que lo que ves es lo que se mide."));
        connect(actual, &QAction::triggered, video_,
                &inspection::EditorCanvas::zoomToActualPixels);
    }

    menu.exec(globalPos);
}

void MainWindow::onDuplicateToolClicked() {
    const int index = video_->selectedIndex();
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        statusBar()->showMessage(tr("Selecciona una herramienta para duplicar."));
        return;
    }
    // Copia con un pequeño desplazamiento y nombre nuevo; id = -1 = aún sin
    // guardar en la BD (se persiste al guardar la plantilla, como las nuevas).
    inspection::EditedTool tool = liveTools_[static_cast<std::size_t>(index)];
    tool.config.id = -1;
    inspection::translateGeometry(tool.geometry, {15.0F, 15.0F});
    ++toolNameCounter_;
    tool.config.name = (typeLabel(tool.config.type) +
                        QStringLiteral(" %1").arg(toolNameCounter_))
                           .toStdString();
    tool.config.geometryJson = inspection::toJson(tool.geometry);

    liveTools_.push_back(std::move(tool));
    commitUndoState();
    video_->clearResults();
    const int newIndex = static_cast<int>(liveTools_.size()) - 1;
    video_->setSelectedIndex(newIndex);
    onLiveSelectionChanged(newIndex);
    statusBar()->showMessage(tr("Herramienta duplicada."));
}

void MainWindow::deleteToolAt(int index) {
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    const auto& tool = liveTools_[static_cast<std::size_t>(index)];
    if (tool.config.id >= 0 && repos_.tools != nullptr) {
        if (auto removed = repos_.tools->remove(tool.config.id); !removed.isOk()) {
            statusBar()->showMessage(QString::fromStdString(removed.error().message));
            return;
        }
    }
    liveTools_.erase(liveTools_.begin() + index);
    commitUndoState();
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
    statusBar()->showMessage(tr("Herramienta eliminada."));
}

}  // namespace pci::ui
