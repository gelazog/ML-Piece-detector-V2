#include "ui/template_manager_dialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

#include "inspection_editor/tools/template_io.h"
#include "repositories/tool_repository.h"

namespace pci::ui {

TemplateManagerDialog::TemplateManagerDialog(repositories::ToolRepository* repo,
                                             std::int64_t pieceId, QString activeTemplate,
                                             QWidget* parent)
    : QDialog(parent), repo_(repo), pieceId_(pieceId) {
    setWindowTitle(tr("Gestionar plantillas"));
    resize(360, 380);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(tr("Plantillas de la pieza:"), this));

    list_ = new QListWidget(this);
    root->addWidget(list_, 1);

    auto* actions = new QHBoxLayout();
    auto* newBtn = new QPushButton(tr("Nueva…"), this);
    auto* renameBtn = new QPushButton(tr("Renombrar…"), this);
    auto* duplicateBtn = new QPushButton(tr("Duplicar…"), this);
    auto* deleteBtn = new QPushButton(tr("Eliminar"), this);
    actions->addWidget(newBtn);
    actions->addWidget(renameBtn);
    actions->addWidget(duplicateBtn);
    actions->addWidget(deleteBtn);
    root->addLayout(actions);

    auto* fileActions = new QHBoxLayout();
    auto* exportBtn = new QPushButton(tr("Exportar…"), this);
    exportBtn->setToolTip(tr("Guardar la plantilla seleccionada en un archivo .json"));
    auto* importBtn = new QPushButton(tr("Importar…"), this);
    importBtn->setToolTip(tr("Cargar una plantilla desde un archivo .json a esta pieza"));
    fileActions->addWidget(exportBtn);
    fileActions->addWidget(importBtn);
    fileActions->addStretch(1);
    root->addLayout(fileActions);

    auto* buttons = new QDialogButtonBox(this);
    auto* useBtn = buttons->addButton(tr("Usar seleccionada"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Cerrar"), QDialogButtonBox::RejectRole);
    root->addWidget(buttons);

    connect(newBtn, &QPushButton::clicked, this, &TemplateManagerDialog::onNew);
    connect(renameBtn, &QPushButton::clicked, this, &TemplateManagerDialog::onRename);
    connect(duplicateBtn, &QPushButton::clicked, this, &TemplateManagerDialog::onDuplicate);
    connect(deleteBtn, &QPushButton::clicked, this, &TemplateManagerDialog::onDelete);
    connect(exportBtn, &QPushButton::clicked, this, &TemplateManagerDialog::onExport);
    connect(importBtn, &QPushButton::clicked, this, &TemplateManagerDialog::onImport);
    connect(useBtn, &QPushButton::clicked, this, [this] {
        selected_ = currentName();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] {
        selected_ = currentName();
        accept();
    });

    reload(activeTemplate);
}

void TemplateManagerDialog::reload(const QString& select) {
    const QString target = select.isEmpty() ? currentName() : select;
    list_->clear();

    // "principal" siempre está disponible aunque aún no tenga herramientas.
    std::vector<std::string> names{"principal"};
    if (repo_ != nullptr) {
        if (auto listed = repo_->listTemplates(pieceId_); listed.isOk()) {
            for (const auto& name : listed.value()) {
                if (std::find(names.begin(), names.end(), name) == names.end()) {
                    names.push_back(name);
                }
            }
        }
    }
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        list_->addItem(QString::fromStdString(name));
    }

    const auto matches = list_->findItems(target, Qt::MatchExactly);
    if (!matches.isEmpty()) {
        list_->setCurrentItem(matches.front());
    } else if (list_->count() > 0) {
        list_->setCurrentRow(0);
    }
}

QString TemplateManagerDialog::currentName() const {
    return list_->currentItem() != nullptr ? list_->currentItem()->text() : QString();
}

void TemplateManagerDialog::onNew() {
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Nueva plantilla"), tr("Nombre:"), QLineEdit::Normal,
                              tr("plantilla %1").arg(list_->count() + 1), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (!list_->findItems(name, Qt::MatchExactly).isEmpty()) {
        QMessageBox::warning(this, tr("Ya existe"), tr("Ya hay una plantilla con ese nombre."));
        return;
    }
    // Nace vacía (se materializa en la BD al guardar su primera herramienta);
    // se añade y se selecciona para poder activarla.
    list_->addItem(name);
    list_->setCurrentItem(list_->findItems(name, Qt::MatchExactly).front());
}

void TemplateManagerDialog::onRename() {
    const QString from = currentName();
    if (from.isEmpty() || repo_ == nullptr) {
        return;
    }
    bool ok = false;
    const QString to =
        QInputDialog::getText(this, tr("Renombrar plantilla"),
                              tr("Nuevo nombre para '%1':").arg(from), QLineEdit::Normal,
                              from, &ok)
            .trimmed();
    if (!ok || to.isEmpty() || to == from) {
        return;
    }
    if (auto result = repo_->renameTemplate(pieceId_, from.toStdString(), to.toStdString());
        !result.isOk()) {
        QMessageBox::warning(this, tr("No se pudo renombrar"),
                             QString::fromStdString(result.error().message));
        return;
    }
    reload(to);
}

void TemplateManagerDialog::onDuplicate() {
    const QString from = currentName();
    if (from.isEmpty() || repo_ == nullptr) {
        return;
    }
    bool ok = false;
    const QString to =
        QInputDialog::getText(this, tr("Duplicar plantilla"),
                              tr("Nombre de la copia de '%1':").arg(from), QLineEdit::Normal,
                              tr("%1 copia").arg(from), &ok)
            .trimmed();
    if (!ok || to.isEmpty()) {
        return;
    }
    if (auto result = repo_->duplicateTemplate(pieceId_, from.toStdString(), to.toStdString());
        !result.isOk()) {
        QMessageBox::warning(this, tr("No se pudo duplicar"),
                             QString::fromStdString(result.error().message));
        return;
    }
    reload(to);
}

void TemplateManagerDialog::onDelete() {
    const QString name = currentName();
    if (name.isEmpty() || repo_ == nullptr) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Eliminar plantilla"),
        tr("¿Eliminar la plantilla '%1' y todas sus herramientas?\n"
           "Esto no se puede deshacer.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (auto result = repo_->deleteTemplate(pieceId_, name.toStdString()); !result.isOk()) {
        QMessageBox::warning(this, tr("No se pudo eliminar"),
                             QString::fromStdString(result.error().message));
        return;
    }
    reload();
}

void TemplateManagerDialog::onExport() {
    const QString name = currentName();
    if (name.isEmpty() || repo_ == nullptr) {
        return;
    }
    auto tools = repo_->listForPiece(pieceId_, name.toStdString());
    if (!tools.isOk()) {
        QMessageBox::warning(this, tr("No se pudo leer la plantilla"),
                             QString::fromStdString(tools.error().message));
        return;
    }
    if (tools.value().empty()) {
        QMessageBox::information(this, tr("Plantilla vacía"),
                                 tr("La plantilla '%1' no tiene herramientas que exportar.")
                                     .arg(name));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar plantilla"), name + QStringLiteral(".json"),
        tr("Plantilla de inspección (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    const std::string json = inspection::exportTemplateJson(tools.value());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("No se pudo escribir"),
                             tr("No se pudo abrir el archivo para escribir."));
        return;
    }
    file.write(json.data(), static_cast<qint64>(json.size()));
    file.close();
    QMessageBox::information(this, tr("Exportada"),
                             tr("Plantilla '%1' exportada (%2 herramienta(s)).")
                                 .arg(name)
                                 .arg(tools.value().size()));
}

void TemplateManagerDialog::onImport() {
    if (repo_ == nullptr) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importar plantilla"), QString(),
        tr("Plantilla de inspección (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("No se pudo leer"),
                             tr("No se pudo abrir el archivo."));
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    auto imported = inspection::importTemplateJson(data.toStdString());
    if (!imported.isOk()) {
        QMessageBox::warning(this, tr("Archivo inválido"),
                             QString::fromStdString(imported.error().message));
        return;
    }

    bool ok = false;
    const QString target =
        QInputDialog::getText(this, tr("Importar en plantilla"),
                              tr("Nombre de la plantilla destino:"), QLineEdit::Normal,
                              QFileInfo(path).completeBaseName(), &ok)
            .trimmed();
    if (!ok || target.isEmpty()) {
        return;
    }
    if (!list_->findItems(target, Qt::MatchExactly).isEmpty()) {
        QMessageBox::warning(this, tr("Ya existe"),
                             tr("Ya hay una plantilla '%1'. Elige otro nombre.").arg(target));
        return;
    }

    int errors = 0;
    for (auto& config : imported.value()) {
        config.id = -1;
        if (auto saved = repo_->save(pieceId_, config, target.toStdString()); !saved.isOk()) {
            ++errors;
        }
    }
    reload(target);
    if (errors == 0) {
        QMessageBox::information(this, tr("Importada"),
                                 tr("Plantilla importada como '%1' (%2 herramienta(s)).")
                                     .arg(target)
                                     .arg(imported.value().size()));
    } else {
        QMessageBox::warning(this, tr("Importada con errores"),
                             tr("%1 herramienta(s) no se pudieron guardar.").arg(errors));
    }
}

}  // namespace pci::ui
