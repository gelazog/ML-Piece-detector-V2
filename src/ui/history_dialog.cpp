#include "ui/history_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include "repositories/inspection_repository.h"
#include "repositories/piece_repository.h"

namespace pci::ui {

namespace {

// Comilla CSV: encierra en comillas y duplica las internas.
QString csvField(const QString& value) {
    QString escaped = value;
    escaped.replace(QChar('"'), QStringLiteral("\"\""));
    return QChar('"') + escaped + QChar('"');
}

}  // namespace

HistoryDialog::HistoryDialog(repositories::InspectionRepository* inspections,
                             repositories::PieceRepository* pieces,
                             std::int64_t initialPieceId, QWidget* parent)
    : QDialog(parent), inspections_(inspections), pieces_(pieces) {
    setWindowTitle(tr("Historial de inspecciones"));
    resize(640, 460);

    auto* root = new QVBoxLayout(this);

    auto* filters = new QHBoxLayout();
    filters->addWidget(new QLabel(tr("Pieza:"), this));
    pieceCombo_ = new QComboBox(this);
    pieceCombo_->setMinimumWidth(180);
    filters->addWidget(pieceCombo_);
    filters->addWidget(new QLabel(tr("Últimas:"), this));
    limitSpin_ = new QSpinBox(this);
    limitSpin_->setRange(1, 1000);
    limitSpin_->setValue(50);
    filters->addWidget(limitSpin_);
    filters->addStretch(1);
    root->addLayout(filters);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        {tr("Fecha"), tr("Veredicto"), tr("Similitud"), tr("Versión ref.")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    root->addWidget(table_, 1);

    summaryLabel_ = new QLabel(this);
    root->addWidget(summaryLabel_);

    auto* buttons = new QDialogButtonBox(this);
    auto* exportBtn = buttons->addButton(tr("Exportar CSV…"), QDialogButtonBox::ActionRole);
    buttons->addButton(tr("Cerrar"), QDialogButtonBox::RejectRole);
    root->addWidget(buttons);

    connect(pieceCombo_, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(limitSpin_, &QSpinBox::valueChanged, this, [this] { reload(); });
    connect(exportBtn, &QPushButton::clicked, this, &HistoryDialog::exportCsv);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    reloadPieces(initialPieceId);
}

void HistoryDialog::reloadPieces(std::int64_t select) {
    QSignalBlocker blocker(pieceCombo_);
    pieceCombo_->clear();
    if (pieces_ != nullptr) {
        if (auto listed = pieces_->listPieces(); listed.isOk()) {
            for (const auto& piece : listed.value()) {
                pieceCombo_->addItem(QString::fromStdString(piece.name),
                                     QVariant::fromValue<qlonglong>(piece.id));
                if (piece.id == select) {
                    pieceCombo_->setCurrentIndex(pieceCombo_->count() - 1);
                }
            }
        }
    }
    blocker.unblock();
    reload();
}

std::int64_t HistoryDialog::currentPieceId() const {
    const QVariant data = pieceCombo_->currentData();
    return data.isValid() ? data.toLongLong() : -1;
}

void HistoryDialog::reload() {
    table_->setRowCount(0);
    summaryLabel_->clear();
    const std::int64_t pieceId = currentPieceId();
    if (pieceId < 0 || inspections_ == nullptr) {
        return;
    }

    auto history = inspections_->recentForPiece(pieceId, limitSpin_->value());
    if (!history.isOk()) {
        summaryLabel_->setText(tr("No se pudo leer el historial: %1")
                                   .arg(QString::fromStdString(history.error().message)));
        return;
    }

    const auto& entries = history.value();
    table_->setRowCount(static_cast<int>(entries.size()));
    int okCount = 0;
    for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
        const auto& e = entries[static_cast<std::size_t>(row)];
        const bool ok = e.verdict == "OK";
        if (ok) {
            ++okCount;
        }
        table_->setItem(row, 0,
                        new QTableWidgetItem(QString::fromStdString(e.startedAt)));
        auto* verdictItem = new QTableWidgetItem(QString::fromStdString(e.verdict));
        verdictItem->setForeground(ok ? QBrush(QColor(0, 170, 0)) : QBrush(QColor(200, 40, 40)));
        table_->setItem(row, 1, verdictItem);
        table_->setItem(row, 2,
                        new QTableWidgetItem(QString::number(e.similarity, 'f', 4)));
        table_->setItem(row, 3,
                        new QTableWidgetItem(QString::number(e.referenceVersion)));
    }

    const int total = static_cast<int>(entries.size());
    summaryLabel_->setText(tr("%1 inspección(es) mostradas — %2 OK / %3 NG")
                               .arg(total)
                               .arg(okCount)
                               .arg(total - okCount));
}

void HistoryDialog::exportCsv() {
    if (table_->rowCount() == 0) {
        QMessageBox::information(this, tr("Sin datos"),
                                 tr("No hay inspecciones que exportar."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar historial"), QStringLiteral("historial.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("No se pudo escribir"),
                             tr("No se pudo abrir el archivo para escribir."));
        return;
    }
    QTextStream out(&file);
    out << "fecha,veredicto,similitud,version_ref\n";
    for (int row = 0; row < table_->rowCount(); ++row) {
        out << csvField(table_->item(row, 0)->text()) << ','
            << csvField(table_->item(row, 1)->text()) << ','
            << csvField(table_->item(row, 2)->text()) << ','
            << csvField(table_->item(row, 3)->text()) << '\n';
    }
    file.close();
    QMessageBox::information(this, tr("Exportado"),
                             tr("Historial exportado (%1 filas).").arg(table_->rowCount()));
}

}  // namespace pci::ui
