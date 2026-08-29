#include "inspection_editor/auto_measure_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
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
        // LA RECETA, arriba de las casillas y no al lado: es lo que se elige
        // primero y lo que decide qué queda marcado debajo.
        //
        // Petición de uso: «un conjunto personalizado de reglas para algunas
        // piezas específicas —engranajes, círculos, piezas cuadradas,
        // rectangulares— para tomar de mejor manera las medidas».
        //
        // Las casillas siguen ahí a propósito. Una receta que solo dijera su
        // nombre sería una caja negra: se ve QUÉ trae, y se puede ajustar sin
        // salir del diálogo.
        auto* recipeRow = new QHBoxLayout();
        recipeRow->addWidget(new QLabel(tr("Receta:"), this));
        recipeBox_ = new QComboBox(this);
        recipeBox_->setObjectName(QStringLiteral("recipeBox"));
        recipes_ = factoryRecipes();
        for (const auto& recipe : recipes_) {
            recipeBox_->addItem(QString::fromStdString(recipe.name));
        }
        recipeBox_->setToolTip(
            tr("Qué se mide en esta clase de pieza. Una receta acota las cotas que\n"
               "se proponen; no fuerza ninguna: si la pieza no es de su familia, se\n"
               "dice en vez de sacar un número que no significa nada."));
        recipeRow->addWidget(recipeBox_);

        // GUARDAR LA MÍA, que es la mitad que faltaba de «un conjunto
        // PERSONALIZADO de reglas»: hasta ahora se podían ajustar las casillas
        // y ese ajuste duraba lo que la sesión.
        //
        // Guarda lo que hay puesto AHORA —la receta base con las casillas tal
        // como estén—, así que el gesto es: elegir la más parecida, quitar y
        // poner lo que haga falta, y ponerle nombre.
        auto* saveRecipe = new QPushButton(tr("Guardar como receta…"), this);
        saveRecipe->setObjectName(QStringLiteral("saveRecipeButton"));
        saveRecipe->setToolTip(
            tr("Guarda las clases que tengas marcadas con un nombre tuyo —«la mía\n"
               "de bridas»— para no volver a marcarlas. Aparecerá en esta lista y se\n"
               "puede asignar a una pieza como cualquier otra."));
        recipeRow->addWidget(saveRecipe);
        recipeRow->addStretch(1);
        layout->addLayout(recipeRow);

        connect(saveRecipe, &QPushButton::clicked, this, [this] {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("Guardar receta"),
                tr("Nombre de la receta (p. ej. «bridas del proveedor B»):"),
                QLineEdit::Normal, QString(), &ok);
            if (!ok || name.trimmed().isEmpty()) {
                return;
            }
            MeasureRecipe mine = chosenRecipe();
            mine.name = name.trimmed().toStdString();
            // Se anota de qué salió: dentro de un mes, «mía de bridas» no dice
            // qué trae ni a qué piezas se aplica, y la frase es lo único que se
            // lee antes de elegirla.
            mine.what = "Receta propia, ajustada a partir de «" + base_.name + "» (" +
                        familyName(mine.family) + ").";
            toSave_ = mine;
            // Quien guarda es la ventana —este diálogo no toca la base—, así que
            // aquí solo se apunta y se dice que se apuntó.
            if (noticeLabel_ != nullptr) {
                noticeLabel_->setText(
                    tr("Se guardará como «%1» al aceptar.").arg(name.trimmed()));
                noticeLabel_->show();
            }
        });

        // Qué trae la receta, en una frase. Sin esto, elegir entre seis nombres
        // es adivinar.
        recipeWhat_ = new QLabel(this);
        recipeWhat_->setObjectName(QStringLiteral("recipeWhat"));
        recipeWhat_->setWordWrap(true);
        layout->addWidget(recipeWhat_);

        auto* filterRow = new QHBoxLayout();
        filterRow->addWidget(new QLabel(tr("Proponer:"), this));
        for (const ToolType type : proposableTypes()) {
            // EL NOMBRE QUE EL OPERADOR LEE, no el que usa la base de datos.
            //
            // Aquí ponía `toolTypeName`, que es la clave con la que una
            // herramienta se guarda —«circle», «point_to_line», «region»— y no
            // lo que nadie llama a esa cota. La fila del filtro decía
            // «caliper  circle  point_to_line  arc  angle  roundness  polygon
            // thread  gear  region» mientras la paleta, a dos palmos, decía
            // «Calibre  Círculo  Punto-Línea…».
            //
            // `toolTypeLabel` es la MISMA lista que usa la paleta, así que las
            // dos superficies no pueden volver a discrepar.
            auto* box = new QCheckBox(QString::fromUtf8(toolTypeLabel(type)), this);
            // Con nombre, y el nombre es la CLAVE de la clase: el rótulo es lo
            // que se lee y puede cambiar, la clave es lo que identifica. Así
            // una prueba puede coger la casilla del Círculo y comprobar QUÉ
            // dice, en vez de buscarla por lo que dice.
            box->setObjectName(QStringLiteral("typeCheck.") +
                               QString::fromLatin1(toolTypeName(type)));
            box->setChecked(true);
            box->setToolTip(tr("Si lo desmarcas, la medición automática deja de\n"
                               "proponer cotas de esta clase — y el tope de\n"
                               "propuestas se reparte entre las que sí quieres."));
            filterRow->addWidget(box);
            typeBoxes_.push_back(box);
            boxTypes_.push_back(type);
            connect(box, &QCheckBox::toggled, this, [this](bool) {
                reproposeWithCurrentRecipe();
            });
        }
        filterRow->addStretch(1);
        layout->addLayout(filterRow);

        // El motivo cuando la receta no va con esta pieza. Vive debajo del
        // filtro y no en un aviso aparte porque es la respuesta a lo que se
        // acaba de tocar.
        noticeLabel_ = new QLabel(this);
        noticeLabel_->setObjectName(QStringLiteral("recipeNotice"));
        noticeLabel_->setWordWrap(true);
        noticeLabel_->hide();
        layout->addWidget(noticeLabel_);

        base_ = recipes_.front();
        recipeWhat_->setText(QString::fromStdString(base_.what));
        connect(recipeBox_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
            const auto chosen =
                std::find_if(recipes_.begin(), recipes_.end(),
                             [&name](const MeasureRecipe& recipe) {
                                 return recipe.name == name.toStdString();
                             });
            if (chosen == recipes_.end()) {
                return;
            }
            base_ = *chosen;
            recipeWhat_->setText(QString::fromStdString(base_.what));
            applyRecipeToBoxes(base_);
            reproposeWithCurrentRecipe();
        });
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

// Las que hay para elegir: las de fábrica primero y las del operador después.
//
// Se añaden en vez de sustituir, y las propias no pueden llamarse como una de
// fábrica —lo impide el repositorio— así que la lista no tiene ambigüedades: el
// nombre sigue identificando UNA receta, que es lo que la pieza guarda.
void AutoMeasureDialog::setRecipes(std::vector<MeasureRecipe> recipes) {
    if (recipeBox_ == nullptr || recipes.empty()) {
        return;
    }
    const QString current = recipeBox_->currentText();
    recipes_ = std::move(recipes);
    QSignalBlocker quiet(recipeBox_);
    recipeBox_->clear();
    for (const auto& recipe : recipes_) {
        recipeBox_->addItem(QString::fromStdString(recipe.name));
    }
    // Se conserva la que estuviera elegida. Sin esto, añadir las recetas
    // guardadas —que pasa al abrir el diálogo— reiniciaría la elección a la
    // primera, y la receta de la pieza se perdería justo al cargarla.
    const int index = recipeBox_->findText(current);
    recipeBox_->setCurrentIndex(index >= 0 ? index : 0);
}

void AutoMeasureDialog::selectRecipe(const std::string& name) {
    if (recipeBox_ == nullptr || name.empty()) {
        return;
    }
    // Se busca en LAS QUE HAY, no solo en las de fábrica: si no, una receta
    // propia guardada en la pieza no se podría recuperar y el operador la vería
    // abrirse siempre con la primera.
    const int index = recipeBox_->findText(QString::fromStdString(name));
    if (index < 0) {
        return;
    }
    recipeBox_->setCurrentIndex(index);
}

MeasureRecipe AutoMeasureDialog::chosenRecipe() const {
    MeasureRecipe recipe = base_;
    recipe.options.allowedTypes = chosenTypes();
    return recipe;
}

void AutoMeasureDialog::applyRecipeToBoxes(const MeasureRecipe& recipe) {
    for (std::size_t i = 0; i < typeBoxes_.size(); ++i) {
        QSignalBlocker blocker(typeBoxes_[i]);
        typeBoxes_[i]->setChecked(recipe.options.allows(boxTypes_[i]));
    }
}

void AutoMeasureDialog::reproposeWithCurrentRecipe() {
    if (!reproposer_) {
        return;
    }
    const RecipeResult result = reproposer_(chosenRecipe());
    proposals_ = result.proposals;
    fillTable();
    updateAcceptLabel();
    if (noticeLabel_ == nullptr) {
        return;
    }
    // SE DICE POR QUÉ, siempre que haya algo que decir. Una tabla vacía sin
    // motivo se lee como «esta pieza no tiene nada que medir», que casi nunca es
    // lo que pasó: lo que pasó es que la receta era para otra familia.
    if (!result.applies || result.proposals.empty()) {
        noticeLabel_->setText(QString::fromStdString(result.why));
        noticeLabel_->setVisible(!result.why.empty());
    } else {
        noticeLabel_->hide();
    }
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
