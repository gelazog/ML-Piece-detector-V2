#include "inspection_editor/editor_window.h"

#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include "vision/contour_analysis.h"
#include <variant>

#include "camera/camera_controller.h"
#include "camera/frame_utils.h"
#include "core/logging.h"
#include "inspection_editor/auto_measure.h"
#include "inspection_editor/auto_measure_dialog.h"
#include "repositories/piece_repository.h"
#include "inspection_editor/canvas/tool_icons.h"
#include "inspection_editor/canvas/tool_palette.h"
#include "inspection_editor/execution/tool_executor.h"
#include "repositories/tool_repository.h"
#include "vision/geometry_features.h"
#include "vision/pipeline.h"
#include "vision/position_fixture.h"

namespace pci::inspection {

namespace {

// Nombre corto de la lista única compartida con la vista en vivo, envuelto en
// QString.
QString typeLabel(ToolType type) { return QString::fromUtf8(toolTypeLabel(type)); }

}  // namespace

EditorWindow::EditorWindow(const QImage& reference, const vision::Fixture& fixture,
                           std::int64_t pieceId, repositories::ToolRepository* repo,
                           domain::ScaleCalibration calibration,
                           const std::string& templateName, QWidget* parent,
                           const std::vector<EditedTool>* initialTools,
                           camera::CameraController* liveController,
                           vision::PipelineConfig pipeline)
    : QDialog(parent), reference_(reference), fixture_(fixture), pieceId_(pieceId),
      repo_(repo), calibration_(calibration), templateName_(templateName),
      liveController_(liveController), pipeline_(std::move(pipeline)) {
    setWindowTitle(tr("Editor de plantilla '%1'")
                       .arg(QString::fromStdString(templateName)));
    resize(1100, 700);

    auto* rootLayout = new QHBoxLayout(this);

    // Paleta agrupada por familias (izquierda). Antes eran quince botones en
    // columna, ~440 px de alto; con las herramientas que quedan por añadir no
    // cabrían.
    //
    // El acordeón enseñaba una familia a la vez pero gastaba una fila entera de
    // alto por herramienta: con 32, una familia grande ya no cabía sin barra de
    // desplazamiento. El panel las pone en rejilla, así que la familia mayor
    // ocupa dos filas en vez de ocho, y se ven todas a la vez — que es lo que
    // hace que se pueda elegir en un vistazo en vez de leyendo.
    palette_ = new ToolPalette(this);
    // 190 era lo que pedía el acordeón por su texto vertical. El panel cabe en
    // 176 —medido: es el ancho de la franja de familias— y por debajo de eso no
    // encoge nada, así que ponerle menos solo dejaría hueco muerto.
    palette_->setMinimumWidth(180);
    rootLayout->addWidget(palette_);

    // Canvas (centro).
    canvas_ = new EditorCanvas(this);
    canvas_->setScene(reference_, fixture_);
    canvas_->setTools(&tools_);
    canvas_->setMmPerPixel(calibration_.mmPerPixel);

    // Canvas + barra de vista (Z3): los mismos controles que en la ventana
    // principal, para que el zoom se maneje igual en los dos sitios.
    auto* centerLayout = new QVBoxLayout();
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addWidget(canvas_, 1);
    auto* zoomLayout = new QHBoxLayout();
    zoomLayout->setSpacing(2);
    zoomLayout->addStretch(1);
    auto* zoomLabel = new QLabel(this);
    zoomLabel->setMinimumWidth(52);
    zoomLabel->setAlignment(Qt::AlignCenter);
    zoomLabel->setToolTip(tr("Zoom actual. Rueda = acercar/alejar hacia el cursor,\n"
                             "botón central o Ctrl + arrastrar = mover la vista,\n"
                             "doble clic = ajustar a la ventana."));
    auto makeZoomButton = [this, zoomLayout](const QString& text, const QString& tip,
                                             auto slot) {
        auto* button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, this, slot);
        zoomLayout->addWidget(button);
        return button;
    };
    auto* zoomMin = makeZoomButton(QStringLiteral("⤢"),
                                   tr("Zoom mínimo: ajustar a la ventana (Ctrl+0)"),
                                   [this] { canvas_->zoomToMin(); });
    auto* zoomOut = makeZoomButton(QStringLiteral("−"), tr("Alejar (Ctrl+-)"),
                                   [this] { canvas_->zoomOut(); });
    zoomLayout->addWidget(zoomLabel);
    auto* zoomIn = makeZoomButton(QStringLiteral("+"), tr("Acercar (Ctrl++)"),
                                  [this] { canvas_->zoomIn(); });
    auto* zoomMax = makeZoomButton(QStringLiteral("⛶"), tr("Zoom máximo (Ctrl+2)"),
                                   [this] { canvas_->zoomToMax(); });
    zoomLayout->addStretch(1);
    centerLayout->addLayout(zoomLayout);
    rootLayout->addLayout(centerLayout, 1);

    auto updateZoom = [this, zoomLabel, zoomMin, zoomOut, zoomIn, zoomMax] {
        const double scale = canvas_->displayScale();
        const bool hasImage = scale > 0.0;
        zoomLabel->setText(hasImage ? tr("%1 %").arg(qRound(scale * 100.0))
                                    : QStringLiteral("—"));
        zoomMin->setEnabled(hasImage && !canvas_->atMinZoom());
        zoomOut->setEnabled(hasImage && !canvas_->atMinZoom());
        zoomIn->setEnabled(hasImage && !canvas_->atMaxZoom());
        zoomMax->setEnabled(hasImage && !canvas_->atMaxZoom());
    };
    connect(canvas_, &EditorCanvas::viewChanged, this, updateZoom);
    updateZoom();

    // Atajos de vista dentro del editor (mismas teclas que en la principal).
    for (const auto& [sequence, action] :
         std::initializer_list<std::pair<QKeySequence, std::function<void()>>>{
             {QKeySequence::ZoomIn, [this] { canvas_->zoomIn(); }},
             {QKeySequence::ZoomOut, [this] { canvas_->zoomOut(); }},
             {QKeySequence(Qt::CTRL | Qt::Key_0), [this] { canvas_->zoomToMin(); }},
             {QKeySequence(Qt::CTRL | Qt::Key_1), [this] { canvas_->zoomToActualPixels(); }},
             {QKeySequence(Qt::CTRL | Qt::Key_2), [this] { canvas_->zoomToMax(); }}}) {
        auto* shortcutAction = new QAction(this);
        shortcutAction->setShortcut(sequence);
        connect(shortcutAction, &QAction::triggered, this, action);
        addAction(shortcutAction);
    }

    // Panel derecho: lista + propiedades + acciones.
    auto* sideLayout = new QVBoxLayout();
    sideLayout->addWidget(new QLabel(tr("Herramientas:"), this));
    list_ = new QListWidget(this);
    list_->setMinimumWidth(260);
    sideLayout->addWidget(list_, 1);

    auto* form = new QFormLayout();
    nameEdit_ = new QLineEdit(this);
    form->addRow(tr("Nombre:"), nameEdit_);
    tolMin_ = new QDoubleSpinBox(this);
    tolMin_->setRange(0.0, 100000.0);
    tolMin_->setDecimals(2);
    form->addRow(tr("Tolerancia mín:"), tolMin_);
    tolMax_ = new QDoubleSpinBox(this);
    tolMax_->setRange(0.0, 100000.0);
    tolMax_->setDecimals(2);
    form->addRow(tr("Tolerancia máx:"), tolMax_);
    tolMmLabel_ = new QLabel(this);
    form->addRow(QString(), tolMmLabel_);
    paramLabel_ = new QLabel(tr("Puntos:"), this);
    paramSpin_ = new QSpinBox(this);
    paramSpin_->setRange(1, 1000);
    paramSpin_->setToolTip(
        tr("Cantidad de puntos de muestreo de la herramienta:\n"
           "Calibre: grosor de banda promediada (px)\n"
           "Círculo: rayos de búsqueda del borde\n"
           "Borde liso: escaneos perpendiculares\n"
           "Blob: área mínima de cada mancha (px²)"));
    form->addRow(paramLabel_, paramSpin_);

    // Construcciones geométricas (X1). Los tres desplegables solo se habilitan
    // con un Punto o una Recta construidos seleccionados: en cualquier otra
    // herramienta prometerían algo que esa herramienta ignora.
    constructionCombo_ = new QComboBox(this);
    constructionCombo_->setToolTip(
        tr("Qué se construye a partir de las referencias. No se mide nada:\n"
           "el resultado existe para que otras herramientas lo usen de datum."));
    choiceLabel_ = new QLabel(tr("Construcción:"), this);
    form->addRow(choiceLabel_, constructionCombo_);
    ref1Label_ = new QLabel(tr("1ª referencia:"), this);
    ref1Combo_ = new QComboBox(this);
    form->addRow(ref1Label_, ref1Combo_);
    ref2Label_ = new QLabel(tr("2ª referencia:"), this);
    ref2Combo_ = new QComboBox(this);
    form->addRow(ref2Label_, ref2Combo_);
    if (calibration_.valid()) {
        auto* scaleHint = new QLabel(
            tr("Escala calibrada: 1 px ≈ %1 mm (tolerancias en px)")
                .arg(calibration_.mmPerPixel, 0, 'f', 4),
            this);
        scaleHint->setWordWrap(true);
        form->addRow(scaleHint);
    }
    sideLayout->addLayout(form);

    // «Eliminar herramienta» se mudó DENTRO de la paleta, junto a Mover/Elegir:
    // es la continuación del mismo gesto —eliges y quitas— y tenerlo al otro
    // extremo de la columna obligaba a un viaje de ida y vuelta.

    refreshButton_ = new QPushButton(tr("Actualizar desde cámara"), this);
    refreshButton_->setToolTip(
        tr("Recaptura una imagen fresca de la cámara en marcha y reanaliza la\n"
           "pieza, sin cerrar el editor. Las herramientas siguen a la pieza."));
    refreshButton_->setEnabled(liveController_ != nullptr);
    sideLayout->addWidget(refreshButton_);

    auto* autoButton = new QPushButton(tr("Medir automáticamente…"), this);
    autoButton->setToolTip(
        tr("Mide la pieza sola y propone las cotas que encuentra: dimensiones\n"
           "generales, agujeros, redondeos, espesores entre caras paralelas y\n"
           "ángulos de esquina. Se revisan antes de añadirlas."));
    sideLayout->addWidget(autoButton);

    contourButton_ = new QPushButton(tr("Ver contorno"), this);
    contourButton_->setCheckable(true);
    contourButton_->setToolTip(
        tr("Dibuja el contorno detectado sobre la imagen, separando en colores\n"
           "los tramos rectos de los arcos (con su radio), marcando los agujeros\n"
           "y resumiendo perímetro, área y envolvente."));
    sideLayout->addWidget(contourButton_);

    auto* exportButton = new QPushButton(tr("Exportar contorno a CSV…"), this);
    exportButton->setToolTip(
        tr("Guarda los puntos del contorno y de sus agujeros en un CSV para\n"
           "abrirlo en un CAD. En mm si hay calibración; si no, en píxeles."));
    sideLayout->addWidget(exportButton);

    auto* testButton = new QPushButton(tr("Probar sobre esta imagen"), this);
    sideLayout->addWidget(testButton);

    auto* saveButton = new QPushButton(tr("Guardar plantilla"), this);
    saveButton->setEnabled(repo_ != nullptr);
    if (repo_ == nullptr) {
        saveButton->setToolTip(tr("Base de datos no disponible"));
    }
    sideLayout->addWidget(saveButton);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    sideLayout->addWidget(statusLabel_);

    rootLayout->addLayout(sideLayout);

    connect(palette_, &ToolPalette::toolChosen, this,
            [this](std::optional<ToolType> type) { canvas_->setCreateType(type); });
    connect(canvas_, &EditorCanvas::toolCreated, this, &EditorWindow::onToolCreated);
    connect(canvas_, &EditorCanvas::selectionChanged, this, &EditorWindow::onCanvasSelection);
    connect(canvas_, &EditorCanvas::toolModified, this, [this] {
        canvas_->clearResults();
        commitUndoState();
    });
    connect(canvas_, &EditorCanvas::traceRejected, this,
            [this](const QString& reason) { statusLabel_->setText(reason); });
    connect(list_, &QListWidget::currentRowChanged, this, &EditorWindow::onListRowChanged);
    connect(nameEdit_, &QLineEdit::editingFinished, this, &EditorWindow::onPanelEdited);
    connect(tolMin_, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onPanelEdited);
    connect(tolMax_, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onPanelEdited);
    connect(paramSpin_, &QSpinBox::valueChanged, this, &EditorWindow::onPanelEdited);
    connect(constructionCombo_, &QComboBox::currentIndexChanged, this,
            &EditorWindow::onPanelEdited);
    connect(ref1Combo_, &QComboBox::currentIndexChanged, this, &EditorWindow::onPanelEdited);
    connect(ref2Combo_, &QComboBox::currentIndexChanged, this, &EditorWindow::onPanelEdited);
    // Los dos botones de borrar viven en la paleta, junto a Mover/Elegir, y son
    // los mismos que en la ventana principal: es el mismo panel compartido, así
    // que el gesto se aprende una vez.
    connect(palette_, &ToolPalette::deleteRequested, this, &EditorWindow::onDeleteClicked);
    connect(palette_, &ToolPalette::deleteAllRequested, this,
            &EditorWindow::onDeleteAllClicked);
    connect(autoButton, &QPushButton::clicked, this, &EditorWindow::onAutoMeasureClicked);
    connect(contourButton_, &QPushButton::toggled, this,
            &EditorWindow::onShowContourToggled);
    connect(exportButton, &QPushButton::clicked, this,
            &EditorWindow::onExportContourClicked);
    connect(testButton, &QPushButton::clicked, this, &EditorWindow::onTestClicked);
    connect(saveButton, &QPushButton::clicked, this, &EditorWindow::onSaveClicked);
    connect(refreshButton_, &QPushButton::clicked, this,
            &EditorWindow::onRefreshFromCamera);
    // Mantener el último frame de la cámara en marcha para el refresco bajo
    // demanda (no se muestra en vivo: la imagen del editor solo cambia al pulsar
    // "Actualizar desde cámara", así las tolerancias no bailan mientras se ajusta).
    if (liveController_ != nullptr) {
        connect(liveController_, &camera::CameraController::frameReady, this,
                [this](const QImage& frame) { latestLiveFrame_ = frame; });
    }

    // Con herramientas iniciales (las de la vista en vivo) arrancamos de ellas;
    // si no, se cargan de la BD como siempre.
    if (initialTools != nullptr) {
        tools_ = *initialTools;
        nameCounter_ = static_cast<int>(tools_.size());
    } else {
        loadExistingTools();
    }
    stableTools_ = tools_;
    refreshList();
    syncPanelFromSelection();

    // Atajos estándar dentro del editor (fijos; los configurables viven en la
    // ventana principal): Ctrl+Z / Ctrl+Y deshacen dibujo, movimiento,
    // borrado y ediciones del panel; Supr borra la selección.
    auto* undoAction = new QAction(tr("Deshacer"), this);
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, [this] { applyUndoRedo(false); });
    addAction(undoAction);
    auto* redoAction = new QAction(tr("Rehacer"), this);
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, [this] { applyUndoRedo(true); });
    addAction(redoAction);
    auto* deleteAction = new QAction(tr("Eliminar herramienta"), this);
    deleteAction->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(deleteAction, &QAction::triggered, this, &EditorWindow::onDeleteClicked);
    addAction(deleteAction);

    // Duplicar (Ctrl+D) y copiar/pegar (Ctrl+C / Ctrl+V). El portapapeles es de
    // proceso, así que copiar y reabrir el editor en otra plantilla de la misma
    // pieza permite pegar allí la herramienta.
    auto* duplicateAction = new QAction(tr("Duplicar herramienta"), this);
    duplicateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(duplicateAction, &QAction::triggered, this,
            [this] { duplicateSelected(); });
    addAction(duplicateAction);
    auto* copyAction = new QAction(tr("Copiar herramienta"), this);
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, [this] { copySelected(); });
    addAction(copyAction);
    auto* pasteAction = new QAction(tr("Pegar herramienta"), this);
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, [this] { pasteClipboard(); });
    addAction(pasteAction);

    // Con herramientas cargadas, medir de entrada: las medidas se ven sin
    // tener que pulsar Probar.
    if (!tools_.empty()) {
        onTestClicked();
    }
}

void EditorWindow::commitUndoState() {
    undoStack_.push(stableTools_);
    stableTools_ = tools_;
}

void EditorWindow::applyUndoRedo(bool redo) {
    auto state = redo ? undoStack_.redo(tools_) : undoStack_.undo(tools_);
    if (!state.has_value()) {
        return;
    }
    tools_ = std::move(*state);
    stableTools_ = tools_;
    canvas_->setSelectedIndex(-1);
    canvas_->clearResults();
    canvas_->update();
    refreshList();
    syncPanelFromSelection();
    statusLabel_->setText(redo ? tr("Rehecho.") : tr("Deshecho."));
}

void EditorWindow::loadExistingTools() {
    if (repo_ == nullptr) {
        return;
    }
    auto listed = repo_->listForPiece(pieceId_, templateName_);
    if (!listed.isOk()) {
        statusLabel_->setText(tr("No se pudieron cargar las herramientas: %1")
                                  .arg(QString::fromStdString(listed.error().message)));
        return;
    }
    for (auto& config : listed.value()) {
        auto geometry = geometryFromJson(config.type, config.geometryJson);
        if (!geometry.isOk()) {
            core::logWarning("Herramienta '" + config.name +
                             "' con geometría corrupta: " + geometry.error().message);
            continue;
        }
        EditedTool tool;
        tool.config = std::move(config);
        tool.geometry = std::move(geometry.value());
        tools_.push_back(std::move(tool));
        ++nameCounter_;
    }
}

int EditorWindow::listRowToToolIndex(int row) const {
    int visible = -1;
    for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
        if (!tools_[static_cast<std::size_t>(i)].deleted) {
            ++visible;
            if (visible == row) {
                return i;
            }
        }
    }
    return -1;
}

void EditorWindow::refreshList() {
    syncing_ = true;
    list_->clear();
    for (const auto& tool : tools_) {
        if (!tool.deleted) {
            list_->addItem(typeLabel(tool.config.type) + QStringLiteral(" — ") +
                           QString::fromStdString(tool.config.name));
        }
    }
    syncing_ = false;
}

void EditorWindow::syncPanelFromSelection() {
    syncing_ = true;
    const int index = canvas_->selectedIndex();
    const bool hasSelection = index >= 0 && index < static_cast<int>(tools_.size());
    // Los dos botones de borrar de la paleta saben si tienen algo que hacer.
    // `tools_` guarda las borradas con una marca, así que se cuentan las vivas.
    int alive = 0;
    for (const auto& tool : tools_) {
        if (!tool.deleted) {
            ++alive;
        }
    }
    palette_->setDeletable(static_cast<int>(canvas_->selectedIndices().size()), alive);
    nameEdit_->setEnabled(hasSelection);
    tolMin_->setEnabled(hasSelection);
    tolMax_->setEnabled(hasSelection);

    paramSpin_->setEnabled(false);
    paramLabel_->setText(tr("Puntos:"));
    if (hasSelection) {
        const auto& tool = tools_[static_cast<std::size_t>(index)];
        nameEdit_->setText(QString::fromStdString(tool.config.name));
        tolMin_->setValue(tool.config.toleranceMin);
        tolMax_->setValue(tool.config.toleranceMax);
        if (calibration_.valid() && tool.config.type != ToolType::Blob &&
            tool.config.type != ToolType::LineToLine &&
            tool.config.type != ToolType::Angle &&
            tool.config.type != ToolType::PolyBlob) {
            tolMmLabel_->setText(tr("= %1 – %2 mm")
                                     .arg(calibration_.toMm(tool.config.toleranceMin), 0,
                                          'f', 2)
                                     .arg(calibration_.toMm(tool.config.toleranceMax), 0,
                                          'f', 2));
        } else {
            tolMmLabel_->clear();
        }

        // Parámetro de muestreo según el tipo de herramienta.
        std::visit(
            [this](const auto& g) {
                using T = std::decay_t<decltype(g)>;
                if constexpr (std::is_same_v<T, CaliperGeometry>) {
                    paramLabel_->setText(tr("Banda (px):"));
                    paramSpin_->setValue(static_cast<int>(g.bandWidth));
                    paramSpin_->setEnabled(true);
                } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                    paramLabel_->setText(tr("Rayos:"));
                    paramSpin_->setValue(g.rayCount);
                    paramSpin_->setEnabled(true);
                } else if constexpr (std::is_same_v<T, EdgeFlawGeometry>) {
                    paramLabel_->setText(tr("Escaneos:"));
                    paramSpin_->setValue(g.scanCount);
                    paramSpin_->setEnabled(true);
                } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                    paramLabel_->setText(tr("Área mín (px²):"));
                    paramSpin_->setValue(static_cast<int>(g.minArea));
                    paramSpin_->setEnabled(true);
                }
                // PointToLine no tiene parámetro de muestreo editable.
            },
            tool.geometry);
        syncConstructionPanel(&tool);
    } else {
        nameEdit_->clear();
        syncConstructionPanel(nullptr);
    }
    syncing_ = false;
}

void EditorWindow::syncConstructionPanel(const EditedTool* tool) {
    constructionCombo_->clear();
    ref1Combo_->clear();
    ref2Combo_->clear();
    const bool isConstruction =
        tool != nullptr && (tool->config.type == ToolType::ConstructedPoint ||
                            tool->config.type == ToolType::ConstructedLine);
    // Qué medida elige esta herramienta lo dice el MODELO, no el panel. Antes
    // aquí ponía «¿es una Región?», y con esa pregunta Ranura, Chaflán, Acuerdo
    // y Máx./mín. se quedaban sin desplegable: publican de dos a tres números y
    // la tolerancia vigilaba siempre el primero de su enum, sin forma de decir
    // cuál era la cota. Es el mismo error que ya se corrigió con las
    // referencias, y preguntando al modelo no puede repetirse con la siguiente.
    const MeasureChoices measures =
        tool != nullptr ? measureChoicesOf(tool->geometry) : MeasureChoices{};
    const bool choosesMeasure = !measures.options.empty();
    constructionCombo_->setEnabled(isConstruction || choosesMeasure);
    choiceLabel_->setEnabled(isConstruction || choosesMeasure);

    // Qué referencias admite esta herramienta lo dice el MODELO, no el panel.
    // Antes lo decidía aquí preguntándose «¿es una construcción?», y con esa
    // pregunta Posición, Orientación y Desviación de centros se quedaban sin
    // desplegables: medían contra una referencia que no había forma de
    // asignarles desde el editor.
    std::array<OperandKind, 2> kinds{OperandKind::Unused, OperandKind::Unused};
    if (tool != nullptr) {
        kinds = referenceOperandsOf(tool->geometry);
    }

    // La Región usa el mismo desplegable para elegir QUÉ mide. Es el mismo
    // gesto —una opción discreta de la herramienta— y darle un control propio
    // habría dejado dos filas que nunca se ven a la vez.
    if (choosesMeasure) {
        choiceLabel_->setText(tr("Medida:"));
        for (const auto& option : measures.options) {
            constructionCombo_->addItem(QString::fromStdString(option.label), option.value);
        }
        constructionCombo_->setCurrentIndex(constructionCombo_->findData(measures.current));
    } else if (isConstruction) {
        choiceLabel_->setText(tr("Construcción:"));
        // Los modos que ofrece este tipo, con su valor guardado como dato para
        // no depender del orden del desplegable.
        if (tool->config.type == ToolType::ConstructedPoint) {
            const auto& g = std::get<ConstructedPointGeometry>(tool->geometry);
            for (const auto mode : allPointConstructions()) {
                constructionCombo_->addItem(QString::fromUtf8(constructionLabel(mode)),
                                            static_cast<int>(mode));
            }
            constructionCombo_->setCurrentIndex(
                constructionCombo_->findData(static_cast<int>(g.mode)));
        } else {
            const auto& g = std::get<ConstructedLineGeometry>(tool->geometry);
            for (const auto mode : allLineConstructions()) {
                constructionCombo_->addItem(QString::fromUtf8(constructionLabel(mode)),
                                            static_cast<int>(mode));
            }
            constructionCombo_->setCurrentIndex(
                constructionCombo_->findData(static_cast<int>(g.mode)));
        }
    } else {
        choiceLabel_->setText(tr("Construcción:"));
    }

    // Las candidatas son TODAS las demás herramientas, no solo las que hoy
    // ofrecen un elemento. Filtrar aquí exigiría una segunda tabla de "qué
    // produce cada tipo" que acabaría discrepando de la que usa el ejecutor; en
    // vez de eso, la etiqueta dice qué hace falta y el resultado lo dice claro
    // si no encaja.
    const auto fill = [this, tool](QComboBox* combo, OperandKind kind,
                                   const std::string& current) {
        combo->addItem(tr("— ninguna —"), QString());
        if (tool != nullptr) {
            for (const auto& other : tools_) {
                if (other.deleted || &other == tool || other.config.name.empty()) {
                    continue;
                }
                const QString name = QString::fromStdString(other.config.name);
                combo->addItem(name, name);
            }
        }
        const int index = combo->findData(QString::fromStdString(current));
        combo->setCurrentIndex(index >= 0 ? index : 0);
        combo->setEnabled(kind != OperandKind::Unused);
    };
    fill(ref1Combo_, kinds[0], tool != nullptr ? tool->config.reference : std::string());
    fill(ref2Combo_, kinds[1], tool != nullptr ? tool->config.reference2 : std::string());

    const auto label = [](OperandKind kind, const QString& prefix) {
        if (kind == OperandKind::Unused) {
            return prefix + QObject::tr(" (no se usa):");
        }
        return prefix + " (" + QString::fromUtf8(operandKindLabel(kind)) + "):";
    };
    ref1Label_->setText(label(kinds[0], tr("1ª referencia")));
    ref2Label_->setText(label(kinds[1], tr("2ª referencia")));
    ref1Label_->setEnabled(kinds[0] != OperandKind::Unused);
    ref2Label_->setEnabled(kinds[1] != OperandKind::Unused);
}

void EditorWindow::applyConstructionPanel(EditedTool& tool) {
    if (!constructionCombo_->isEnabled() || constructionCombo_->currentIndex() < 0) {
        return;
    }
    const int mode = constructionCombo_->currentData().toInt();
    // La medida la escribe el modelo, que sabe cuál de las cinco herramientas
    // es y valida que el valor sea suyo. Aquí solo quedan las construcciones,
    // que son las que este desplegable comparte.
    if (!setMeasureChoice(tool.geometry, mode)) {
        std::visit(
            [mode](auto& g) {
                using T = std::decay_t<decltype(g)>;
                if constexpr (std::is_same_v<T, ConstructedPointGeometry>) {
                    g.mode = static_cast<PointConstruction>(mode);
                } else if constexpr (std::is_same_v<T, ConstructedLineGeometry>) {
                    g.mode = static_cast<LineConstruction>(mode);
                }
            },
            tool.geometry);
    }
    // Solo se tocan las referencias si esta herramienta las usa: la Región
    // comparte el desplegable de arriba pero no tiene referencias, y
    // escribirlas a ciegas las borraría.
    if (ref1Combo_->isEnabled()) {
        tool.config.reference = ref1Combo_->currentData().toString().toStdString();
        tool.config.reference2 = ref2Combo_->currentData().toString().toStdString();
    }
}

// LA PIEZA QUE ESTE EDITOR EDITA, y no la mayor del encuadre.
//
// El editor se abre SOBRE UNA PIEZA: recibe su fixture y guarda las
// herramientas en sus coordenadas. Pero cada vez que necesitaba volver a mirar
// la imagen llamaba a `analyzeFrame`, que devuelve la mayor y no sabe nada de
// eso. Con varias piezas en la mesa, la medición automática proponía las cotas
// de la MAYOR y las anclaba al fixture de la que se está editando: cotas de una
// pieza dibujadas sobre otra.
//
// Es la queja del taller: «si hay más de una pieza y se usa la automedición,
// esta toma una medición para todas las piezas, en lugar de una medición
// independiente por pieza».
//
// Se reconoce por CERCANÍA del origen del fixture, no por índice. El índice
// cambia en cuanto una pieza entra o sale del encuadre, y el editor puede estar
// abierto mientras la cámara sigue dando frames; el sitio, no. Si no hay
// ninguna cerca —la pieza salió de cuadro— se vuelve a la mayor en vez de no
// dar nada.
core::Result<vision::PieceAnalysis> EditorWindow::analyseEditedPiece(
    const cv::Mat& image) const {
    auto all = vision::analyzeFrames(image, pipeline_);
    if (!all.isOk()) {
        return core::Result<vision::PieceAnalysis>::err(all.error().message);
    }
    if (all.value().empty()) {
        return core::Result<vision::PieceAnalysis>::err("No se detectó ninguna pieza");
    }
    std::size_t nearest = vision::largestPieceIndex(all.value());
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < all.value().size(); ++i) {
        const cv::Point2f delta = all.value()[i].fixture.origin - fixture_.origin;
        const double distance = std::hypot(delta.x, delta.y);
        if (distance < best) {
            best = distance;
            nearest = i;
        }
    }
    // Un tope, para que «la más cercana» no acabe siendo una pieza del otro
    // extremo de la mesa cuando la del editor ya no está: la diagonal del
    // encuadre partida por cuatro. Más allá de eso no se está reconociendo
    // nada, se está cogiendo lo que queda.
    const double tooFar = std::hypot(image.cols, image.rows) / 4.0;
    if (best > tooFar) {
        nearest = vision::largestPieceIndex(all.value());
    }
    return core::Result<vision::PieceAnalysis>::ok(std::move(all.value()[nearest]));
}

void EditorWindow::onAutoMeasureClicked() {
    const cv::Mat image = camera::qImageToMat(reference_);
    const auto analysis = analyseEditedPiece(image);
    if (!analysis.isOk()) {
        statusLabel_->setText(tr("No se puede medir sola: no se detecta la pieza (%1)")
                                  .arg(QString::fromStdString(analysis.error().message)));
        return;
    }

    // Se mide con el fixture CON EL QUE SE EDITA, no con el que acaba de salir
    // del análisis: las herramientas se guardan en coordenadas de pieza, y si
    // se usara otro sistema las propuestas quedarían desplazadas respecto a las
    // que ya hay en la plantilla.
    int dropped = 0;
    // Con los agujeros de vuelta: la máscara del análisis viene rellena, y sin
    // esto a una arandela no se le propondría el diámetro interior.
    const cv::Mat mask = vision::pieceMaskWithHoles(image, analysis.value().mask,
                                                    pipeline_.segmentation);
    const auto proposals =
        proposeTools(image, mask, fixture_, {}, calibration_.mmPerPixel, &dropped);
    if (proposals.empty()) {
        statusLabel_->setText(
            tr("No se encontró ninguna cota que proponer sobre esta imagen."));
        return;
    }
    // Y si el tope dejó cotas fuera, se dice. Descartarlas en silencio deja al
    // operador creyendo que la pieza no tenía más, que es lo contrario de lo
    // que pasó.
    if (dropped > 0) {
        statusLabel_->setText(
            tr("Se proponen las %1 cotas mayores; otras %2 más pequeñas se han dejado "
               "fuera para que la lista se pueda revisar.")
                .arg(proposals.size())
                .arg(dropped));
    }

    // El diálogo puede VOLVER A PROPONER cuando el operador cambia qué clases
    // de cota quiere. Se le pasa cómo hacerlo en vez de darle la imagen: así la
    // ventana sigue siendo la dueña de la imagen y del pipeline, y el diálogo
    // solo sabe pedir.
    //
    // Reproponer y no filtrar la lista: el recorte por el tope se aplica
    // DESPUÉS del filtro, así que esconder filas dejaría tres diámetros donde
    // podía haber doce.
    // Se le pasa una RECETA entera y no la lista de clases: la comprobación de
    // «esta receta no es para esta pieza» necesita la familia, y el diálogo
    // tiene que poder enseñar el motivo en vez de una tabla vacía.
    auto reproposer = [&](const MeasureRecipe& recipe) {
        return proposeWithRecipe(image, mask, fixture_, recipe, calibration_.mmPerPixel);
    };
    AutoMeasureDialog dialog(proposals, calibration_.mmPerPixel, this,
                             std::move(reproposer));
    // LA RECETA QUE ESTA PIEZA YA TENÍA. Sin esto, quien mide un lote de cien
    // engranajes elige «Engranaje» cien veces, que es la mitad de la petición
    // de uso —«de un lote o de una pieza»—.
    if (pieces_ != nullptr && pieceId_ >= 0) {
        auto stored = pieces_->loadMeasureRecipe(pieceId_);
        if (stored.isOk()) {
            dialog.selectRecipe(stored.value());
        }
    }
    if (dialog.exec() != QDialog::Accepted) {
        statusLabel_->setText(tr("Medición automática cancelada."));
        return;
    }
    // Y se recuerda la que dejó puesta. Se guarda al ACEPTAR y no al elegirla:
    // cancelar tiene que no haber pasado, también para esto.
    //
    // Se guarda el NOMBRE de la receta, no las casillas que el operador haya
    // ajustado a mano. Guardar el ajuste dejaría el desplegable diciendo
    // «Arandela» mientras las clases son otras, y eso se lee peor que no
    // recordarlo: la receta es lo que tiene nombre.
    if (pieces_ != nullptr && pieceId_ >= 0) {
        const auto chosen = dialog.chosenRecipe();
        if (auto saved = pieces_->saveMeasureRecipe(pieceId_, chosen.name); !saved.isOk()) {
            statusLabel_->setText(
                tr("No se pudo recordar la receta «%1»: %2")
                    .arg(QString::fromStdString(chosen.name))
                    .arg(QString::fromStdString(saved.error().message)));
        }
    }
    const auto accepted = dialog.accepted();
    if (accepted.empty()) {
        return;
    }

    // Todas de una vez y UN solo estado de deshacer: quitar siete herramientas
    // con siete Ctrl+Z sería peor que haberlas dibujado a mano.
    for (const auto& proposal : accepted) {
        EditedTool tool;
        tool.geometry = proposal.geometry;
        tool.config = proposal.config;
        tools_.push_back(std::move(tool));
    }
    commitUndoState();
    refreshList();
    canvas_->clearResults();
    canvas_->update();
    statusLabel_->setText(tr("Añadidas %1 medidas. Revisa sus tolerancias y guarda; "
                             "Ctrl+Z las quita todas de una vez.")
                              .arg(accepted.size()));
}

void EditorWindow::onToolCreated(const ToolGeometry& geometry) {
    EditedTool tool;
    tool.geometry = geometry;
    tool.config.type = typeOf(geometry);

    // El Perfil no se traza: su nominal ES el contorno de la pieza que hay
    // delante, capturado aquí y guardado dentro de la herramienta. Va en
    // coordenadas de pieza como todo lo demás, y por eso al medir no hace falta
    // alinear nada — el fixture ya lo hizo.
    if (tool.config.type == ToolType::Profile) {
        if (!ensureContourReport() || contour_.outer.size() < 8) {
            statusLabel_->setText(
                tr("No se puede crear un Perfil: hace falta ver el contorno de la pieza "
                   "en esta imagen, y ahora mismo no se detecta."));
            return;
        }
        ProfileGeometry profile;
        // Se remuestrea: el contorno crudo trae un punto por píxel y guardar
        // miles en la plantilla no aporta nada a la medida.
        const auto sampled = vision::resampleClosedContour(contour_.outer, 3.0);
        profile.nominal.reserve(sampled.size());
        for (const auto& p : sampled) {
            profile.nominal.push_back(vision::toPieceCoords(fixture_, p));
        }
        if (profile.nominal.size() < 8) {
            statusLabel_->setText(tr("El contorno de esta pieza es demasiado corto para "
                                     "servir de nominal."));
            return;
        }
        tool.geometry = profile;
    }
    ++nameCounter_;
    tool.config.name =
        (typeLabel(tool.config.type) + QStringLiteral(" %1").arg(nameCounter_)).toStdString();
    tool.config.geometryJson = toJson(tool.geometry);
    tool.config.toleranceMin = 0.0;
    tool.config.toleranceMax = 100000.0;

    // Medir de inmediato sobre la imagen de referencia y sugerir tolerancias:
    // la pieza buena define su propio rango de aceptación.
    const auto measured = runTool(camera::qImageToMat(reference_), fixture_, tool.config,
                                  calibration_.mmPerPixel);
    if (measured.isOk() && (measured.value().ok || measured.value().measured > 0.0)) {
        suggestTolerances(tool.geometry, measured.value().measured,
                          tool.config.toleranceMin, tool.config.toleranceMax);
        statusLabel_->setText(tr("%1 midió %2 — tolerancias sugeridas [%3, %4]; "
                                 "ajústalas si hace falta y Guardar")
                                  .arg(QString::fromStdString(tool.config.name))
                                  .arg(measured.value().measured, 0, 'f', 1)
                                  .arg(tool.config.toleranceMin, 0, 'f', 1)
                                  .arg(tool.config.toleranceMax, 0, 'f', 1));
    } else {
        statusLabel_->setText(
            tr("%1 creada, pero no midió sobre esta imagen (%2) — ajusta su posición")
                .arg(QString::fromStdString(tool.config.name),
                     QString::fromStdString(measured.isOk() ? measured.value().detail
                                                            : measured.error().message)));
    }
    tools_.push_back(std::move(tool));
    commitUndoState();

    canvas_->clearResults();
    canvas_->setSelectedIndex(static_cast<int>(tools_.size()) - 1);
    refreshList();
    syncPanelFromSelection();
}

namespace {
// Portapapeles de proceso: comparte una herramienta entre instancias del editor
// (p. ej. entre plantillas de la misma pieza).
std::optional<EditedTool> g_toolClipboard;
}  // namespace

void EditorWindow::addToolCopy(const ToolConfig& config, const ToolGeometry& geometry,
                               const cv::Point2f& offset) {
    EditedTool tool;
    tool.geometry = geometry;
    translateGeometry(tool.geometry, offset);
    tool.config = config;
    tool.config.id = -1;  // copia nueva: aún no guardada en la BD
    ++nameCounter_;
    tool.config.name =
        (typeLabel(config.type) + QStringLiteral(" %1").arg(nameCounter_)).toStdString();
    tool.config.geometryJson = toJson(tool.geometry);
    tool.deleted = false;

    tools_.push_back(std::move(tool));
    commitUndoState();
    canvas_->clearResults();
    canvas_->setSelectedIndex(static_cast<int>(tools_.size()) - 1);
    refreshList();
    syncPanelFromSelection();
    onTestClicked();
}

void EditorWindow::duplicateSelected() {
    const int index = canvas_->selectedIndex();
    if (index < 0 || index >= static_cast<int>(tools_.size())) {
        statusLabel_->setText(tr("Selecciona una herramienta para duplicar."));
        return;
    }
    const auto& src = tools_[static_cast<std::size_t>(index)];
    addToolCopy(src.config, src.geometry, {15.0F, 15.0F});
    statusLabel_->setText(tr("Herramienta duplicada."));
}

void EditorWindow::copySelected() {
    const int index = canvas_->selectedIndex();
    if (index < 0 || index >= static_cast<int>(tools_.size())) {
        statusLabel_->setText(tr("Selecciona una herramienta para copiar."));
        return;
    }
    g_toolClipboard = tools_[static_cast<std::size_t>(index)];
    statusLabel_->setText(tr("Herramienta copiada (Ctrl+V para pegar, también en "
                             "otra plantilla)."));
}

void EditorWindow::pasteClipboard() {
    if (!g_toolClipboard.has_value()) {
        statusLabel_->setText(tr("El portapapeles de herramientas está vacío."));
        return;
    }
    addToolCopy(g_toolClipboard->config, g_toolClipboard->geometry, {15.0F, 15.0F});
    statusLabel_->setText(tr("Herramienta pegada."));
}

void EditorWindow::onCanvasSelection(int index) {
    Q_UNUSED(index);
    syncPanelFromSelection();
}

void EditorWindow::onListRowChanged(int row) {
    if (syncing_) {
        return;
    }
    canvas_->setSelectedIndex(listRowToToolIndex(row));
    syncPanelFromSelection();
}

void EditorWindow::onPanelEdited() {
    if (syncing_) {
        return;
    }
    const int index = canvas_->selectedIndex();
    if (index < 0 || index >= static_cast<int>(tools_.size())) {
        return;
    }
    auto& tool = tools_[static_cast<std::size_t>(index)];
    const std::string newName = nameEdit_->text().trimmed().toStdString();
    if (!newName.empty()) {
        tool.config.name = newName;
    }
    tool.config.toleranceMin = tolMin_->value();
    tool.config.toleranceMax = tolMax_->value();
    if (calibration_.valid() && tool.config.type != ToolType::Blob &&
        tool.config.type != ToolType::LineToLine &&
        tool.config.type != ToolType::Angle &&
        tool.config.type != ToolType::PolyBlob) {
        tolMmLabel_->setText(tr("= %1 – %2 mm")
                                 .arg(calibration_.toMm(tolMin_->value()), 0, 'f', 2)
                                 .arg(calibration_.toMm(tolMax_->value()), 0, 'f', 2));
    } else {
        tolMmLabel_->clear();
    }
    if (paramSpin_->isEnabled()) {
        const int value = paramSpin_->value();
        std::visit(
            [value](auto& g) {
                using T = std::decay_t<decltype(g)>;
                if constexpr (std::is_same_v<T, CaliperGeometry>) {
                    g.bandWidth = static_cast<float>(value);
                } else if constexpr (std::is_same_v<T, CircleGeometry>) {
                    g.rayCount = value;
                } else if constexpr (std::is_same_v<T, EdgeFlawGeometry>) {
                    g.scanCount = value;
                } else if constexpr (std::is_same_v<T, BlobGeometry>) {
                    g.minArea = static_cast<float>(value);
                }
            },
            tool.geometry);
        canvas_->update();
    }
    applyConstructionPanel(tool);
    // Cambiar de construcción cambia QUÉ referencias hacen falta, así que los
    // desplegables se vuelven a montar. Sin esto, elegir "centro de un círculo"
    // dejaría a la vista una segunda referencia que ya no se usa.
    syncing_ = true;
    syncConstructionPanel(&tool);
    syncing_ = false;
    canvas_->update();
    commitUndoState();
    refreshList();
}

void EditorWindow::onDeleteClicked() {
    const auto indices = canvas_->selectedIndices();
    if (indices.empty()) {
        return;
    }
    for (const int index : indices) {
        if (index >= 0 && index < static_cast<int>(tools_.size())) {
            tools_[static_cast<std::size_t>(index)].deleted = true;
        }
    }
    commitUndoState();
    canvas_->setSelectedIndex(-1);
    canvas_->clearResults();
    refreshList();
    syncPanelFromSelection();
}

void EditorWindow::onDeleteAllClicked() {
    int alive = 0;
    for (const auto& tool : tools_) {
        if (!tool.deleted) {
            ++alive;
        }
    }
    if (alive == 0) {
        return;
    }

    // Se pregunta, y la pregunta DICE CUÁNTAS. «¿Seguro?» a secas no informa:
    // quien lleva media hora dibujando necesita el número para reconocer si es
    // el trabajo que cree o el de otra plantilla que abrió sin darse cuenta. Y
    // se dice que hay vuelta atrás, porque el miedo a un botón destructivo viene
    // de no saber si se puede deshacer.
    QMessageBox box(QMessageBox::Warning, tr("Borrar todas las herramientas"),
                    tr("Se van a borrar las %n herramienta(s) de esta plantilla.", nullptr,
                       alive),
                    QMessageBox::NoButton, this);
    box.setInformativeText(tr("Se puede deshacer con Ctrl+Z."));
    auto* confirm =
        box.addButton(tr("Borrar las %n", nullptr, alive), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);  // el defecto NUNCA es el destructivo
    box.exec();
    if (box.clickedButton() != confirm) {
        return;
    }

    for (auto& tool : tools_) {
        tool.deleted = true;
    }
    commitUndoState();
    canvas_->setSelectedIndex(-1);
    canvas_->clearResults();
    refreshList();
    syncPanelFromSelection();
}

std::vector<ToolConfig> EditorWindow::activeConfigs() const {
    std::vector<ToolConfig> configs;
    for (const auto& tool : tools_) {
        if (tool.deleted) {
            continue;
        }
        ToolConfig config = tool.config;
        config.geometryJson = toJson(tool.geometry);
        configs.push_back(std::move(config));
    }
    return configs;
}

void EditorWindow::onRefreshFromCamera() {
    if (latestLiveFrame_.isNull()) {
        statusLabel_->setText(tr("Aún no llega imagen de la cámara; espera un momento."));
        return;
    }
    const QImage frame = latestLiveFrame_;
    const auto analysis = analyseEditedPiece(camera::qImageToMat(frame));
    if (!analysis.isOk()) {
        statusLabel_->setText(tr("No se pudo detectar la pieza en la imagen nueva: %1")
                                  .arg(QString::fromStdString(analysis.error().message)));
        return;
    }
    // Nueva imagen de referencia + fixture; las herramientas (en coords de
    // pieza) siguen ancladas y se remiden sobre la imagen fresca.
    reference_ = frame;
    fixture_ = analysis.value().fixture;
    canvas_->setScene(reference_, fixture_);
    // El contorno que hubiera dibujado describe la foto anterior: dejarlo
    // encima sería enseñar el borde de una imagen sobre otra.
    invalidateContourReport();
    onTestClicked();
    statusLabel_->setText(tr("Imagen actualizada desde la cámara."));
}

bool EditorWindow::ensureContourReport() {
    if (contour_.valid) {
        return true;
    }
    const auto analysis = analyseEditedPiece(camera::qImageToMat(reference_));
    if (!analysis.isOk()) {
        statusLabel_->setText(tr("No se ve el contorno: no se detecta la pieza (%1)")
                                  .arg(QString::fromStdString(analysis.error().message)));
        return false;
    }
    contour_ = vision::describeContour(analysis.value().mask);
    if (!contour_.valid) {
        statusLabel_->setText(tr("La pieza detectada no tiene un contorno utilizable."));
        return false;
    }
    return true;
}

void EditorWindow::invalidateContourReport() {
    contour_ = {};
    canvas_->setContourReport(false);
    if (contourButton_ != nullptr) {
        const QSignalBlocker blocker(contourButton_);
        contourButton_->setChecked(false);
    }
}

void EditorWindow::onShowContourToggled(bool on) {
    if (!on) {
        canvas_->setContourReport(false);
        return;
    }
    if (!ensureContourReport()) {
        const QSignalBlocker blocker(contourButton_);
        contourButton_->setChecked(false);
        return;
    }
    canvas_->setContourReport(true, contour_);
    statusLabel_->setText(canvas_->contourSummaryLines().join(QStringLiteral("\n")));
}

void EditorWindow::onExportContourClicked() {
    if (!ensureContourReport()) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar contorno"), QStringLiteral("contorno.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    const std::string csv = vision::contourToCsv(contour_, calibration_.mmPerPixel);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Exportar contorno"),
                             tr("No se pudo escribir «%1».").arg(path));
        return;
    }
    file.write(csv.data(), static_cast<qint64>(csv.size()));
    file.close();

    // Se dice en qué unidad ha salido: el mismo archivo en px y en mm se
    // distingue solo por la cabecera, y quien lo abra en el CAD tres días
    // después ya no se acuerda de si la pieza estaba calibrada.
    const bool inMm = calibration_.mmPerPixel > 0.0;
    std::size_t points = contour_.outer.size();
    for (const auto& hole : contour_.holes) {
        points += hole.size();
    }
    statusLabel_->setText(tr("Contorno exportado a %1 (%2 puntos, en %3).")
                              .arg(path)
                              .arg(points)
                              .arg(inMm ? tr("mm") : tr("píxeles")));
}

void EditorWindow::onTestClicked() {
    const cv::Mat image = camera::qImageToMat(reference_);
    const auto results =
        runTools(image, fixture_, activeConfigs(), calibration_.mmPerPixel);
    canvas_->setResults(results);

    QStringList lines;
    for (const auto& result : results) {
        // Un punto construido no tiene medida: sus coordenadas van en el
        // detalle. Escribir "0,0 px" sería un número inventado. Todo lo demás
        // lo rotula `formatMeasure`, que es el único sitio donde se decide la
        // unidad — aquí se decidía aparte, y se decidía mal: un Blob poligonal
        // salía formateado como una longitud.
        const QString measure =
            result.informative
                ? QStringLiteral("—")
                : QString::fromStdString(formatMeasure(result, calibration_.mmPerPixel,
                                                       LengthUnit::Auto));
        // Una construcción que salió bien no es un OK: no ha juzgado nada. Que
        // sí falle es otra cosa, y eso se dice.
        const QString state = (result.informative && result.ok) ? QStringLiteral("—")
                              : result.ok                       ? QStringLiteral("OK")
                                                                : QStringLiteral("NG");
        lines << QStringLiteral("%1 [%2] %3 — %4")
                     .arg(QString::fromStdString(result.name), state, measure,
                          QString::fromStdString(result.detail));
    }
    statusLabel_->setText(lines.isEmpty() ? tr("No hay herramientas que probar")
                                          : lines.join(QStringLiteral("\n")));
}

void EditorWindow::onSaveClicked() {
    if (repo_ == nullptr) {
        return;
    }
    int saved = 0;
    QStringList errors;
    for (auto& tool : tools_) {
        if (tool.deleted) {
            if (tool.config.id >= 0) {
                if (auto removed = repo_->remove(tool.config.id); !removed.isOk()) {
                    errors << QString::fromStdString(removed.error().message);
                }
            }
            continue;
        }
        tool.config.geometryJson = toJson(tool.geometry);
        auto result = repo_->save(pieceId_, tool.config, templateName_);
        if (result.isOk()) {
            tool.config.id = result.value();
            ++saved;
        } else {
            errors << QString::fromStdString(result.error().message);
        }
    }
    // Purgar los borrados ya aplicados en BD.
    std::erase_if(tools_, [](const EditedTool& tool) { return tool.deleted; });
    refreshList();

    if (errors.isEmpty()) {
        savedToDb_ = true;  // la vista en vivo puede tratar el estado como limpio
        statusLabel_->setText(tr("Plantilla guardada (%1 herramienta(s)).").arg(saved));
    } else {
        QMessageBox::warning(this, tr("Errores al guardar"),
                             errors.join(QStringLiteral("\n")));
    }
}

std::vector<EditedTool> EditorWindow::editedTools() const {
    std::vector<EditedTool> result;
    for (const auto& tool : tools_) {
        if (tool.deleted) {
            continue;
        }
        EditedTool copy = tool;
        copy.config.geometryJson = toJson(copy.geometry);
        result.push_back(std::move(copy));
    }
    return result;
}

}  // namespace pci::inspection
