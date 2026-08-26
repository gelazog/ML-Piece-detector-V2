#include "ui/piece_report_dialog.h"
#include "ui/theme.h"
#include "inspection_editor/tools/tool_geometry.h"
#include <algorithm>

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
#include <QTreeWidget>
#include <QFont>
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
        label->setStyleSheet(theme::noticeStyle(theme::kWarn, theme::kWarnField));
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
               "Las cotas de la otra pestana las mide el programa solo. Para vigilar\n"
               "una en cada inspeccion, dibujala sobre la pieza o usa el boton\n"
               "«Vigilar estas cotas»."),
            page);
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(
            theme::textStyle(theme::kInkOff, QStringLiteral("padding:24px;")));
        layout->addWidget(empty);
        return page;
    }

    auto* intro = new QLabel(
        tr("Tus cotas, agrupadas por la clase de herramienta que las mide. Abre una\n"
           "clase para ver todas las veces que la has usado, y abre un uso para ver\n"
           "todo lo que esa misma figura puede medir."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* tree = new QTreeWidget(page);
    tree->setColumnCount(4);
    tree->setHeaderLabels({tr("Cuenta"), tr("Cota"), tr("Mide"), tr("Veredicto")});
    tree->setRootIsDecorated(true);
    tree->setUniformRowHeights(false);
    tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);

    // Los tres, de la paleta. El rojo de antes era el CUARTO distinto que la
    // aplicación usaba para «no cumple».
    const QBrush grey(theme::color(theme::kInkOff));
    const QBrush black(theme::color(theme::kInk));
    const QBrush red(theme::color(theme::kBad));

    // NIVEL 1: LA CLASE DE HERRAMIENTA.
    //
    // Peticion de uso: «que las separases por la herramienta en cuestion que se
    // esta usando, y luego que se desglose todas las veces que se uso esa
    // herramienta».
    //
    // Tiene sentido mas alla de la peticion: doce cotas seguidas con nombres que
    // genera el proponedor —«Lado 1», «Lado 2», «Radio 3»— se leen como una
    // lista plana donde no se ve QUE clase de medida domina la pieza. Agrupadas,
    // «Calibre (7)» dice de un vistazo que la pieza se esta midiendo a
    // distancias y a casi nada mas.
    //
    // El orden de las clases es el de la PRIMERA aparicion, no alfabetico: asi
    // la pestana se parece al orden en que el operador fue dibujando, que es el
    // que tiene en la cabeza.
    std::vector<inspection::ToolType> orderOfTypes;
    for (const auto& tool : drawn_) {
        if (std::find(orderOfTypes.begin(), orderOfTypes.end(), tool.config.type) ==
            orderOfTypes.end()) {
            orderOfTypes.push_back(tool.config.type);
        }
    }

    for (const auto type : orderOfTypes) {
        std::vector<std::size_t> uses;
        int failing = 0;
        int off = 0;
        for (std::size_t i = 0; i < drawn_.size(); ++i) {
            if (drawn_[i].config.type != type) {
                continue;
            }
            uses.push_back(i);
            if (!drawn_[i].config.enabled) {
                ++off;
            } else if (!drawn_[i].result.ok) {
                ++failing;
            }
        }
        if (uses.empty()) {
            continue;
        }

        auto* family = new QTreeWidgetItem(tree);
        family->setText(1, tr("%1 (%2)")
                               .arg(QString::fromUtf8(inspection::toolTypeLabel(type)))
                               .arg(uses.size()));
        QFont heading = family->font(1);
        heading.setBold(true);
        family->setFont(1, heading);

        // EL RESUMEN DE LA CLASE, EN LA FILA DE LA CLASE: cuantas de sus cotas no
        // cumplen. Sin esto habria que abrir cada clase para saber si dentro hay
        // algo rojo, que es justo lo que agrupar tenia que evitar.
        if (failing > 0) {
            family->setText(3, tr("%n cota(s) NO cumplen", nullptr, failing));
            family->setForeground(3, red);
        } else if (off == static_cast<int>(uses.size())) {
            family->setText(3, tr("ninguna cuenta: las has desmarcado todas"));
            family->setForeground(3, grey);
        } else {
            family->setText(3, tr("todas cumplen"));
        }

        for (const std::size_t index : uses) {
            const DrawnTool& tool = drawn_[index];

            // NIVEL 2: CADA USO DE ESA CLASE.
            auto* use = new QTreeWidgetItem(family);
            use->setText(1, QString::fromStdString(tool.config.name));
            use->setText(2, QString::fromStdString(tool.text));

            auto* box = new QCheckBox(tree);
            box->setChecked(tool.config.enabled);
            box->setToolTip(tr("Si lo desmarcas, esta cota deja de medirse y deja de\n"
                               "pesar en el veredicto. La herramienta NO se borra:\n"
                               "vuelve en cuanto la marques."));
            tree->setItemWidget(use, 0, box);
            toolSwitches_.push_back({box, index});
            switchesAtStart_.push_back(tool.config.enabled);

            // EL VEREDICTO, Y DE DONDE SALE.
            QString verdict;
            if (!tool.config.enabled) {
                verdict = tr("No cuenta: la has desmarcado.");
            } else if (!tool.result.ok && !tool.result.detail.empty()) {
                verdict =
                    tr("NO CUMPLE — %1").arg(QString::fromStdString(tool.result.detail));
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
            use->setText(3, verdict);
            if (tool.config.enabled && !tool.result.ok) {
                use->setForeground(3, red);
            } else if (!tool.config.enabled) {
                for (int column = 1; column < 4; ++column) {
                    use->setForeground(column, grey);
                }
            }

            // NIVEL 3: TODAS LAS MEDIDAS DE ESA MISMA FIGURA.
            //
            // La que ya mide sale marcada y sin interruptor propio: SU
            // interruptor es el de arriba. Darle uno segundo pondria dos casillas
            // para el mismo hecho, y una de las dos tendria que mentir.
            for (const auto& other : tool.alsoMeasures) {
                auto* child = new QTreeWidgetItem(use);
                child->setText(1, QString::fromStdString(other.label));
                child->setText(2, QString::fromStdString(other.text));

                if (other.isTheOneItMeasures) {
                    child->setText(3, tr("Es la que mide esta herramienta."));
                    for (int column = 1; column < 4; ++column) {
                        child->setForeground(column,
                                            QBrush(theme::color(theme::kInkMuted)));
                    }
                    QFont bold = child->font(1);
                    bold.setBold(true);
                    child->setFont(1, bold);
                    continue;
                }

                // LAS OTRAS NO LLEVAN VEREDICTO, y no es un olvido: nadie ha
                // declarado tolerancia para ellas. Poner «Cumple» sobre una banda
                // que no existe seria inventarse una conformidad.
                child->setText(3, tr("Sin tolerancia declarada — marcala para vigilarla."));
                for (int column = 1; column < 4; ++column) {
                    child->setForeground(column, QBrush(theme::color(theme::kInkOff)));
                }

                auto* add = new QCheckBox(tree);
                add->setToolTip(
                    tr("Anade esta medida como cota nueva sobre la MISMA figura,\n"
                       "sin volver a dibujarla. Tendras que declararle su\n"
                       "tolerancia igual que a cualquier otra."));
                tree->setItemWidget(child, 0, add);
                siblingBoxes_.push_back(
                    {add, MeasureToAdd{static_cast<int>(index), other.value, other.label}});
            }

            use->setExpanded(false);

            // Y la fila entera se apaga cuando la cota no cuenta: si sigue igual
            // de negra que las demas, el operador la lee como vigente.
            connect(box, &QCheckBox::toggled, tree, [use, black, grey](bool on) {
                for (int column = 1; column < 4; ++column) {
                    use->setForeground(column, on ? black : grey);
                }
            });
        }

        // LA CLASE SE ABRE SI HAY ALGO QUE MIRAR DENTRO. Con una sola cota, o con
        // algo que no cumple, se abre; con siete que cumplen se deja cerrada y el
        // resumen de la fila basta.
        family->setExpanded(failing > 0 || uses.size() == 1);
    }

    for (int column = 0; column < 3; ++column) {
        tree->resizeColumnToContents(column);
    }
    layout->addWidget(tree, 1);
    return page;
}


std::vector<PieceReportDialog::MeasureToAdd> PieceReportDialog::measuresToAdd() const {
    std::vector<MeasureToAdd> wanted;
    for (const auto& sibling : siblingBoxes_) {
        if (sibling.box->isChecked()) {
            wanted.push_back(sibling.what);
        }
    }
    return wanted;
}

std::vector<inspection::ToolConfig> PieceReportDialog::toolsWithChangedState() const {
    std::vector<inspection::ToolConfig> changed;
    for (std::size_t i = 0; i < toolSwitches_.size() && i < switchesAtStart_.size(); ++i) {
        const bool now = toolSwitches_[i].box->isChecked();
        if (now == switchesAtStart_[i]) {
            continue;
        }
        // POR EL INDICE QUE LLEVA LA CASILLA, no por su posicion en el vector:
        // al agrupar por clase las casillas se crean en otro orden que `drawn_`.
        const std::size_t which = toolSwitches_[i].tool;
        if (which >= drawn_.size()) {
            continue;
        }
        inspection::ToolConfig config = drawn_[which].config;
        config.enabled = now;
        changed.push_back(std::move(config));
    }
    return changed;
}

void PieceReportDialog::onCopyClicked() {
    QGuiApplication::clipboard()->setText(
        QString::fromStdString(inspection::measurementsToText(report_.rows, report_.warnings)));
    status_->setStyleSheet(theme::textStyle(theme::kGood));
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
    status_->setStyleSheet(theme::textStyle(theme::kGood));
    status_->setText(tr("Medidas exportadas a %1.").arg(path));
}

void PieceReportDialog::onWatchClicked() {
    // LO QUE YA TIENES NO SE VUELVE A AÑADIR.
    //
    // Esto se llevaba TODAS las propuestas sin mirar nada, y los nombres que
    // genera el proponedor son deterministas —«Ø», «Largo total», «Lado 1»…—,
    // así que pulsarlo dos veces sobre la misma pieza duplicaba cada cota.
    // Reportado por un operador: «se duplicaron las herramientas».
    //
    // El botón decía «vigilar las MARCADAS» cuando no había nada que marcar; ahora
    // al menos lo que ya existe se queda fuera, y se dice cuántas.
    toWatch_.clear();
    int already = 0;
    for (const auto& proposal : report_.watchable) {
        bool have = false;
        for (const auto& tool : drawn_) {
            if (tool.config.name == proposal.config.name) {
                have = true;
                break;
            }
        }
        if (have) {
            ++already;
            continue;
        }
        toWatch_.push_back(proposal);
    }
    if (toWatch_.empty()) {
        // No se cierra: cerrar sin añadir nada y sin decir por qué se lee como
        // que se añadieron.
        status_->setStyleSheet(theme::textStyle(theme::kWarn));
        status_->setText(tr("No se ha añadido ninguna: ya tienes las %1 cotas que se "
                            "proponen. Mira la pestaña «Mis herramientas».")
                             .arg(already));
        return;
    }
    if (already > 0) {
        status_->setText(tr("Se añaden %1; otras %2 ya las tenías.")
                             .arg(toWatch_.size())
                             .arg(already));
    }
    accept();
}

}  // namespace pci::ui
