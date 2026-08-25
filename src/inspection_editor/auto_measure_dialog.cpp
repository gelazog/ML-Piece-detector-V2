#include "inspection_editor/auto_measure_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QCheckBox>
#include <QSignalBlocker>
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

AutoMeasureDialog::AutoMeasureDialog(std::vector<AutoProposal> proposals, double mmPerPixel,
                                     QWidget* parent, Reproposer reproposer)
    : QDialog(parent),
      proposals_(std::move(proposals)),
      mmPerPixel_(mmPerPixel),
      reproposer_(std::move(reproposer)) {
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

    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels(
        {tr(""), tr("Medida"), tr("Valor"), tr("Tolerancia"), tr("Por qué se propone")});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // QUÉ CLASES DE COTA PROPONER.
    //
    // Petición de uso: poder elegir qué herramientas entran en la medición
    // automática. El proponedor ofrece hasta doce cotas de siete clases, y quien
    // solo mide diámetros acaba desmarcando nueve cada vez.
    //
    // Al cambiar una casilla se VUELVE A PROPONER en vez de esconder filas, y
    // eso importa: el recorte a doce se aplica después de filtrar. Escondiendo
    // filas quedarían tres diámetros, porque los otros nueve huecos se los
    // habrían comido cotas que no se querían.
    if (reproposer_) {
        auto* filterRow = new QHBoxLayout();
        filterRow->addWidget(new QLabel(tr("Proponer:"), this));
        for (const ToolType type : proposableTypes()) {
            auto* box = new QCheckBox(QString::fromStdString(toolTypeName(type)), this);
            box->setChecked(true);
            box->setToolTip(tr("Si lo desmarcas, la medición automática deja de\n"
                               "proponer cotas de esta clase — y el tope de\n"
                               "propuestas se reparte entre las que sí quieres."));
            filterRow->addWidget(box);
            typeBoxes_.push_back(box);
            boxTypes_.push_back(type);
            connect(box, &QCheckBox::toggled, this, [this](bool) {
                proposals_ = reproposer_(chosenTypes());
                fillTable();
                updateAcceptLabel();
            });
        }
        filterRow->addStretch(1);
        layout->addLayout(filterRow);
    }

    fillTable();
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

// Rehace la tabla con las propuestas que haya ahora.
//
// Se saca del constructor porque el filtro de clases la reconstruye: al cambiar
// qué cotas se quieren, se vuelve a proponer y la lista es otra.
void AutoMeasureDialog::fillTable() {
    const QSignalBlocker quiet(table_);
    table_->clearContents();
    table_->setRowCount(static_cast<int>(proposals_.size()));

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
        //
        // El número lleva SU unidad. Antes esta columna era un `QString::number`
        // pelado, y en ella convivían píxeles, grados y recuentos: un hexágono
        // decía «6.00» donde el de al lado decía «203.15», y ninguno de los dos
        // decía de qué. La unidad estaba solo en el tooltip, que es tanto como
        // no estar.
        ToolRunResult reading;
        reading.measured = proposal.measured;
        reading.kind = proposal.kind;
        reading.type = proposal.config.type;
        auto* value = new QTableWidgetItem(
            QString::fromStdString(formatMeasure(reading, mmPerPixel_, LengthUnit::Auto)));
        value->setToolTip(QString::fromStdString(proposal.detail));
        table_->setItem(row, kColumnMeasured, value);

        // La tolerancia va en la misma unidad que la medida, por lo mismo: una
        // banda «5.4 … 6.6» junto a un valor en milímetros se lee en
        // milímetros, y para un recuento de lados no lo era.
        ToolRunResult low = reading;
        low.measured = proposal.config.toleranceMin;
        ToolRunResult high = reading;
        high.measured = proposal.config.toleranceMax;
        table_->setItem(
            row, kColumnTolerance,
            new QTableWidgetItem(
                QStringLiteral("%1 … %2")
                    .arg(QString::fromStdString(
                        formatMeasure(low, mmPerPixel_, LengthUnit::Auto, true)))
                    .arg(QString::fromStdString(
                        formatMeasure(high, mmPerPixel_, LengthUnit::Auto, true)))));
        table_->setItem(row, kColumnReason,
                        new QTableWidgetItem(QString::fromStdString(proposal.reason)));
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setSectionResizeMode(kColumnReason, QHeaderView::Stretch);
}

std::vector<ToolType> AutoMeasureDialog::chosenTypes() const {
    std::vector<ToolType> chosen;
    for (std::size_t i = 0; i < typeBoxes_.size(); ++i) {
        if (typeBoxes_[i]->isChecked()) {
            chosen.push_back(boxTypes_[i]);
        }
    }
    // TODAS MARCADAS SE DEVUELVE COMO VACÍO, que es lo que `ProposeOptions`
    // entiende por «sin filtro». Si no, marcar todas y no marcar ninguna
    // acabarían pasando listas distintas para la misma intención.
    if (chosen.size() == typeBoxes_.size()) {
        return {};
    }
    return chosen;
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
