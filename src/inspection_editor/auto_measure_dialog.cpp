#include "inspection_editor/auto_measure_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

#include "inspection_editor/canvas/tool_icons.h"

namespace pci::inspection {

namespace {

constexpr int kColumnCheck = 0;
constexpr int kColumnName = 1;
constexpr int kColumnMeasured = 2;
constexpr int kColumnTolerance = 3;
constexpr int kColumnReason = 4;

}  // namespace

AutoMeasureDialog::AutoMeasureDialog(std::vector<AutoProposal> proposals, QWidget* parent)
    : QDialog(parent), proposals_(std::move(proposals)) {
    setWindowTitle(tr("Medición automática"));
    resize(820, 420);

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Se ha medido la pieza y estas son las cotas encontradas. Marca las que "
           "quieras conservar: se añadirán como herramientas, con sus tolerancias ya "
           "sugeridas, en un solo paso que puedes deshacer."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    table_ = new QTableWidget(static_cast<int>(proposals_.size()), 5, this);
    table_->setHorizontalHeaderLabels(
        {tr(""), tr("Medida"), tr("Valor"), tr("Tolerancia"), tr("Por qué se propone")});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(kColumnReason, QHeaderView::Stretch);

    for (int row = 0; row < static_cast<int>(proposals_.size()); ++row) {
        const AutoProposal& proposal = proposals_[static_cast<std::size_t>(row)];

        auto* check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        // Todas marcadas de entrada: lo normal es querer casi todas, y así
        // revisar consiste en desmarcar lo que sobra en vez de marcar de una en
        // una.
        check->setCheckState(Qt::Checked);
        table_->setItem(row, kColumnCheck, check);

        auto* name = new QTableWidgetItem(QString::fromStdString(proposal.config.name));
        name->setIcon(toolIcon(proposal.config.type));
        table_->setItem(row, kColumnName, name);

        // El detalle completo de la herramienta (con unidades y avisos) va en el
        // tooltip: en la celda solo cabe el número, pero un aviso de condiciones
        // de medida no se puede esconder.
        auto* value = new QTableWidgetItem(QString::number(proposal.measured, 'f', 2));
        value->setToolTip(QString::fromStdString(proposal.detail));
        table_->setItem(row, kColumnMeasured, value);

        table_->setItem(row, kColumnTolerance,
                        new QTableWidgetItem(QStringLiteral("%1 … %2")
                                                 .arg(proposal.config.toleranceMin, 0, 'f', 1)
                                                 .arg(proposal.config.toleranceMax, 0, 'f', 1)));
        table_->setItem(row, kColumnReason,
                        new QTableWidgetItem(QString::fromStdString(proposal.reason)));
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setSectionResizeMode(kColumnReason, QHeaderView::Stretch);
    layout->addWidget(table_, 1);

    auto* selectionRow = new QHBoxLayout();
    auto* all = new QPushButton(tr("Marcar todas"), this);
    auto* none = new QPushButton(tr("Desmarcar todas"), this);
    selectionRow->addWidget(all);
    selectionRow->addWidget(none);
    selectionRow->addStretch(1);
    layout->addLayout(selectionRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    acceptButton_ = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);

    const auto setAll = [this](Qt::CheckState state) {
        for (int row = 0; row < table_->rowCount(); ++row) {
            table_->item(row, kColumnCheck)->setCheckState(state);
        }
        updateAcceptLabel();
    };
    connect(all, &QPushButton::clicked, this, [setAll] { setAll(Qt::Checked); });
    connect(none, &QPushButton::clicked, this, [setAll] { setAll(Qt::Unchecked); });
    connect(table_, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem*) { updateAcceptLabel(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateAcceptLabel();
}

void AutoMeasureDialog::updateAcceptLabel() {
    if (acceptButton_ == nullptr) {
        return;
    }
    int checked = 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (table_->item(row, kColumnCheck)->checkState() == Qt::Checked) {
            ++checked;
        }
    }
    // El botón dice cuántas va a insertar: sin ese número hay que contar a mano
    // las casillas antes de pulsar.
    acceptButton_->setText(checked == 0 ? tr("No insertar nada")
                                        : tr("Insertar %1").arg(checked));
    acceptButton_->setEnabled(checked > 0);
}

std::vector<AutoProposal> AutoMeasureDialog::accepted() const {
    std::vector<AutoProposal> out;
    if (result() != QDialog::Accepted) {
        return out;
    }
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (table_->item(row, kColumnCheck)->checkState() == Qt::Checked) {
            out.push_back(proposals_[static_cast<std::size_t>(row)]);
        }
    }
    return out;
}

}  // namespace pci::inspection
