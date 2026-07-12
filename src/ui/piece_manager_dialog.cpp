#include "ui/piece_manager_dialog.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "repositories/piece_repository.h"
#include "repositories/tool_repository.h"

namespace pci::ui {

PieceManagerDialog::PieceManagerDialog(repositories::PieceRepository* pieces,
                                       repositories::ToolRepository* tools, QWidget* parent)
    : QDialog(parent), pieces_(pieces), tools_(tools) {
    setWindowTitle(tr("Gestionar piezas"));
    resize(560, 480);

    auto* rootLayout = new QHBoxLayout(this);
    list_ = new QListWidget(this);
    rootLayout->addWidget(list_, 1);

    auto* sideLayout = new QVBoxLayout();
    infoLabel_ = new QLabel(this);
    infoLabel_->setWordWrap(true);
    sideLayout->addWidget(infoLabel_);

    renameButton_ = new QPushButton(tr("Renombrar…"), this);
    sideLayout->addWidget(renameButton_);
    deleteButton_ = new QPushButton(tr("Eliminar…"), this);
    deleteButton_->setToolTip(
        tr("Elimina la pieza con sus referencias, herramientas e historial."));
    sideLayout->addWidget(deleteButton_);

    auto* form = new QFormLayout();
    offsetSpin_ = new QDoubleSpinBox(this);
    offsetSpin_->setRange(-180.0, 180.0);
    offsetSpin_->setDecimals(1);
    offsetSpin_->setSuffix(QStringLiteral(" °"));
    offsetSpin_->setToolTip(
        tr("Gira el sistema de coordenadas de la pieza para dejar el eje donde\n"
           "quieras. Aplica al video en vivo, al registro y a la inspección.\n"
           "Ojo: si la pieza ya tiene herramientas dibujadas, girar la\n"
           "orientación las desplaza con el nuevo eje."));
    form->addRow(tr("Orientación:"), offsetSpin_);
    sideLayout->addLayout(form);
    rotateButton_ = new QPushButton(tr("Girar +90°"), this);
    sideLayout->addWidget(rotateButton_);

    sideLayout->addStretch(1);
    auto* closeButton = new QPushButton(tr("Cerrar"), this);
    sideLayout->addWidget(closeButton);
    rootLayout->addLayout(sideLayout);

    connect(list_, &QListWidget::currentRowChanged, this,
            &PieceManagerDialog::onSelectionChanged);
    connect(renameButton_, &QPushButton::clicked, this,
            &PieceManagerDialog::onRenameClicked);
    connect(deleteButton_, &QPushButton::clicked, this,
            &PieceManagerDialog::onDeleteClicked);
    connect(offsetSpin_, &QDoubleSpinBox::valueChanged, this,
            &PieceManagerDialog::onOffsetEdited);
    connect(rotateButton_, &QPushButton::clicked, this,
            &PieceManagerDialog::onRotate90Clicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    reloadList();
}

void PieceManagerDialog::reloadList(std::int64_t selectId) {
    syncing_ = true;
    list_->clear();
    if (pieces_ != nullptr) {
        if (auto listed = pieces_->listPieces(); listed.isOk()) {
            for (const auto& piece : listed.value()) {
                auto* item = new QListWidgetItem(QString::fromStdString(piece.name), list_);
                item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(piece.id));
                if (piece.id == selectId) {
                    list_->setCurrentItem(item);
                }
            }
        }
    }
    syncing_ = false;
    if (list_->currentRow() < 0 && list_->count() > 0) {
        list_->setCurrentRow(0);
    } else {
        onSelectionChanged();
    }
}

std::int64_t PieceManagerDialog::selectedPieceId() const {
    auto* item = list_->currentItem();
    return item != nullptr ? item->data(Qt::UserRole).toLongLong() : -1;
}

void PieceManagerDialog::onSelectionChanged() {
    if (syncing_) {
        return;
    }
    const std::int64_t pieceId = selectedPieceId();
    const bool hasSelection = pieceId >= 0;
    renameButton_->setEnabled(hasSelection);
    deleteButton_->setEnabled(hasSelection);
    offsetSpin_->setEnabled(hasSelection);
    rotateButton_->setEnabled(hasSelection);

    if (!hasSelection) {
        infoLabel_->setText(tr("No hay piezas registradas."));
        return;
    }

    int versions = 0;
    int toolCount = 0;
    if (auto listed = pieces_->listReferenceVersions(pieceId); listed.isOk()) {
        versions = static_cast<int>(listed.value().size());
    }
    if (tools_ != nullptr) {
        if (auto listed = tools_->listForPiece(pieceId); listed.isOk()) {
            toolCount = static_cast<int>(listed.value().size());
        }
    }
    infoLabel_->setText(tr("Referencias: %1 versión(es)\nHerramientas: %2\n\n"
                           "Los puntos y tolerancias de las herramientas se editan en "
                           "Plantilla… con esta pieza seleccionada.")
                            .arg(versions)
                            .arg(toolCount));

    syncing_ = true;
    if (auto offset = pieces_->loadOrientationOffset(pieceId); offset.isOk()) {
        offsetSpin_->setValue(offset.value());
    } else {
        offsetSpin_->setValue(0.0);
    }
    syncing_ = false;
}

void PieceManagerDialog::onRenameClicked() {
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0) {
        return;
    }
    const QString current = list_->currentItem()->text();
    const QString newName = QInputDialog::getText(this, tr("Renombrar pieza"),
                                                  tr("Nuevo nombre:"), QLineEdit::Normal,
                                                  current)
                                .trimmed();
    if (newName.isEmpty() || newName == current) {
        return;
    }
    if (auto renamed = pieces_->renamePiece(pieceId, newName.toStdString());
        !renamed.isOk()) {
        QMessageBox::warning(this, tr("No se pudo renombrar"),
                             QString::fromStdString(renamed.error().message));
        return;
    }
    changed_ = true;
    reloadList(pieceId);
}

void PieceManagerDialog::onDeleteClicked() {
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0) {
        return;
    }
    const QString name = list_->currentItem()->text();
    const auto answer = QMessageBox::warning(
        this, tr("Eliminar pieza"),
        tr("¿Eliminar '%1' con TODAS sus referencias, herramientas e historial?\n"
           "Esta acción no se puede deshacer.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (auto removed = pieces_->removePiece(pieceId); !removed.isOk()) {
        QMessageBox::warning(this, tr("No se pudo eliminar"),
                             QString::fromStdString(removed.error().message));
        return;
    }
    changed_ = true;
    reloadList();
}

void PieceManagerDialog::onOffsetEdited() {
    if (syncing_) {
        return;
    }
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0) {
        return;
    }
    if (auto saved = pieces_->saveOrientationOffset(pieceId, offsetSpin_->value());
        saved.isOk()) {
        changed_ = true;
    } else {
        QMessageBox::warning(this, tr("No se pudo guardar"),
                             QString::fromStdString(saved.error().message));
    }
}

void PieceManagerDialog::onRotate90Clicked() {
    double next = offsetSpin_->value() + 90.0;
    if (next > 180.0) {
        next -= 360.0;
    }
    offsetSpin_->setValue(next);  // dispara onOffsetEdited y persiste
}

}  // namespace pci::ui
