#include "ui/piece_report_dialog.h"

#include <QClipboard>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

#include "ui/dialog_geometry.h"

namespace pci::ui {

namespace {

constexpr int kColumnName = 0;
constexpr int kColumnValue = 1;
constexpr int kColumnUnit = 2;
constexpr int kColumnTolerance = 3;
constexpr int kColumnDetail = 4;

// Fila de separación entre los dos bloques. Se hace con una fila de la propia
// tabla y no con dos tablas: dos tablas se desplazan por separado, y comparar
// una cota con el tamaño total de la pieza obliga a verlas a la vez.
void addSectionRow(QTableWidget* table, int row, const QString& title) {
    auto* item = new QTableWidgetItem(title);
    QFont bold = item->font();
    bold.setBold(true);
    item->setFont(bold);
    item->setFlags(Qt::ItemIsEnabled);
    table->setItem(row, kColumnName, item);
    table->setSpan(row, kColumnName, 1, 5);
}

}  // namespace

PieceReportDialog::PieceReportDialog(inspection::PieceReport report,
                                     const QString& sourceLabel,
                                     repositories::SettingsRepository* settings,
                                     QWidget* parent)
    : QDialog(parent), report_(std::move(report)) {
    setWindowTitle(tr("Medidas de la pieza"));
    keepDialogSize(*this, settings, QStringLiteral("piecereport"), 820, 620);

    auto* root = new QVBoxLayout(this);

    // El titular dice QUÉ figura se reconoció y con qué se midió. Sin lo
    // primero, las cotas de abajo parecen llovidas del cielo; sin lo segundo,
    // no se sabe si son de la cámara o de una imagen abierta hace media hora.
    auto* headline = new QLabel(
        tr("<b>%1</b> — medido sobre %2")
            .arg(QString::fromStdString(report_.headline), sourceLabel),
        this);
    headline->setWordWrap(true);
    root->addWidget(headline);

    // LOS AVISOS, antes que las cifras y no debajo.
    //
    // Un aviso que dice «estas medidas son límites inferiores» puesto al final
    // llega cuando ya se han leído las cifras, y entonces no cambia nada. Va
    // arriba, con color, y con el texto entero: quien lo lea tiene que poder
    // decidir si sigue mirando o va a recolocar la pieza.
    for (const auto& warning : report_.warnings) {
        auto* label = new QLabel(QString::fromStdString(warning), this);
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("color:#3a2a00; background:#ffc861;"
                                            " border-radius:6px; padding:6px;"));
        root->addWidget(label);
    }

    if (!report_.shape.reason.empty()) {
        // Por qué se reconoció esa figura, con su residuo. Una clasificación sin
        // su número es una opinión.
        auto* why = new QLabel(QString::fromStdString(report_.shape.reason), this);
        why->setWordWrap(true);
        why->setStyleSheet(QStringLiteral("color:#9aa0a6;"));
        root->addWidget(why);
    }

    const int facts = static_cast<int>(report_.contourFactCount());
    const int dimensions = static_cast<int>(report_.rows.size()) - facts;
    // Dos filas de más: los dos títulos de bloque.
    table_ = new QTableWidget(static_cast<int>(report_.rows.size()) + 2, 5, this);
    table_->setHorizontalHeaderLabels(
        {tr("Medida"), tr("Valor"), tr("Unidad"), tr("Tolerancia"), tr("De dónde sale")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(kColumnDetail, QHeaderView::Stretch);

    int row = 0;
    addSectionRow(table_, row++, tr("El contorno de la pieza"));
    bool sectionDone = false;
    for (const auto& entry : report_.rows) {
        if (!sectionDone && entry.group == inspection::kGroupDimension) {
            addSectionRow(table_, row++,
                          tr("Cotas (%n), deducidas de la forma", nullptr, dimensions));
            sectionDone = true;
        }
        table_->setItem(row, kColumnName,
                        new QTableWidgetItem(QString::fromStdString(entry.tool)));
        // Los recuentos sin decimales: «6,00 agujeros» invita a leer un recuento
        // como una magnitud continua.
        const bool isCount = entry.unit == "n";
        table_->setItem(row, kColumnValue,
                        new QTableWidgetItem(QString::number(entry.value, 'f',
                                                             isCount ? 0 : 2)));
        table_->setItem(row, kColumnUnit,
                        new QTableWidgetItem(QString::fromStdString(entry.unit)));
        table_->setItem(
            row, kColumnTolerance,
            new QTableWidgetItem(entry.hasTolerance
                                     ? QStringLiteral("%1 … %2")
                                           .arg(entry.toleranceMin, 0, 'f', 2)
                                           .arg(entry.toleranceMax, 0, 'f', 2)
                                     : QStringLiteral("—")));
        table_->setItem(row, kColumnDetail,
                        new QTableWidgetItem(QString::fromStdString(entry.detail)));
        ++row;
    }
    if (!sectionDone && dimensions == 0) {
        addSectionRow(table_, row,
                      tr("No se ha podido deducir ninguna cota de esta forma."));
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setSectionResizeMode(kColumnDetail, QHeaderView::Stretch);
    root->addWidget(table_, 1);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    root->addWidget(status_);

    auto* buttons = new QHBoxLayout();
    auto* watch = new QPushButton(tr("Vigilar estas cotas"), this);
    watch->setEnabled(!report_.watchable.empty());
    watch->setToolTip(
        tr("Convierte las cotas en herramientas de la pieza, con sus tolerancias ya\n"
           "sugeridas, para que cada inspección las compruebe.\n\n"
           "Medir y vigilar son dos decisiones distintas: por eso esto no pasa solo\n"
           "cada vez que consultas las medidas."));
    buttons->addWidget(watch);
    buttons->addStretch(1);

    auto* copy = new QPushButton(tr("Copiar"), this);
    copy->setToolTip(tr("Copia la tabla como texto alineado, para un correo o un parte."));
    buttons->addWidget(copy);
    auto* exportCsv = new QPushButton(tr("Exportar CSV…"), this);
    exportCsv->setToolTip(
        tr("Guarda las medidas en columnas que una hoja de cálculo puede sumar."));
    buttons->addWidget(exportCsv);
    auto* close = new QPushButton(tr("Cerrar"), this);
    buttons->addWidget(close);
    root->addLayout(buttons);

    connect(watch, &QPushButton::clicked, this, &PieceReportDialog::onWatchClicked);
    connect(copy, &QPushButton::clicked, this, &PieceReportDialog::onCopyClicked);
    connect(exportCsv, &QPushButton::clicked, this, &PieceReportDialog::onExportClicked);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
}

void PieceReportDialog::onCopyClicked() {
    QGuiApplication::clipboard()->setText(
        QString::fromStdString(inspection::measurementsToText(report_.rows, report_.warnings)));
    status_->setStyleSheet(QStringLiteral("color:#22cc44;"));
    status_->setText(tr("%n medida(s) copiadas al portapapeles.", nullptr,
                        static_cast<int>(report_.rows.size())));
}

void PieceReportDialog::onExportClicked() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar medidas"), QStringLiteral("medidas_pieza.csv"),
        tr("CSV (*.csv);;Todos (*)"));
    if (path.isEmpty()) {
        return;  // cancelar no es un error
    }
    const std::string csv = inspection::measurementsToCsv(report_.rows, report_.warnings);
    // `QSaveFile`: escribe a un temporal y renombra al cerrar, así que un fallo
    // a mitad no deja el fichero anterior medio sobrescrito.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) ||
        file.write(csv.data(), static_cast<qint64>(csv.size())) < 0 || !file.commit()) {
        QMessageBox::warning(this, tr("No se pudo exportar"),
                             tr("No se pudo escribir en %1: %2").arg(path, file.errorString()));
        return;
    }
    status_->setStyleSheet(QStringLiteral("color:#22cc44;"));
    status_->setText(tr("Medidas exportadas a %1.").arg(path));
}

void PieceReportDialog::onWatchClicked() {
    toWatch_ = report_.watchable;
    accept();
}

}  // namespace pci::ui
