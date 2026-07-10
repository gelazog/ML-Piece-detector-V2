#include "inspection_editor/editor_window.h"

#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "camera/frame_utils.h"
#include "core/logging.h"
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
    }
    return QStringLiteral("?");
}

}  // namespace

EditorWindow::EditorWindow(const QImage& reference, const vision::Fixture& fixture,
                           std::int64_t pieceId, repositories::ToolRepository* repo,
                           QWidget* parent)
    : QDialog(parent), reference_(reference), fixture_(fixture), pieceId_(pieceId),
      repo_(repo) {
    setWindowTitle(tr("Editor de plantilla de inspección"));
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
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        if (id >= 0) {
            button->setToolTip(
                QString::fromUtf8(toolTypeDescription(static_cast<ToolType>(id))));
        } else {
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
    modesLayout->addStretch(1);
    rootLayout->addLayout(modesLayout);

    // Canvas (centro).
    canvas_ = new EditorCanvas(this);
    canvas_->setScene(reference_, fixture_);
    canvas_->setTools(&tools_);
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
    connect(canvas_, &EditorCanvas::toolModified, this, [this] { canvas_->clearResults(); });
    connect(list_, &QListWidget::currentRowChanged, this, &EditorWindow::onListRowChanged);
    connect(nameEdit_, &QLineEdit::editingFinished, this, &EditorWindow::onPanelEdited);
    connect(tolMin_, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onPanelEdited);
    connect(tolMax_, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onPanelEdited);
    connect(deleteButton_, &QPushButton::clicked, this, &EditorWindow::onDeleteClicked);
    connect(testButton, &QPushButton::clicked, this, &EditorWindow::onTestClicked);
    connect(saveButton, &QPushButton::clicked, this, &EditorWindow::onSaveClicked);

    loadExistingTools();
    refreshList();
    syncPanelFromSelection();
}

void EditorWindow::loadExistingTools() {
    if (repo_ == nullptr) {
        return;
    }
    auto listed = repo_->listForPiece(pieceId_);
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
    if (hasSelection) {
        const auto& tool = tools_[static_cast<std::size_t>(index)];
        nameEdit_->setText(QString::fromStdString(tool.config.name));
        tolMin_->setValue(tool.config.toleranceMin);
        tolMax_->setValue(tool.config.toleranceMax);
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
    const auto measured =
        runTool(camera::qImageToMat(reference_), fixture_, tool.config);
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

    canvas_->clearResults();
    canvas_->setSelectedIndex(static_cast<int>(tools_.size()) - 1);
    refreshList();
    syncPanelFromSelection();
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
    refreshList();
}

void EditorWindow::onDeleteClicked() {
    const int index = canvas_->selectedIndex();
    if (index < 0 || index >= static_cast<int>(tools_.size())) {
        return;
    }
    tools_[static_cast<std::size_t>(index)].deleted = true;
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
    const auto results = runTools(image, fixture_, activeConfigs());
    canvas_->setResults(results);

    QStringList lines;
    for (const auto& result : results) {
        lines << QStringLiteral("%1 [%2] %3")
                     .arg(QString::fromStdString(result.name),
                          result.ok ? QStringLiteral("OK") : QStringLiteral("NG"),
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
        auto result = repo_->save(pieceId_, tool.config);
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
        statusLabel_->setText(tr("Plantilla guardada (%1 herramienta(s)).").arg(saved));
    } else {
        QMessageBox::warning(this, tr("Errores al guardar"),
                             errors.join(QStringLiteral("\n")));
    }
}

}  // namespace pci::inspection
