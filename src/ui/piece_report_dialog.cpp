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
#include <QCheckBox>
#include <QTabWidget>
#include <QBrush>
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
                                     QWidget* parent, std::vector<DrawnTool> drawn)
    : QDialog(parent), report_(std::move(report)), drawn_(std::move(drawn)) {
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
    // LAS DOS PESTAÑAS.
    //
    // Petición de uso: «al momento de darle al botón de medir debería de haber
    // dos pestañas: una donde se vean todas las medidas automáticamente, y otra
    // donde se pueda elegir qué herramientas se utilizan».
    //
    // La separación tiene sentido más allá de la petición: lo de arriba lo mide
    // el programa solo y no se puede tocar; lo de la otra pestaña son las cotas
    // que dibujó el operador, con su tolerancia y su veredicto. Mezclarlas
    // invitaría a buscarle banda a un perímetro.
    auto* tabs = new QTabWidget(this);
    auto* measurements = new QWidget(tabs);
    auto* measurementsLayout = new QVBoxLayout(measurements);
    measurementsLayout->setContentsMargins(0, 0, 0, 0);
    measurementsLayout->addWidget(table_, 1);

    status_ = new QLabel(measurements);
    status_->setWordWrap(true);
    measurementsLayout->addWidget(status_);
    tabs->addTab(measurements, tr("Medidas de la pieza"));
    tabs->addTab(buildToolsTab(), drawn_.empty()
                                      ? tr("Mis herramientas")
                                      : tr("Mis herramientas (%1)").arg(drawn_.size()));
    root->addWidget(tabs, 1);

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

// LAS HERRAMIENTAS DEL OPERADOR: qué miden, si cumplen, y si entran o no.
//
// Tres cosas en una tabla porque son la misma pregunta desde tres lados: qué da
// esta cota, si eso está dentro de lo que declaraste, y si quieres que siga
// contando. Separarlas obligaría a cruzar dos pantallas para una decisión.
//
// El interruptor escribe en `ToolConfig::enabled`, que ya existía, ya se
// respeta al ejecutar y ya se guarda en la base y en las plantillas — pero no
// tenía ningún control que lo cambiara, así que valía `true` siempre. Aquí no
// se inventa un mecanismo: se le pone el interruptor que le faltaba.
QWidget* PieceReportDialog::buildToolsTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    if (drawn_.empty()) {
        auto* empty = new QLabel(
            tr("No hay ninguna herramienta dibujada sobre esta pieza.\n\n"
               "Las cotas de la otra pestaña las mide el programa solo. Para vigilar\n"
               "una en cada inspección, dibújala sobre la pieza o usa el botón\n"
               "«Vigilar las marcadas»."),
            page);
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(QStringLiteral("color:#999; padding:24px;"));
        layout->addWidget(empty);
        return page;
    }

    auto* intro = new QLabel(
        tr("Estas son tus cotas sobre esta pieza. Desmarca las que no quieras que\n"
           "cuenten: dejan de medirse y dejan de pesar en el veredicto, sin tener\n"
           "que borrarlas ni volver a dibujarlas."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* table = new QTableWidget(static_cast<int>(drawn_.size()), 4, page);
    table->setHorizontalHeaderLabels(
        {tr("Cuenta"), tr("Cota"), tr("Mide"), tr("Veredicto")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    for (int row = 0; row < static_cast<int>(drawn_.size()); ++row) {
        const DrawnTool& tool = drawn_[static_cast<std::size_t>(row)];

        // El interruptor va en su propio widget para poder centrarlo: una
        // casilla pegada al borde izquierdo de la celda se lee como parte del
        // nombre de la fila de al lado.
        auto* holder = new QWidget(table);
        auto* holderLayout = new QHBoxLayout(holder);
        holderLayout->setContentsMargins(0, 0, 0, 0);
        holderLayout->setAlignment(Qt::AlignCenter);
        auto* box = new QCheckBox(holder);
        box->setChecked(tool.config.enabled);
        box->setToolTip(tr("Si lo desmarcas, esta cota deja de medirse y deja de\n"
                           "pesar en el veredicto. La herramienta NO se borra:\n"
                           "vuelve en cuanto la marques."));
        holderLayout->addWidget(box);
        table->setCellWidget(row, 0, holder);
        toolSwitches_.push_back(box);
        switchesAtStart_.push_back(tool.config.enabled);

        table->setItem(row, 1,
                       new QTableWidgetItem(QString::fromStdString(tool.config.name)));

        // LO QUE MIDE, con su unidad. Sin unidad, un ángulo y una longitud
        // comparten columna y ninguno de los dos dice de qué es.
        table->setItem(row, 2,
                       new QTableWidgetItem(QString::fromStdString(tool.text)));

        // EL VEREDICTO, Y DE DÓNDE SALE.
        //
        // Petición de uso: «si no cumple, simplemente diga que no cumple en su
        // descripción de medida, o de dónde sale». Un «NG» a secas obliga a ir
        // a buscar la tolerancia en otra pantalla para saber por qué.
        QString verdict;
        if (!tool.config.enabled) {
            verdict = tr("No cuenta: la has desmarcado.");
        } else if (!tool.result.ok && !tool.result.detail.empty()) {
            verdict = tr("NO CUMPLE — %1").arg(QString::fromStdString(tool.result.detail));
        } else if (!tool.result.ok) {
            verdict = tr("NO CUMPLE — mide %1 y se admite entre %2 y %3")
                          .arg(tool.result.measured, 0, 'f', 2)
                          .arg(tool.config.toleranceMin, 0, 'f', 2)
                          .arg(tool.config.toleranceMax, 0, 'f', 2);
        } else {
            verdict = tr("Cumple: entre %1 y %2")
                          .arg(tool.config.toleranceMin, 0, 'f', 2)
                          .arg(tool.config.toleranceMax, 0, 'f', 2);
        }
        auto* verdictItem = new QTableWidgetItem(verdict);
        if (tool.config.enabled && !tool.result.ok) {
            verdictItem->setForeground(QBrush(QColor(198, 40, 40)));
        } else if (!tool.config.enabled) {
            verdictItem->setForeground(QBrush(QColor(150, 150, 150)));
        }
        table->setItem(row, 3, verdictItem);

        // Y la fila entera se apaga cuando la cota no cuenta: si sigue igual de
        // negra que las demás, el operador la lee como vigente.
        connect(box, &QCheckBox::toggled, table, [table, row](bool on) {
            for (int column = 1; column < table->columnCount(); ++column) {
                if (auto* item = table->item(row, column); item != nullptr) {
                    item->setForeground(on ? QBrush(QColor(20, 20, 20))
                                           : QBrush(QColor(150, 150, 150)));
                }
            }
        });
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    return page;
}

std::vector<inspection::ToolConfig> PieceReportDialog::toolsWithChangedState() const {
    std::vector<inspection::ToolConfig> changed;
    for (std::size_t i = 0; i < toolSwitches_.size() && i < drawn_.size(); ++i) {
        const bool now = toolSwitches_[i]->isChecked();
        if (now == switchesAtStart_[i]) {
            continue;
        }
        inspection::ToolConfig config = drawn_[i].config;
        config.enabled = now;
        changed.push_back(std::move(config));
    }
    return changed;
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
