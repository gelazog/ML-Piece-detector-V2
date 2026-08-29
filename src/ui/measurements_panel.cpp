#include "ui/measurements_panel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

#include "ui/theme.h"

namespace pci::ui {
namespace {

constexpr int kColumnEye = 0;
constexpr int kColumnPiece = 1;
constexpr int kColumnName = 2;
constexpr int kColumnValue = 3;
constexpr int kColumnBand = 4;
constexpr int kColumnState = 5;
constexpr int kColumnDelete = 6;

// La banda declarada, tal como se escribe en la plantilla. Sin ella, un número
// en una tabla no dice si cumple por poco o por mucho: sólo se ve el veredicto,
// que es la respuesta sin el porqué.
QString bandText(const inspection::ToolConfig& config, double mmPerPixel,
                 inspection::LengthUnit unit, inspection::MeasuredKind kind) {
    // La banda se guarda en las mismas unidades que la medida, así que se
    // formatea con el mismo camino: se arma un resultado de mentira con cada
    // extremo y se le pide a `formatMeasure` que lo escriba. Escribirlo aquí a
    // mano sería la quinta copia de la regla de unidades, y las otras cuatro se
    // equivocaron igual.
    const auto write = [&](double value) {
        inspection::ToolRunResult fake;
        fake.kind = kind;
        fake.measured = value;
        return QString::fromStdString(
            inspection::formatMeasure(fake, mmPerPixel, unit, true));
    };
    // Un máximo de 1e9 es el «sin tope» que pone el editor cuando la tolerancia
    // no se ha tocado. Escribirlo como «1000000000 px» sería ruido.
    const bool openTop = config.toleranceMax >= 1e8;
    if (config.toleranceMin <= 0.0 && openTop) {
        return QStringLiteral("—");
    }
    if (openTop) {
        return QStringLiteral("≥ %1").arg(write(config.toleranceMin));
    }
    return QStringLiteral("%1 … %2").arg(write(config.toleranceMin), write(config.toleranceMax));
}

// UN BOTÓN DE FILA: el ojo y la papelera.
//
// Planos y sin marco para que la tabla siga leyéndose como una tabla —catorce
// botones con relieve serían catorce llamadas de atención—, pero con 24 px de
// lado, que es el mínimo cómodo con ratón a 60 cm.
QToolButton* rowButton(const QString& glyph, const QString& tip, bool checkable) {
    auto* button = new QToolButton();
    button->setText(glyph);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setCheckable(checkable);
    button->setFixedSize(24, 24);
    return button;
}

}  // namespace

MeasurementsPanel::MeasurementsPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    // QUÉ PIEZA SE SUPERVISA, arriba del todo.
    //
    // Va primero porque decide qué significa todo lo de abajo, que es la misma
    // regla que ya sigue la pestaña de piezas: el modo antes que sus ajustes.
    auto* pieceRow = new QHBoxLayout();
    pieceRow->addWidget(new QLabel(tr("Pieza:"), this));
    pieceBox_ = new QComboBox(this);
    pieceBox_->setObjectName(QStringLiteral("piecePicker"));
    pieceBox_->setToolTip(
        tr("Cuál de las piezas del encuadre se está midiendo. Es la misma\n"
           "elección que hacen las flechas de la barra y el mosaico: no hay\n"
           "dos estados distintos que puedan discrepar."));
    pieceRow->addWidget(pieceBox_, 1);
    root->addLayout(pieceRow);
    connect(pieceBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) {
            return;
        }
        const int piece = pieceBox_->itemData(index).toInt();
        chosenPiece_ = piece;
        rebuild();
        emit pieceChosen(piece);
    });

    table_ = new QTableWidget(0, 7, this);
    table_->setObjectName(QStringLiteral("measurementsTable"));
    table_->setHorizontalHeaderLabels({QString(), tr("Pieza"), tr("Cota"), tr("Valor"),
                                       tr("Banda"), tr("Estado"), QString()});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->horizontalHeader()->setSectionResizeMode(kColumnValue, QHeaderView::Stretch);
    root->addWidget(table_, 1);

    // PULSAR UNA FILA REMARCA ESA COTA sobre la imagen.
    //
    // Con catorce cotas encima de la pieza, saber cuál es cuál a ojo no se puede.
    // La tabla y el dibujo son la misma información en dos sitios, así que
    // señalar en uno tiene que señalar en el otro.
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = table_->currentRow();
        if (row < 0 || table_->item(row, kColumnName) == nullptr) {
            return;
        }
        const auto toolId = table_->item(row, kColumnName)->data(Qt::UserRole).toLongLong();
        if (toolId >= 0) {
            emit toolChosen(toolId);
        }
    });

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("measurementsSummary"));
    summary_->setWordWrap(true);
    root->addWidget(summary_);
}

int MeasurementsPanel::rowCount() const { return table_->rowCount(); }

void MeasurementsPanel::setChosenPiece(int pieceIndex) {
    if (pieceIndex == chosenPiece_) {
        return;
    }
    chosenPiece_ = pieceIndex;
    rebuild();
}

void MeasurementsPanel::setResults(const std::vector<inspection::ToolRunResult>& results,
                                   const std::vector<inspection::ToolConfig>& configs,
                                   double mmPerPixel, inspection::LengthUnit unit) {
    results_ = results;
    configs_ = configs;
    mmPerPixel_ = mmPerPixel;
    unit_ = unit;
    rebuild();
}

void MeasurementsPanel::rebuild() {
    // Qué piezas hay, para el desplegable. Se rehace con cada análisis porque el
    // encuadre cambia: una pieza que se fue no puede quedarse en la lista.
    std::set<int> pieces;
    for (const auto& result : results_) {
        pieces.insert(result.pieceIndex);
    }
    {
        const QSignalBlocker quiet(pieceBox_);
        const int wanted = chosenPiece_;
        pieceBox_->clear();
        // «Todas» primero: con una sola pieza es lo mismo que elegirla, y con
        // seis es lo que se quiere ver al empezar.
        pieceBox_->addItem(tr("Todas (%1)").arg(pieces.size()), -1);
        for (const int piece : pieces) {
            pieceBox_->addItem(tr("Pieza %1").arg(piece + 1), piece);
        }
        const int index = pieceBox_->findData(wanted);
        pieceBox_->setCurrentIndex(index >= 0 ? index : 0);
        chosenPiece_ = pieceBox_->currentData().toInt();
        // Con una sola pieza el desplegable no elige nada: se deja a la vista
        // —para que no aparezca y desaparezca— pero apagado, que es lo que dice
        // «aquí no hay nada que elegir todavía».
        pieceBox_->setEnabled(pieces.size() > 1);
    }

    std::vector<const inspection::ToolRunResult*> shown;
    for (const auto& result : results_) {
        if (chosenPiece_ < 0 || result.pieceIndex == chosenPiece_) {
            shown.push_back(&result);
        }
    }

    const QSignalBlocker quiet(table_);
    table_->setRowCount(static_cast<int>(shown.size()));
    // La columna de la pieza sólo dice algo cuando se ven todas.
    table_->setColumnHidden(kColumnPiece, chosenPiece_ >= 0);

    int ok = 0;
    int failing = 0;
    int withoutANumber = 0;
    for (int row = 0; row < static_cast<int>(shown.size()); ++row) {
        const inspection::ToolRunResult& result = *shown[static_cast<std::size_t>(row)];
        const auto config = std::find_if(
            configs_.begin(), configs_.end(),
            [&result](const inspection::ToolConfig& c) { return c.id == result.toolId; });
        const bool visible = std::find(hidden_.begin(), hidden_.end(), result.toolId) ==
                             hidden_.end();

        // EL OJO: si esta cota se dibuja sobre la pieza.
        //
        // Ocultarla no la deja de medir —sigue en la tabla, con su veredicto—,
        // así que apagar el dibujo no puede confundirse con apagar la cota. Eso
        // último ya existe y es otra cosa: el interruptor del informe.
        auto* eye = rowButton(visible ? QStringLiteral("👁") : QStringLiteral("—"),
                              tr("Dibujar esta cota sobre la pieza.\n\n"
                                 "Apagarla no deja de medirla: sigue aquí con su veredicto.\n"
                                 "Sirve para no tapar la imagen cuando hay muchas."),
                              true);
        eye->setChecked(visible);
        const std::int64_t toolId = result.toolId;
        connect(eye, &QToolButton::toggled, this, [this, toolId, eye](bool on) {
            eye->setText(on ? QStringLiteral("👁") : QStringLiteral("—"));
            auto at = std::find(hidden_.begin(), hidden_.end(), toolId);
            if (on && at != hidden_.end()) {
                hidden_.erase(at);
            } else if (!on && at == hidden_.end()) {
                hidden_.push_back(toolId);
            }
            emit overlayVisibilityChanged(toolId, on);
        });
        table_->setCellWidget(row, kColumnEye, eye);

        auto* piece = new QTableWidgetItem(QString::number(result.pieceIndex + 1));
        piece->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, kColumnPiece, piece);

        auto* name = new QTableWidgetItem(QString::fromStdString(result.name));
        // El id viaja EN LA FILA: emparejar por posición se rompió una vez en el
        // informe de pieza —«Ø» apagaba «alto»— al reordenar la tabla.
        name->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(result.toolId));
        table_->setItem(row, kColumnName, name);

        // EL VALOR, O EL MOTIVO POR EL QUE NO LO HAY.
        //
        // Ninguna herramienta se calla —está medido sobre las 32— pero esa
        // explicación no se leía en ningún sitio mientras se trabaja. Ocupa la
        // celda del valor porque es lo que responde a la pregunta que se estaba
        // haciendo.
        auto* value = new QTableWidgetItem(
            result.ok || result.measured != 0.0
                ? QString::fromStdString(
                      inspection::formatMeasure(result, mmPerPixel_, unit_, true))
                : QString::fromStdString(result.detail));
        value->setToolTip(QString::fromStdString(result.detail));
        table_->setItem(row, kColumnValue, value);

        table_->setItem(row, kColumnBand,
                        new QTableWidgetItem(config != configs_.end()
                                                 ? bandText(*config, mmPerPixel_, unit_,
                                                            result.kind)
                                                 : QStringLiteral("—")));

        // «¿QUÉ ES OK A SECAS?» — pregunta literal del taller, y tenía razón.
        //
        // «OK» dice que cumple y no dice por cuánto, que es lo que hace falta
        // para saber si la pieza va justa o sobrada. Ahora la celda dice el
        // estado EN PALABRAS y, cuando hay banda, cuánto margen queda —o cuánto
        // se pasa—. El veredicto sigue yendo también en color, pero el color no
        // es lo único: en blanco y negro, o con un daltónico delante, el texto
        // sigue ahí.
        QString state;
        if (result.informative) {
            state = tr("—");  // una construcción no juzga nada
        } else if (!result.ok && result.measured == 0.0) {
            state = tr("No mide");
        } else if (config != configs_.end() && config->toleranceMax < 1e8) {
            const double toLow = result.measured - config->toleranceMin;
            const double toHigh = config->toleranceMax - result.measured;
            const double margin = std::min(toLow, toHigh);
            inspection::ToolRunResult asLength = result;
            asLength.measured = std::abs(margin);
            const QString amount = QString::fromStdString(
                inspection::formatMeasure(asLength, mmPerPixel_, unit_, true));
            state = result.ok ? tr("Cumple, margen %1").arg(amount)
                              : tr("No cumple, se pasa %1").arg(amount);
        } else {
            state = result.ok ? tr("Cumple") : tr("No cumple");
        }
        auto* verdict = new QTableWidgetItem(state);
        if (!result.informative) {
            verdict->setForeground(QColor(result.ok ? theme::kGood : theme::kBad));
        }
        table_->setItem(row, kColumnState, verdict);

        // BORRAR, con la papelera en su propia columna y no en un menú: «que
        // puedas borrar si quieres la medida, por si se satura de más». Quien
        // borra es la ventana, que tiene el deshacer.
        auto* remove = rowButton(QStringLiteral("✕"),
                                 tr("Quitar esta cota de la pieza.\n\n"
                                    "Se puede deshacer con Ctrl+Z, como cualquier otro\n"
                                    "borrado de herramientas."),
                                 false);
        connect(remove, &QToolButton::clicked, this,
                [this, toolId] { emit deleteRequested(toolId); });
        table_->setCellWidget(row, kColumnDelete, remove);

        if (result.informative) {
            continue;
        }
        if (result.ok) {
            ++ok;
        } else {
            ++failing;
        }
        if (!result.ok && result.measured == 0.0) {
            ++withoutANumber;
        }
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setSectionResizeMode(kColumnValue, QHeaderView::Stretch);

    if (results_.empty()) {
        summary_->setText(tr("Sin herramientas dibujadas: no hay nada que medir todavía."));
        return;
    }
    // El resumen cuenta las que NO dan número aparte de las que no cumplen. Son
    // dos cosas distintas y llevan a hacer cosas distintas: una cota fuera de
    // banda es un problema de la pieza; una que no mide es un problema del
    // trazo, del encuadre o de la referencia que le falta.
    QString text = tr("%1 cumplen, %2 no.").arg(ok).arg(failing);
    if (withoutANumber > 0) {
        text += tr(" %1 no llegan a medir: mira el motivo en su fila.").arg(withoutANumber);
    }
    summary_->setText(text);
}

}  // namespace pci::ui
