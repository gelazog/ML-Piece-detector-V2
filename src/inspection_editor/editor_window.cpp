#include "inspection_editor/editor_window.h"

#include <QAction>
#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <optional>
#include <type_traits>
#include <variant>

#include "camera/frame_utils.h"
#include "core/logging.h"
#include "inspection_editor/canvas/tool_icons.h"
#include "inspection_editor/execution/tool_executor.h"
#include "repositories/tool_repository.h"

namespace pci::inspection {

namespace {

QString typeLabel(ToolType type) {
    switch (type) {
        case ToolType::Caliper: return QStringLiteral("Caliper");
        case ToolType::Circle: return QStringLiteral("Círculo");
        case ToolType::PointToLine: return QStringLiteral("Punto-Línea");
        case ToolType::EdgeFlaw: return QStringLiteral("Borde liso");
        case ToolType::Blob: return QStringLiteral("Blob");
        case ToolType::Ruler: return QStringLiteral("Regla");
        case ToolType::LineToLine: return QStringLiteral("Línea-Línea");
        case ToolType::Angle: return QStringLiteral("Ángulo");
        case ToolType::PolyBlob: return QStringLiteral("Blob poligonal");
    }
    return QStringLiteral("?");
}

}  // namespace

EditorWindow::EditorWindow(const QImage& reference, const vision::Fixture& fixture,
                           std::int64_t pieceId, repositories::ToolRepository* repo,
                           domain::ScaleCalibration calibration,
                           const std::string& templateName, QWidget* parent,
                           const std::vector<EditedTool>* initialTools)
    : QDialog(parent), reference_(reference), fixture_(fixture), pieceId_(pieceId),
      repo_(repo), calibration_(calibration), templateName_(templateName) {
    setWindowTitle(tr("Editor de plantilla '%1'")
                       .arg(QString::fromStdString(templateName)));
    resize(1100, 700);

    auto* rootLayout = new QHBoxLayout(this);

    // Barra de modos (izquierda).
    auto* modesLayout = new QVBoxLayout();
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);

    auto addMode = [this, modesLayout](const QString& text, int id) {
        auto* button = new QToolButton(this);
        button->setText(text);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIconSize(QSize(22, 22));
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        if (id >= 0) {
            button->setIcon(toolIcon(static_cast<ToolType>(id)));
            button->setToolTip(
                QString::fromUtf8(toolTypeDescription(static_cast<ToolType>(id))));
        } else {
            button->setIcon(moveModeIcon());
            button->setToolTip(tr("Clic para seleccionar; arrastra para mover."));
        }
        modeGroup_->addButton(button, id);
        modesLayout->addWidget(button);
        return button;
    };
    addMode(tr("Seleccionar"), -1)->setChecked(true);
    addMode(tr("Caliper"), static_cast<int>(ToolType::Caliper));
    addMode(tr("Círculo"), static_cast<int>(ToolType::Circle));
    addMode(tr("Punto-Línea"), static_cast<int>(ToolType::PointToLine));
    addMode(tr("Borde liso"), static_cast<int>(ToolType::EdgeFlaw));
    addMode(tr("Blob"), static_cast<int>(ToolType::Blob));
    addMode(tr("Regla"), static_cast<int>(ToolType::Ruler));
    addMode(tr("Línea-Línea"), static_cast<int>(ToolType::LineToLine));
    addMode(tr("Ángulo"), static_cast<int>(ToolType::Angle));
    addMode(tr("Blob poligonal"), static_cast<int>(ToolType::PolyBlob));
    modesLayout->addStretch(1);
    rootLayout->addLayout(modesLayout);

    // Canvas (centro).
    canvas_ = new EditorCanvas(this);
    canvas_->setScene(reference_, fixture_);
    canvas_->setTools(&tools_);
    canvas_->setMmPerPixel(calibration_.mmPerPixel);
    rootLayout->addWidget(canvas_, 1);

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
           "Caliper: grosor de banda promediada (px)\n"
           "Círculo: rayos de búsqueda del borde\n"
           "Borde liso: escaneos perpendiculares\n"
           "Blob: área mínima de cada mancha (px²)"));
    form->addRow(paramLabel_, paramSpin_);
    if (calibration_.valid()) {
        auto* scaleHint = new QLabel(
            tr("Escala calibrada: 1 px ≈ %1 mm (tolerancias en px)")
                .arg(calibration_.mmPerPixel, 0, 'f', 4),
            this);
        scaleHint->setWordWrap(true);
        form->addRow(scaleHint);
    }
    sideLayout->addLayout(form);

    deleteButton_ = new QPushButton(tr("Eliminar herramienta"), this);
    sideLayout->addWidget(deleteButton_);

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

    connect(modeGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        canvas_->setCreateType(id < 0 ? std::nullopt
                                      : std::optional<ToolType>(static_cast<ToolType>(id)));
    });
    connect(canvas_, &EditorCanvas::toolCreated, this, &EditorWindow::onToolCreated);
    connect(canvas_, &EditorCanvas::selectionChanged, this, &EditorWindow::onCanvasSelection);
    connect(canvas_, &EditorCanvas::toolModified, this, [this] {
        canvas_->clearResults();
        commitUndoState();
    });
    connect(list_, &QListWidget::currentRowChanged, this, &EditorWindow::onListRowChanged);
    connect(nameEdit_, &QLineEdit::editingFinished, this, &EditorWindow::onPanelEdited);
    connect(tolMin_, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onPanelEdited);
    connect(tolMax_, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onPanelEdited);
    connect(paramSpin_, &QSpinBox::valueChanged, this, &EditorWindow::onPanelEdited);
    connect(deleteButton_, &QPushButton::clicked, this, &EditorWindow::onDeleteClicked);
    connect(testButton, &QPushButton::clicked, this, &EditorWindow::onTestClicked);
    connect(saveButton, &QPushButton::clicked, this, &EditorWindow::onSaveClicked);

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
    nameEdit_->setEnabled(hasSelection);
    tolMin_->setEnabled(hasSelection);
    tolMax_->setEnabled(hasSelection);
    deleteButton_->setEnabled(hasSelection);
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
    } else {
        nameEdit_->clear();
    }
    syncing_ = false;
}

void EditorWindow::onToolCreated(const ToolGeometry& geometry) {
    EditedTool tool;
    tool.geometry = geometry;
    tool.config.type = typeOf(geometry);
    ++nameCounter_;
    tool.config.name =
        (typeLabel(tool.config.type) + QStringLiteral(" %1").arg(nameCounter_)).toStdString();
    tool.config.geometryJson = toJson(geometry);
    tool.config.toleranceMin = 0.0;
    tool.config.toleranceMax = 100000.0;

    // Medir de inmediato sobre la imagen de referencia y sugerir tolerancias:
    // la pieza buena define su propio rango de aceptación.
    const auto measured = runTool(camera::qImageToMat(reference_), fixture_, tool.config,
                                  calibration_.mmPerPixel);
    if (measured.isOk() && (measured.value().ok || measured.value().measured > 0.0)) {
        suggestTolerances(tool.config.type, measured.value().measured,
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

void EditorWindow::onTestClicked() {
    const cv::Mat image = camera::qImageToMat(reference_);
    const auto results =
        runTools(image, fixture_, activeConfigs(), calibration_.mmPerPixel);
    canvas_->setResults(results);

    QStringList lines;
    for (const auto& result : results) {
        QString measure;
        if (result.measuredIsAngle) {
            measure = QStringLiteral("%1°").arg(result.measured, 0, 'f', 1);
        } else if (result.type == ToolType::Blob) {
            measure = QString::number(result.measured, 'f', 0);
        } else {
            measure = QString::fromStdString(calibration_.formatLength(result.measured));
        }
        lines << QStringLiteral("%1 [%2] %3 — %4")
                     .arg(QString::fromStdString(result.name),
                          result.ok ? QStringLiteral("OK") : QStringLiteral("NG"), measure,
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
