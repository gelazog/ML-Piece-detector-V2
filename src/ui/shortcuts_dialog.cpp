#include "ui/shortcuts_dialog.h"

#include <QAction>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "repositories/settings_repository.h"

namespace pci::ui {

ShortcutsDialog::ShortcutsDialog(std::vector<ShortcutSpec>* shortcuts,
                                 repositories::SettingsRepository* settings, QWidget* parent)
    : QDialog(parent), shortcuts_(shortcuts), settings_(settings) {
    setWindowTitle(tr("Guía de atajos de teclado"));
    resize(560, 620);

    auto* rootLayout = new QVBoxLayout(this);
    auto* help = new QLabel(
        tr("Haz clic en la columna Atajo y pulsa la combinación nueva. Evita asignar la "
           "misma tecla a dos comandos. En los diálogos aplican además los atajos "
           "estándar (Ctrl+Z/Ctrl+Y, Supr, Esc cierra)."),
        this);
    help->setWordWrap(true);
    rootLayout->addWidget(help);

    table_ = new QTableWidget(static_cast<int>(shortcuts_->size()), 2, this);
    table_->setHorizontalHeaderLabels({tr("Comando"), tr("Atajo")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    for (int row = 0; row < static_cast<int>(shortcuts_->size()); ++row) {
        const auto& spec = (*shortcuts_)[static_cast<std::size_t>(row)];
        auto* name = new QTableWidgetItem(spec.description);
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, name);
        auto* edit = new QKeySequenceEdit(spec.action->shortcut(), table_);
        edit->setClearButtonEnabled(true);
        table_->setCellWidget(row, 1, edit);
    }
    rootLayout->addWidget(table_, 1);

    auto* buttonsLayout = new QHBoxLayout();
    auto* restore = new QPushButton(tr("Restaurar por defecto"), this);
    buttonsLayout->addWidget(restore);
    buttonsLayout->addStretch(1);
    auto* save = new QPushButton(tr("Guardar"), this);
    buttonsLayout->addWidget(save);
    auto* cancel = new QPushButton(tr("Cancelar"), this);
    buttonsLayout->addWidget(cancel);
    rootLayout->addLayout(buttonsLayout);

    connect(restore, &QPushButton::clicked, this, &ShortcutsDialog::onRestoreDefaults);
    connect(save, &QPushButton::clicked, this, &ShortcutsDialog::onSave);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void ShortcutsDialog::onRestoreDefaults() {
    for (int row = 0; row < static_cast<int>(shortcuts_->size()); ++row) {
        const auto& spec = (*shortcuts_)[static_cast<std::size_t>(row)];
        if (auto* edit = qobject_cast<QKeySequenceEdit*>(table_->cellWidget(row, 1))) {
            edit->setKeySequence(spec.defaultKey);
        }
    }
}

void ShortcutsDialog::onSave() {
    for (int row = 0; row < static_cast<int>(shortcuts_->size()); ++row) {
        auto& spec = (*shortcuts_)[static_cast<std::size_t>(row)];
        auto* edit = qobject_cast<QKeySequenceEdit*>(table_->cellWidget(row, 1));
        if (edit == nullptr) {
            continue;
        }
        const QKeySequence sequence = edit->keySequence();
        spec.action->setShortcut(sequence);
        if (settings_ != nullptr) {
            settings_->setString(("key_" + spec.id).toStdString(),
                                 sequence.toString().toStdString());
        }
    }
    accept();
}

}  // namespace pci::ui
