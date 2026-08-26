#include "ui/history_dialog.h"
#include "ui/theme.h"

#include <QComboBox>
#include <QDialogButtonBox>

#include "ui/dialog_buttons.h"
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QDate>
#include <vector>
#include "domain/shift_report.h"
#include <QSaveFile>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include "repositories/inspection_repository.h"
#include "repositories/piece_repository.h"
#include "ui/stats_bar_chart.h"

namespace pci::ui {

// El escapado del CSV ya no vive aquí: lo hace `domain/shift_report.cpp`, que es
// quien arma el fichero. Tener dos versiones del mismo escapado es tener dos
// sitios donde una comilla puede romper un informe.

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

    root->addWidget(new QLabel(tr("Tendencia OK/NG por día (últimos 30 días):"), this));
    chart_ = new StatsBarChart(this);
    root->addWidget(chart_);

    auto* buttons = new QDialogButtonBox(this);
    nameButtonsInSpanish(buttons);
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
        chart_->setData({});
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
        verdictItem->setForeground(ok ? QBrush(theme::color(theme::kGood))
                                      : QBrush(theme::color(theme::kBad)));
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

    // Tendencia por día (S2): independiente del límite de la tabla.
    if (auto daily = inspections_->dailyStats(pieceId, 30); daily.isOk()) {
        chart_->setData(daily.value());
    } else {
        chart_->setData({});
    }
}

// EXPORTAR EL TURNO, no la tabla.
//
// Antes esto escribía las filas que se veían en pantalla: fecha, veredicto,
// similitud y versión. Es una lista, y una lista de cuatrocientas filas contesta
// «qué pasó exactamente a las 14:32» —que casi nunca se pregunta— y esconde las
// tres que sí: cuántas van, QUÉ está fallando y DESDE CUÁNDO.
//
// Ahora se pide al historial las inspecciones CON SU MOTIVO y se arma el informe
// de `domain/shift_report.h`, que pone el resumen arriba y las filas debajo. Las
// filas siguen estando enteras: quien quiera cruzarlas con otra cosa las
// necesita.
void HistoryDialog::exportCsv() {
    const std::int64_t pieceId = currentPieceId();
    if (pieceId < 0 || inspections_ == nullptr) {
        QMessageBox::information(this, tr("Sin datos"),
                                 tr("No hay ninguna pieza seleccionada."));
        return;
    }
    // QUÉ PERIODO. Se pregunta, y no se da por supuesto, porque «el informe del
    // turno» sin decir de qué turno era el fallo de la primera versión: la
    // consulta se llevaba el historial entero de la pieza y lo titulaba turno.
    QMessageBox period(QMessageBox::Question, tr("Informe del turno"),
                       tr("¿De qué periodo?"), QMessageBox::NoButton, this);
    auto* todayButton = period.addButton(tr("Hoy"), QMessageBox::AcceptRole);
    auto* weekButton = period.addButton(tr("Últimos 7 días"), QMessageBox::AcceptRole);
    auto* allButton = period.addButton(tr("Todo el historial"), QMessageBox::AcceptRole);
    period.addButton(QMessageBox::Cancel);
    period.setDefaultButton(todayButton);
    period.exec();
    if (period.clickedButton() == nullptr ||
        (period.clickedButton() != todayButton && period.clickedButton() != weekButton &&
         period.clickedButton() != allButton)) {
        return;
    }

    repositories::InspectionRepository::ReportWindow window;
    QString periodName = tr("todo el historial");
    const QDate today = QDate::currentDate();
    if (period.clickedButton() == todayButton) {
        window.from = today.toString(QStringLiteral("yyyy-MM-dd")).toStdString() +
                      " 00:00:00";
        window.to = today.toString(QStringLiteral("yyyy-MM-dd")).toStdString() +
                    " 23:59:59";
        periodName = tr("hoy");
    } else if (period.clickedButton() == weekButton) {
        window.from = today.addDays(-6).toString(QStringLiteral("yyyy-MM-dd")).toStdString() +
                      " 00:00:00";
        window.to = today.toString(QStringLiteral("yyyy-MM-dd")).toStdString() +
                    " 23:59:59";
        periodName = tr("los últimos 7 días");
    }

    int discarded = 0;
    auto listed = inspections_->reportForPiece(
        pieceId, window, repositories::InspectionRepository::kMaxReportRows, &discarded);
    if (!listed.isOk()) {
        QMessageBox::warning(this, tr("No se pudo leer el historial"),
                             QString::fromStdString(listed.error().message));
        return;
    }
    if (listed.value().empty()) {
        QMessageBox::information(
            this, tr("Sin datos"),
            tr("No hay ninguna inspección de esta pieza en %1.").arg(periodName));
        return;
    }

    const QString pieceName =
        pieceCombo_ != nullptr ? pieceCombo_->currentText() : QString();
    std::vector<domain::InspectionRow> rows;
    rows.reserve(listed.value().size());
    for (const auto& entry : listed.value()) {
        domain::InspectionRow row;
        row.startedAt = entry.startedAt;
        row.piece = pieceName.toStdString();
        row.ok = entry.verdict == "OK";
        row.similarity = entry.similarity;
        row.referenceVersion = entry.referenceVersion;
        row.reason = entry.reason;
        rows.push_back(std::move(row));
    }
    const auto summary = domain::summarise(rows);

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar informe del turno"), QStringLiteral("informe_turno.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    // QSaveFile y no QFile: un informe a medio escribir porque se llenó el disco
    // o se quitó el pendrive parece un informe completo, y quien lo abra leerá
    // un turno truncado sin saberlo.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("No se pudo escribir"),
                             tr("No se pudo abrir el archivo para escribir."));
        return;
    }
    {
        QTextStream out(&file);
        out << QString::fromStdString(domain::shiftReportCsv(rows, summary));
    }
    if (!file.commit()) {
        QMessageBox::warning(this, tr("No se pudo escribir"),
                             tr("El archivo no se pudo guardar del todo, así que no se ha "
                                "dejado a medias."));
        return;
    }

    // Y se le enseña el resumen por pantalla, que es lo que iba a mirar de todas
    // formas antes de abrir el fichero.
    QMessageBox done(QMessageBox::Information, tr("Informe del turno"),
                     QString::fromStdString(domain::shiftReportText(rows, summary)),
                     QMessageBox::Ok, this);
    QString note = tr("Guardado en %1 — %2 inspecciones de %3, con sus motivos.")
                       .arg(path)
                       .arg(rows.size())
                       .arg(periodName);
    if (discarded > 0) {
        // Se dice, y no se calla. Un informe recortado da un rendimiento
        // calculado sobre PARTE del periodo con pinta de ser el del periodo
        // entero, y quien lo lea no tiene forma de notarlo.
        note += tr("\n\nATENCIÓN: había %1 inspecciones más en ese periodo y no caben en un "
                   "solo informe. Estas son las %2 últimas, así que el rendimiento de "
                   "arriba es el de ese tramo y no el del periodo completo. Pide un "
                   "periodo más corto para cubrirlo entero.")
                    .arg(discarded)
                    .arg(rows.size());
        done.setIcon(QMessageBox::Warning);
    }
    done.setInformativeText(note);
    done.exec();
}

}  // namespace pci::ui
