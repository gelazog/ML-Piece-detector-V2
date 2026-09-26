#include "ui/measurements_panel.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

#include "ui/theme.h"

namespace pci::ui {
namespace {

using inspection::LengthUnit;
using inspection::MeasuredKind;
using inspection::ToolConfig;
using inspection::ToolRunResult;

// COLUMNAS DEL MODO «UNA PIEZA» (1 pieza, o una elegida en el desplegable):
// una fila por cota, con su valor y su tolerancia en columnas propias.
constexpr int kColEye = 0;
constexpr int kColCota = 1;
constexpr int kSingleColValue = 2;
constexpr int kSingleColTolerance = 3;
constexpr int kSingleColState = 4;
constexpr int kSingleColumnCount = 6;  // + [x]

// EL NÚMERO Y SU SUFIJO, por separado.
//
// `formatMeasure` ya escribe «15.00mm», «90.0°» o «0.930»: un número pegado a
// su unidad. Para componer «15.00mm ± 0.25» —la unidad UNA vez, no una por
// cada mitad de la banda— hace falta poder quedarse solo con el número de la
// segunda cifra. Sin esto, la tolerancia compacta habría sido la SEXTA copia
// de la regla de unidades escribiendo el número a mano.
struct NumSuffix {
    QString numberText;
    double value = 0.0;
    bool ok = false;
};

NumSuffix splitNumber(const QString& text) {
    static const QRegularExpression re(QStringLiteral("^(-?[0-9]+(?:\\.[0-9]+)?)"));
    const auto match = re.match(text);
    if (!match.hasMatch()) {
        return {};
    }
    NumSuffix result;
    result.numberText = match.captured(1);
    result.value = result.numberText.toDouble();
    result.ok = true;
    return result;
}

QString formatValue(MeasuredKind kind, double value, double mmPerPixel, LengthUnit unit) {
    ToolRunResult fake;
    fake.kind = kind;
    fake.measured = value;
    return QString::fromStdString(inspection::formatMeasure(fake, mmPerPixel, unit, true));
}

// LA TOLERANCIA, COMPACTA (punto D del rediseño).
//
// Antes: «14.75mm … 15.25mm», la unidad escrita dos veces para decir una sola
// banda. Si la banda se puede escribir como centro más/menos una distancia SIN
// perder precisión al redondear —que es el caso normal, una banda declarada
// como «15 ± 0,25»—, se escribe así, con la unidad una vez. Si redondear el
// centro y la mitad NO reconstruye los mismos extremos que se guardaron —lo
// que pasa con bandas que no nacieron de un valor nominal más una tolerancia,
// por ejemplo 14.70…15.13, donde el centro redondeado se pasa de la cuenta al
// reconstruir el extremo alto—, se enseña el rango tal cual, que es lo único
// que no miente.
QString toleranceText(const ToolConfig& config, double mmPerPixel, LengthUnit unit,
                      MeasuredKind kind) {
    const auto write = [&](double value) { return formatValue(kind, value, mmPerPixel, unit); };
    const bool openTop = config.toleranceMax >= 1e8;
    if (config.toleranceMin <= 0.0 && openTop) {
        return QString();  // sin tolerancia: la columna se deja en blanco, no en «—»
    }
    if (openTop) {
        return QStringLiteral("≥ %1").arg(write(config.toleranceMin));
    }
    const double lo = config.toleranceMin;
    const double hi = config.toleranceMax;
    const QString loStr = write(lo);
    const QString hiStr = write(hi);
    const double center = (lo + hi) / 2.0;
    const double half = (hi - lo) / 2.0;
    const QString centerStr = write(center);
    const NumSuffix loNum = splitNumber(loStr);
    const NumSuffix hiNum = splitNumber(hiStr);
    const NumSuffix centerNum = splitNumber(centerStr);
    const NumSuffix halfNum = splitNumber(write(half));
    if (loNum.ok && hiNum.ok && centerNum.ok && halfNum.ok) {
        constexpr double kEps = 1e-6;
        const bool reconstructsLo = std::abs((centerNum.value - halfNum.value) - loNum.value) < kEps;
        const bool reconstructsHi = std::abs((centerNum.value + halfNum.value) - hiNum.value) < kEps;
        if (reconstructsLo && reconstructsHi) {
            return QStringLiteral("%1 ± %2").arg(centerStr, halfNum.numberText);
        }
    }
    return QStringLiteral("%1 – %2").arg(loStr, hiStr);
}

// EL VEREDICTO DE UNA COTA, calculado una sola vez y leído desde tres sitios:
// la celda de estado del modo «una pieza», la celda de cada pieza del modo
// «Todas», y la línea de veredicto de arriba. Antes de esto la cuenta del
// margen —«¿qué es OK a secas?»— vivía escrita a mano en un único sitio; ahora
// hacen falta tres lecturas de la misma cuenta y no se iba a escribir tres
// veces.
struct RowVerdict {
    bool ok = false;
    bool informative = false;
    bool noNumber = false;
    bool hasBand = false;
    QString shortMark;  // «✓ 0.25» / «✕ +0.15» / «✓ Cumple» / «No mide» / «referencia»
    QString tooltip;    // la frase completa, para quien no se fía del símbolo
    QString shortNote;  // «margen 0.25mm» / «se pasa 0.15mm», para la celda de pieza y el veredicto
};

RowVerdict evaluate(const ToolRunResult& result, const ToolConfig* config, double mmPerPixel,
                    LengthUnit unit) {
    RowVerdict v;
    v.informative = result.informative;
    if (result.informative) {
        // Una construcción no mide nada dentro o fuera de tolerancia: solo
        // calcula un elemento para que otra herramienta lo use. Ponerle un
        // veredicto enseñaría a no fiarse de los veredictos.
        v.shortMark = QObject::tr("referencia");
        v.tooltip = QObject::tr("Es una construcción: no mide nada que pueda cumplir o no. "
                                "Solo calcula un elemento para que otra herramienta lo use.");
        v.shortNote = QString::fromStdString(result.detail);
        return v;
    }
    v.ok = result.ok;
    if (!result.ok && result.measured == 0.0) {
        // La otra mitad de «varias herramientas no muestran medidas»: el
        // motivo por el que no hay número, no un hueco.
        v.noNumber = true;
        v.shortMark = QObject::tr("No mide");
        v.tooltip = QString::fromStdString(result.detail);
        v.shortNote = QString::fromStdString(result.detail);
        return v;
    }
    if (config != nullptr && config->toleranceMax < 1e8) {
        const double toLow = result.measured - config->toleranceMin;
        const double toHigh = config->toleranceMax - result.measured;
        const double margin = std::min(toLow, toHigh);
        const QString amount = formatValue(result.kind, std::abs(margin), mmPerPixel, unit);
        const NumSuffix split = splitNumber(amount);
        const QString number = split.ok ? split.numberText : amount;
        v.hasBand = true;
        if (result.ok) {
            v.shortMark = QStringLiteral("✓ %1").arg(number);
            v.tooltip = QObject::tr("Cumple, margen %1").arg(amount);
            v.shortNote = QObject::tr("margen %1").arg(amount);
        } else {
            v.shortMark = QStringLiteral("✕ +%1").arg(number);
            v.tooltip = QObject::tr("No cumple, se pasa %1").arg(amount);
            v.shortNote = QObject::tr("se pasa %1").arg(amount);
        }
        return v;
    }
    v.shortMark = result.ok ? QStringLiteral("✓ %1").arg(QObject::tr("Cumple"))
                            : QStringLiteral("✕ %1").arg(QObject::tr("No cumple"));
    v.tooltip = v.shortMark;
    v.shortNote = result.ok ? QObject::tr("cumple") : QObject::tr("no cumple");
    return v;
}

// La frase corta que nombra el fallo en la línea de veredicto: «no mide» es
// más corto que repetir el detalle entero —que puede ser una frase larga como
// «Se necesitan 2 bordes y se detectaron 0»— y el titular tiene que caber en
// una línea.
QString headlinePhrase(const RowVerdict& v) {
    if (v.noNumber) {
        return QObject::tr("no mide");
    }
    if (v.hasBand) {
        return v.shortNote;
    }
    return QObject::tr("no cumple");
}

// UN BOTÓN DE FILA: el ojo y la papelera.
//
// Planos y sin marco para que la tabla siga leyéndose como una tabla —catorce
// botones con relieve serían catorce llamadas de atención—, pero con 24 px de
// lado, que es el mínimo cómodo con ratón a 60 cm.
QToolButton* rowButton(const QString& objectName, const QString& glyph, const QString& tip,
                      bool checkable) {
    auto* button = new QToolButton();
    button->setObjectName(objectName);
    button->setText(glyph);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setCheckable(checkable);
    button->setFixedSize(24, 24);
    return button;
}

// UNA CELDA QUE SE PUEDE PULSAR, con nombre propio.
//
// «Pulsar una fila sigue seleccionando la herramienta»: con las celdas hechas
// de texto de tabla eso lo daba gratis `itemSelectionChanged`, pero un
// `QTableWidgetItem` no admite `setObjectName` —y todas las celdas lo
// necesitan para que las pruebas las encuentren por nombre, nunca por texto,
// porque con el nuevo orden (informativas al final) el número de fila de una
// cota ya no es fijo—. De ahí este widget: una etiqueta con nombre que avisa
// cuando se pulsa.
class ClickableCell : public QLabel {
public:
    using QLabel::QLabel;
    std::function<void()> onClicked;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        QLabel::mousePressEvent(event);
        if (onClicked) {
            onClicked();
        }
    }
};

ClickableCell* makeCell(QTableWidget* table, int row, int column, const QString& objectName,
                        const QString& text, const QString& tooltip, const char* ink,
                        std::function<void()> onClicked) {
    auto* cell = new ClickableCell(text);
    cell->setObjectName(objectName);
    cell->setToolTip(tooltip);
    cell->setContentsMargins(6, 2, 6, 2);
    cell->setStyleSheet(theme::textStyle(ink));
    cell->onClicked = std::move(onClicked);
    table->setCellWidget(row, column, cell);
    return cell;
}

// UNA FILA, agrupada por herramienta y no por pieza. Es el corazón del punto
// B: con esto, tres piezas por cinco cotas dan CINCO filas, no quince.
struct RowData {
    std::int64_t toolId = -1;
    std::string name;
    MeasuredKind kind = MeasuredKind::Length;
    bool informative = false;
    // De qué pieza es cada resultado de esta cota. Con una sola pieza tiene una
    // entrada; con varias, una por cada una — es lo que llena las columnas
    // «Pieza 1», «Pieza 2»… del modo «Todas».
    std::map<int, const ToolRunResult*> byPiece;
};

}  // namespace

MeasurementsPanel::MeasurementsPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    // EL VEREDICTO, ARRIBA DEL TODO (punto A).
    //
    // Queja del taller: el resumen iba abajo y pequeño —«11 cumplen, 1 no.»— y
    // no decía QUÉ pieza fallaba ni POR QUÉ cota. Ahora es lo primero que se
    // lee, grande y en el color del veredicto, con el texto diciendo lo mismo
    // que el color por si el color no llega —daltonismo, una foto en blanco y
    // negro, una pantalla mal calibrada en el taller—.
    verdict_ = new QLabel(this);
    verdict_->setObjectName(QStringLiteral("measurementsVerdict"));
    verdict_->setWordWrap(true);
    verdict_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont verdictFont = verdict_->font();
    verdictFont.setBold(true);
    verdictFont.setPointSizeF(verdictFont.pointSizeF() * 1.25);
    verdict_->setFont(verdictFont);
    root->addWidget(verdict_);

    // QUÉ PIEZA SE SUPERVISA, debajo del veredicto.
    //
    // Va antes que la tabla porque decide qué significa todo lo de abajo, que
    // es la misma regla que ya sigue la pestaña de piezas: el modo antes que
    // sus ajustes.
    auto* pieceRow = new QHBoxLayout();
    pieceRow->addWidget(new QLabel(tr("Pieza:"), this));
    pieceBox_ = new QComboBox(this);
    pieceBox_->setObjectName(QStringLiteral("piecePicker"));
    pieceBox_->setToolTip(tr("Cuál de las piezas del encuadre se está midiendo."));
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

    table_ = new QTableWidget(0, kSingleColumnCount, this);
    table_->setObjectName(QStringLiteral("measurementsTable"));
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(table_, 1);
}

int MeasurementsPanel::rowCount() const { return table_->rowCount(); }

void MeasurementsPanel::setChosenPiece(int pieceIndex) {
    if (pieceIndex == chosenPiece_) {
        return;
    }
    chosenPiece_ = pieceIndex;
    rebuild();
}

void MeasurementsPanel::setResults(const std::vector<ToolRunResult>& results,
                                   const std::vector<ToolConfig>& configs, double mmPerPixel,
                                   LengthUnit unit) {
    results_ = results;
    configs_ = configs;
    mmPerPixel_ = mmPerPixel;
    unit_ = unit;
    rebuild();
}

void MeasurementsPanel::rebuild() {
    // BORRAR LOS WIDGETS DE CELDA A MANO, antes de nada.
    //
    // `QTableWidget` sustituye el widget de una celda cuando se pone otro
    // encima, pero lo hace con `deleteLater()`: el objeto viejo sigue vivo
    // hasta que el bucle de eventos vuelve a girar. Cambiar de modo «Todas»
    // (columnas de pieza) a modo «una pieza» (columna de valor) en la MISMA
    // llamada a `rebuild` reutiliza esas coordenadas de celda, y una prueba
    // que pregunta en el mismo instante «¿ya no está `pieceCell_1_2`?» lo
    // encontraba igual —vivo, aunque ya no en pantalla— porque el bucle de
    // eventos nunca había girado.
    //
    // Y NO SE HACE CON UN `delete` A PELO, que fue la primera versión: la tabla
    // guarda cada widget de celda en un mapa suyo, y borrarlo por fuera dejaba
    // ahí un puntero colgando. En cuanto algo preguntaba por el tamaño de las
    // columnas —ajustarlas a su contenido lo hace—, el programa se caía con un
    // fallo de segmentación.
    //
    // Lo correcto es sacarlo de la tabla (`removeCellWidget`, que lo quita del
    // mapa y lo borra al volver al bucle) y soltarlo del árbol de widgets en el
    // acto (`setParent(nullptr)`), que es lo que hace que `findChild` ya no lo
    // encuentre en esta misma llamada.
    for (int r = 0; r < table_->rowCount(); ++r) {
        for (int c = 0; c < table_->columnCount(); ++c) {
            if (QWidget* old = table_->cellWidget(r, c); old != nullptr) {
                table_->removeCellWidget(r, c);
                old->setParent(nullptr);
            }
        }
    }

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

    if (results_.empty()) {
        table_->setColumnCount(kSingleColumnCount);
        table_->setRowCount(0);
        verdict_->setStyleSheet(theme::textStyle(theme::kInkMuted));
        verdict_->setText(tr("Sin herramientas dibujadas: no hay nada que medir todavía."));
        return;
    }

    // UNA FILA POR COTA, no por pieza×cota (punto B). Se agrupa por
    // `toolId` en el orden en el que aparece primero, y por eso hace falta un
    // mapa aparte de índice: `results_` trae la pieza 0 entera y luego la 1 y
    // luego la 2, así que agrupar por posición repetiría exactamente el fallo
    // que esto arregla.
    std::vector<RowData> all;
    std::map<std::int64_t, std::size_t> indexOf;
    for (const auto& result : results_) {
        const auto it = indexOf.find(result.toolId);
        if (it == indexOf.end()) {
            RowData row;
            row.toolId = result.toolId;
            row.name = result.name;
            row.kind = result.kind;
            row.informative = result.informative;
            row.byPiece.emplace(result.pieceIndex, &result);
            indexOf.emplace(result.toolId, all.size());
            all.push_back(std::move(row));
        } else {
            all[it->second].byPiece.emplace(result.pieceIndex, &result);
        }
    }
    // LAS INFORMATIVAS AL FINAL (punto F): una construcción no se juzga, y
    // mezclada entre las cotas que sí cumplen o no invita a leerla como si
    // también contara.
    std::vector<const RowData*> ordered;
    for (const auto& row : all) {
        if (!row.informative) {
            ordered.push_back(&row);
        }
    }
    for (const auto& row : all) {
        if (row.informative) {
            ordered.push_back(&row);
        }
    }

    // MODO «UNA PIEZA»: 1 pieza en total, o una elegida en el desplegable.
    // MODO «TODAS»: «Todas» elegida y hay más de una pieza — solo entonces
    // tiene sentido una columna por pieza.
    const bool multiMode = chosenPiece_ < 0 && pieces.size() > 1;
    const int effectivePiece = chosenPiece_ >= 0 ? chosenPiece_ : (pieces.empty() ? 0 : *pieces.begin());

    // En modo «una pieza» solo entran las filas que de verdad tienen un
    // resultado para esa pieza — no debería faltar ninguna, pero una tabla que
    // dibuja un puntero nulo es peor que una fila de menos.
    std::vector<const RowData*> display;
    if (multiMode) {
        display = ordered;
    } else {
        for (const RowData* row : ordered) {
            if (row->byPiece.find(effectivePiece) != row->byPiece.end()) {
                display.push_back(row);
            }
        }
    }

    const QSignalBlocker quiet(table_);
    const int firstPieceColumn = 3;
    const int columnCount =
        multiMode ? firstPieceColumn + static_cast<int>(pieces.size()) + 1 : kSingleColumnCount;
    const int deleteColumn = columnCount - 1;
    table_->setColumnCount(columnCount);
    {
        QStringList headers;
        headers << QString() << tr("Cota");
        if (multiMode) {
            headers << tr("Tolerancia");
            for (const int piece : pieces) {
                headers << tr("Pieza %1").arg(piece + 1);
            }
        } else {
            headers << tr("Valor") << tr("Tolerancia") << tr("Estado");
        }
        headers << QString();
        table_->setHorizontalHeaderLabels(headers);
    }
    table_->setRowCount(static_cast<int>(display.size()));

    for (int row = 0; row < static_cast<int>(display.size()); ++row) {
        const RowData& data = *display[static_cast<std::size_t>(row)];
        const std::int64_t toolId = data.toolId;
        const auto configIt = std::find_if(
            configs_.begin(), configs_.end(),
            [toolId](const ToolConfig& c) { return c.id == toolId; });
        const ToolConfig* config = configIt != configs_.end() ? &*configIt : nullptr;
        const char* ink = data.informative ? theme::kInkOff : theme::kInk;

        // EL OJO: si esta cota se dibuja sobre la pieza (punto C: una vez por
        // cota, ya no una vez por pieza×cota).
        const bool visible = std::find(hidden_.begin(), hidden_.end(), toolId) == hidden_.end();
        auto* eye = rowButton(QStringLiteral("eyeButton_%1").arg(toolId),
                              visible ? QStringLiteral("\U0001F441") : QStringLiteral("—"),
                              tr("Dibuja esta cota sobre la pieza. Apagarla no deja de "
                                 "medirla."),
                              true);
        eye->setChecked(visible);
        connect(eye, &QToolButton::toggled, this, [this, toolId, eye](bool on) {
            eye->setText(on ? QStringLiteral("\U0001F441") : QStringLiteral("—"));
            auto at = std::find(hidden_.begin(), hidden_.end(), toolId);
            if (on && at != hidden_.end()) {
                hidden_.erase(at);
            } else if (!on && at == hidden_.end()) {
                hidden_.push_back(toolId);
            }
            emit overlayVisibilityChanged(toolId, on);
        });
        table_->setCellWidget(row, kColEye, eye);

        // Pulsar la fila (por cualquier celda de texto) selecciona la
        // herramienta sobre la imagen (punto G).
        const auto selectThisTool = [this, row, toolId] {
            table_->selectRow(row);
            emit toolChosen(toolId);
        };

        makeCell(table_, row, kColCota, QStringLiteral("cotaCell_%1").arg(toolId),
                QString::fromStdString(data.name), QString::fromStdString(data.name), ink,
                selectThisTool);

        const QString toleranceStr =
            config != nullptr ? toleranceText(*config, mmPerPixel_, unit_, data.kind) : QString();
        const int toleranceColumn = multiMode ? 2 : kSingleColTolerance;
        makeCell(table_, row, toleranceColumn, QStringLiteral("toleranceCell_%1").arg(toolId),
                toleranceStr, toleranceStr, ink, selectThisTool);

        if (!multiMode) {
            const auto it = data.byPiece.find(effectivePiece);
            const ToolRunResult& result = *it->second;
            const RowVerdict v = evaluate(result, config, mmPerPixel_, unit_);

            // EL VALOR, O EL MOTIVO POR EL QUE NO LO HAY.
            const QString valueStr =
                result.ok || result.measured != 0.0
                    ? QString::fromStdString(
                          inspection::formatMeasure(result, mmPerPixel_, unit_, true))
                    : QString::fromStdString(result.detail);
            makeCell(table_, row, kSingleColValue, QStringLiteral("valueCell_%1").arg(toolId),
                    valueStr, QString::fromStdString(result.detail), ink, selectThisTool);

            // ESTADO COMPACTO (punto E): «✓ 0,25» / «✕ +0,15», con la frase
            // completa en el tooltip para quien no se fía del símbolo.
            const char* stateInk = data.informative ? theme::kInkOff : (v.ok ? theme::kGood : theme::kBad);
            makeCell(table_, row, kSingleColState, QStringLiteral("stateCell_%1").arg(toolId),
                    v.shortMark, v.tooltip, stateInk, selectThisTool);
        } else {
            int column = firstPieceColumn;
            for (const int piece : pieces) {
                const QString objectName =
                    QStringLiteral("pieceCell_%1_%2").arg(toolId).arg(piece);
                const auto it = data.byPiece.find(piece);
                const auto selectThisPiece = [this, row, toolId, piece] {
                    table_->selectRow(row);
                    emit toolChosen(toolId);
                    emit pieceChosen(piece);
                };
                if (it == data.byPiece.end()) {
                    makeCell(table_, row, column, objectName, QString(), QString(), ink,
                            selectThisPiece);
                } else {
                    const ToolRunResult& result = *it->second;
                    const RowVerdict v = evaluate(result, config, mmPerPixel_, unit_);
                    QString text;
                    QString tooltip;
                    const char* cellInk = theme::kInk;
                    if (data.informative) {
                        text = result.ok || result.measured != 0.0
                                  ? QString::fromStdString(inspection::formatMeasure(
                                        result, mmPerPixel_, unit_, true))
                                  : QString::fromStdString(result.detail);
                        tooltip = QString::fromStdString(result.detail);
                        cellInk = theme::kInkOff;
                    } else if (v.noNumber) {
                        text = QStringLiteral("✕ %1").arg(tr("No mide"));
                        tooltip = v.tooltip;
                        cellInk = theme::kBad;
                    } else {
                        const QString mark = v.ok ? QStringLiteral("✓") : QStringLiteral("✕");
                        text = QStringLiteral("%1 %2").arg(
                            mark, QString::fromStdString(
                                      inspection::formatMeasure(result, mmPerPixel_, unit_, true)));
                        tooltip = v.shortNote;
                        cellInk = v.ok ? theme::kGood : theme::kBad;
                    }
                    makeCell(table_, row, column, objectName, text, tooltip, cellInk,
                            selectThisPiece);
                }
                ++column;
            }
        }

        // BORRAR, con la papelera en su propia columna y no en un menú.
        // Quien borra es la ventana, que tiene el deshacer.
        auto* remove = rowButton(QStringLiteral("deleteButton_%1").arg(toolId), QStringLiteral("✕"),
                                 tr("Quita esta cota. Se deshace con Ctrl+Z."),
                                 false);
        connect(remove, &QToolButton::clicked, this,
                [this, toolId] { emit deleteRequested(toolId); });
        table_->setCellWidget(row, deleteColumn, remove);
    }

    // EL ANCHO SOBRANTE SE LO QUEDA LA COLUMNA «COTA», EN LOS DOS MODOS.
    //
    // Antes solo se estiraba la columna de valor en modo «una pieza», y en modo
    // «Todas» la tabla se quedaba estrecha a la izquierda con un hueco en blanco
    // a la derecha del panel. Además, el estiramiento se ponía por NÚMERO de
    // columna y no se quitaba al cambiar de modo: la columna 2, que en «una
    // pieza» es el valor, en «Todas» es «Pieza 1», y se quedaba estirada ella.
    //
    // Todo se ajusta primero a su contenido y después solo «Cota» se estira: es
    // la única cuyo texto puede ser largo, y las de pieza o de valor se leen
    // mejor juntas que repartidas.
    auto* header = table_->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kColCota, QHeaderView::Stretch);

    // EL VEREDICTO (punto A): busca la PRIMERA cota que no cumple, recorriendo
    // `results_` tal cual llega —pieza 0 entera, luego la 1, luego la 2—, así
    // que «la primera» es la primera que de verdad se ve al leer de arriba a
    // abajo la tabla sin filtrar.
    struct Failure {
        int pieceIndex = 0;
        QString cota;
        QString phrase;
    };
    std::vector<Failure> failures;
    for (const auto& result : results_) {
        if (result.informative || result.ok) {
            continue;
        }
        const auto configIt = std::find_if(
            configs_.begin(), configs_.end(),
            [&result](const ToolConfig& c) { return c.id == result.toolId; });
        const RowVerdict v =
            evaluate(result, configIt != configs_.end() ? &*configIt : nullptr, mmPerPixel_, unit_);
        failures.push_back({result.pieceIndex, QString::fromStdString(result.name), headlinePhrase(v)});
    }

    QString verdictText;
    const bool good = failures.empty();
    if (pieces.size() <= 1) {
        verdictText = good ? tr("✓ Cumple")
                          : tr("✕ No cumple: %1 %2")
                                .arg(failures.front().cota, failures.front().phrase);
    } else {
        std::set<int> failingPieces;
        for (const auto& f : failures) {
            failingPieces.insert(f.pieceIndex);
        }
        verdictText =
            good ? tr("✓ %1 piezas: todas cumplen").arg(pieces.size())
                : tr("✕ %1 de %2 no cumple · pieza %3: %4 %5")
                      .arg(failingPieces.size())
                      .arg(pieces.size())
                      .arg(failures.front().pieceIndex + 1)
                      .arg(failures.front().cota, failures.front().phrase);
    }
    if (!good && failures.size() > 1) {
        verdictText += tr(" (+%1 más)").arg(failures.size() - 1);
    }
    verdict_->setStyleSheet(good ? theme::noticeStyle(theme::kGood, theme::kGoodField)
                                : theme::noticeStyle(theme::kBad, theme::kBadField));
    verdict_->setText(verdictText);
}

}  // namespace pci::ui
