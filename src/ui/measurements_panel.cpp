#include "ui/measurements_panel.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "ui/theme.h"

namespace pci::ui {
namespace {

constexpr int kColumnPiece = 0;
constexpr int kColumnName = 1;
constexpr int kColumnValue = 2;
constexpr int kColumnBand = 3;
constexpr int kColumnVerdict = 4;

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

}  // namespace

MeasurementsPanel::MeasurementsPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    table_ = new QTableWidget(0, 5, this);
    table_->setObjectName(QStringLiteral("measurementsTable"));
    table_->setHorizontalHeaderLabels(
        {tr("Pieza"), tr("Cota"), tr("Valor"), tr("Banda"), tr("")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(kColumnValue, QHeaderView::Stretch);
    root->addWidget(table_, 1);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("measurementsSummary"));
    summary_->setWordWrap(true);
    root->addWidget(summary_);
}

int MeasurementsPanel::rowCount() const { return table_->rowCount(); }

void MeasurementsPanel::setResults(const std::vector<inspection::ToolRunResult>& results,
                                   const std::vector<inspection::ToolConfig>& configs,
                                   double mmPerPixel, inspection::LengthUnit unit) {
    table_->setRowCount(static_cast<int>(results.size()));

    int ok = 0;
    int failing = 0;
    int withoutANumber = 0;
    for (int row = 0; row < static_cast<int>(results.size()); ++row) {
        const inspection::ToolRunResult& result = results[static_cast<std::size_t>(row)];
        const auto config = std::find_if(
            configs.begin(), configs.end(),
            [&result](const inspection::ToolConfig& c) { return c.id == result.toolId; });

        // La pieza de la que es esta medida, numerada como en el mosaico y en el
        // vídeo: empezando por 1. Con seis piezas, una tabla que no diga de cuál
        // es cada fila no se puede interpretar.
        const auto piece = new QTableWidgetItem(QString::number(result.pieceIndex + 1));
        piece->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, kColumnPiece, piece);
        table_->setItem(row, kColumnName,
                        new QTableWidgetItem(QString::fromStdString(result.name)));

        // EL VALOR, O EL MOTIVO POR EL QUE NO LO HAY.
        //
        // Ésta es la otra mitad de «varias herramientas no muestran medidas»:
        // ninguna se calla —todas explican— pero esa explicación no se leía en
        // ningún sitio mientras se trabaja. Aquí ocupa la celda del valor,
        // porque es lo que responde a la pregunta que se estaba haciendo.
        auto* value = new QTableWidgetItem(
            result.ok || result.measured != 0.0
                ? QString::fromStdString(
                      inspection::formatMeasure(result, mmPerPixel, unit, true))
                : QString::fromStdString(result.detail));
        value->setToolTip(QString::fromStdString(result.detail));
        table_->setItem(row, kColumnValue, value);

        table_->setItem(row, kColumnBand,
                        new QTableWidgetItem(config != configs.end()
                                                 ? bandText(*config, mmPerPixel, unit,
                                                            result.kind)
                                                 : QStringLiteral("—")));

        // El veredicto EN TEXTO y no sólo en color, que es la regla que ya
        // gobierna las etiquetas del vídeo y la tabla del informe: un daltónico
        // no distingue este verde de este rojo, y en un parte impreso en blanco
        // y negro el color desaparece entero.
        //
        // Las construcciones geométricas no juzgan nada, así que llevan «—»: un
        // OK verde sobre algo que no puede estar fuera de tolerancia enseña a no
        // fiarse de los OK.
        auto* verdict = new QTableWidgetItem(result.informative ? QStringLiteral("—")
                                             : result.ok        ? QStringLiteral("OK")
                                                                : QStringLiteral("NG"));
        verdict->setTextAlignment(Qt::AlignCenter);
        if (!result.informative) {
            verdict->setForeground(QColor(result.ok ? theme::kGood : theme::kBad));
        }
        table_->setItem(row, kColumnVerdict, verdict);

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

    if (results.empty()) {
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
