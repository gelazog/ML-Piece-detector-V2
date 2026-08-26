#include "ui/measurement_mode_dialog.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QDialogButtonBox>

#include "ui/dialog_buttons.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

#include "domain/measurement_mode.h"

namespace pci::ui {

MeasurementModeDialog::MeasurementModeDialog(const repositories::PieceMeasurement& current,
                                             const QString& pieceName, QWidget* parent)
    : QDialog(parent), fixedPoint_(current.board.fixedPoint) {
    setWindowTitle(pieceName.isEmpty() ? tr("Modo de medición")
                                       : tr("Modo de medición — %1").arg(pieceName));

    auto* root = new QVBoxLayout(this);

    auto* modeBox = new QGroupBox(tr("¿Cómo se mide esta pieza?"), this);
    auto* modeLayout = new QVBoxLayout(modeBox);
    const auto addMode = [this, modeBox, modeLayout](domain::MeasurementMode mode) {
        auto* radio = new QRadioButton(QString::fromUtf8(domain::modeLabel(mode)), modeBox);
        radio->setToolTip(QString::fromUtf8(domain::modeDescription(mode)));
        modeLayout->addWidget(radio);
        auto* help = new QLabel(QString::fromUtf8(domain::modeDescription(mode)), modeBox);
        help->setWordWrap(true);
        help->setStyleSheet(
            theme::textStyle(theme::kInkOff, QStringLiteral("margin-left:22px;")));
        modeLayout->addWidget(help);
        return radio;
    };
    realRadio_ = addMode(domain::MeasurementMode::Real);
    specialRadio_ = addMode(domain::MeasurementMode::Special);
    (current.mode == domain::MeasurementMode::Special ? specialRadio_ : realRadio_)
        ->setChecked(true);
    root->addWidget(modeBox);

    auto* boardBox = new QGroupBox(tr("Centrado del tablero (modo Especial)"), this);
    auto* boardLayout = new QVBoxLayout(boardBox);

    boardLayout->addWidget(
        new QLabel(tr("<b>Automático</b> — el cero se recalcula en cada frame:"), boardBox));
    originBounds_ = new QRadioButton(tr("Centro de la pieza (contorno) — recomendado"), boardBox);
    originBounds_->setToolTip(
        tr("Centro geométrico del contorno: el punto que se ve centrado en la pieza.\n"
           "Es el centrado automático correcto para poner el cero sobre ella."));
    originPiece_ = new QRadioButton(tr("Centro de masa de la pieza"), boardBox);
    originPiece_->setToolTip(
        tr("Centroide de la máscara. En piezas asimétricas (una L, por ejemplo) queda\n"
           "visiblemente desplazado respecto al centro del contorno."));
    originImage_ = new QRadioButton(tr("Centro de la imagen"), boardBox);
    originImage_->setToolTip(
        tr("El cero queda fijo en pantalla: mide cuánto se desvía la pieza del centro\n"
           "del campo de visión (para centrarla en un soporte)."));
    boardLayout->addWidget(originBounds_);
    boardLayout->addWidget(originPiece_);
    boardLayout->addWidget(originImage_);

    boardLayout->addWidget(new QLabel(tr("<b>Manual</b>:"), boardBox));
    originFixed_ = new QRadioButton(tr("Punto fijado a mano (se marca con un clic)"), boardBox);
    originFixed_->setToolTip(
        tr("Usa el punto que marques con Ver ▸ Origen del tablero ▸\n"
           "Punto fijado a mano… (se conserva el que ya estuviera guardado)."));
    boardLayout->addWidget(originFixed_);

    switch (current.board.origin) {
        case vision::BoardOrigin::PieceBounds:
            originBounds_->setChecked(true);
            break;
        case vision::BoardOrigin::PieceCenter:
            originPiece_->setChecked(true);
            break;
        case vision::BoardOrigin::ImageCenter:
            originImage_->setChecked(true);
            break;
        case vision::BoardOrigin::FixedPoint:
            originFixed_->setChecked(true);
            break;
    }

    // Ajuste fino: se suma a cualquiera de los centrados anteriores, para
    // corregir a mano un cero que no cae exactamente donde el operador quiere.
    auto* offsetLayout = new QHBoxLayout();
    offsetLayout->addWidget(new QLabel(tr("Ajuste fino:"), boardBox));
    offsetX_ = new QDoubleSpinBox(boardBox);
    offsetY_ = new QDoubleSpinBox(boardBox);
    for (auto* spin : {offsetX_, offsetY_}) {
        spin->setRange(-5000.0, 5000.0);
        spin->setDecimals(1);
        spin->setSingleStep(1.0);
        spin->setSuffix(tr(" px"));
    }
    offsetX_->setValue(current.board.manualOffset.x);
    offsetY_->setValue(current.board.manualOffset.y);
    offsetX_->setToolTip(tr("Mueve el cero a la derecha (+) o a la izquierda (−)."));
    offsetY_->setToolTip(tr("Mueve el cero hacia arriba (+) o hacia abajo (−)."));
    offsetLayout->addWidget(new QLabel(tr("X"), boardBox));
    offsetLayout->addWidget(offsetX_);
    offsetLayout->addWidget(new QLabel(tr("Y"), boardBox));
    offsetLayout->addWidget(offsetY_);
    offsetLayout->addStretch(1);
    boardLayout->addLayout(offsetLayout);

    followAngle_ = new QCheckBox(tr("Los ejes giran con la pieza"), boardBox);
    followAngle_->setToolTip(
        tr("Activado: se mide en el marco de la pieza.\n"
           "Desactivado: los ejes quedan alineados con la imagen (marco de la máquina)."));
    followAngle_->setChecked(current.board.followPieceAngle);
    boardLayout->addWidget(followAngle_);
    root->addWidget(boardBox);

    // Reglas que entran en el veredicto OK/NG (M4). 0 = no vigilar, así que
    // quien no las toque sigue inspeccionando exactamente como antes.
    rulesBox_ = new QGroupBox(tr("Reglas de posición (entran en el OK/NG)"), this);
    auto* rulesLayout = new QFormLayout(rulesBox_);
    maxOffset_ = new QDoubleSpinBox(rulesBox_);
    maxOffset_->setRange(0.0, 5000.0);
    maxOffset_->setDecimals(1);
    maxOffset_->setSuffix(tr(" px"));
    maxOffset_->setSpecialValueText(tr("no vigilar"));
    maxOffset_->setValue(current.maxOffsetPx);
    maxOffset_->setToolTip(
        tr("Distancia máxima entre el centro de la pieza y el cero del tablero.\n"
           "Si se supera, la inspección da NG por pieza descentrada."));
    rulesLayout->addRow(tr("Desviación máxima:"), maxOffset_);

    maxAngle_ = new QDoubleSpinBox(rulesBox_);
    maxAngle_->setRange(0.0, 180.0);
    maxAngle_->setDecimals(1);
    maxAngle_->setSuffix(tr(" °"));
    maxAngle_->setSpecialValueText(tr("no vigilar"));
    maxAngle_->setValue(current.maxAngleDeg);
    maxAngle_->setToolTip(
        tr("Giro máximo de la pieza respecto a los ejes del tablero.\n"
           "En piezas casi simétricas el eje no es fiable y esta regla se salta\n"
           "sola, avisando en el detalle, en vez de dar NG falsos."));
    rulesLayout->addRow(tr("Giro máximo:"), maxAngle_);
    root->addWidget(rulesBox_);

    // Aviso de la combinación que no mide nada (hallazgo de la revisión de
    // diseño): Especial + cero en la propia pieza deja la desviación en 0 por
    // definición, así que el modo no aportaría reglas de posición.
    warning_ = new QLabel(this);
    warning_->setWordWrap(true);
    warning_->setStyleSheet(QStringLiteral("color:#ffb454;"));
    warning_->setText(
        tr("⚠ Con el cero puesto sobre la propia pieza, su desviación es cero por "
           "definición: el modo Especial solo aportará el giro. Para vigilar el "
           "centrado, usa el centro de la imagen o un punto fijado a mano."));
    root->addWidget(warning_);

    connect(realRadio_, &QRadioButton::toggled, this, [this](bool) { syncBoardEnabled(); });
    for (auto* radio : {originBounds_, originPiece_, originImage_, originFixed_}) {
        connect(radio, &QRadioButton::toggled, this, [this](bool) { syncBoardEnabled(); });
    }
    syncBoardEnabled();

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    nameButtonsInSpanish(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void MeasurementModeDialog::syncBoardEnabled() {
    // En modo Real el tablero no interviene: se deja visible pero apagado para
    // que se entienda que pertenece al otro modo.
    const bool special = specialRadio_->isChecked();
    for (QWidget* widget : {static_cast<QWidget*>(originBounds_),
                            static_cast<QWidget*>(originPiece_),
                            static_cast<QWidget*>(originImage_),
                            static_cast<QWidget*>(originFixed_),
                            static_cast<QWidget*>(offsetX_),
                            static_cast<QWidget*>(offsetY_),
                            static_cast<QWidget*>(followAngle_)}) {
        widget->setEnabled(special);
    }
    warning_->setVisible(special &&
                         (originBounds_->isChecked() || originPiece_->isChecked()));
    if (rulesBox_ != nullptr) {
        rulesBox_->setEnabled(special);  // las reglas son del modo Especial
    }
}

repositories::PieceMeasurement MeasurementModeDialog::measurement() const {
    repositories::PieceMeasurement result;
    result.mode = specialRadio_->isChecked() ? domain::MeasurementMode::Special
                                             : domain::MeasurementMode::Real;
    if (originImage_->isChecked()) {
        result.board.origin = vision::BoardOrigin::ImageCenter;
    } else if (originFixed_->isChecked()) {
        result.board.origin = vision::BoardOrigin::FixedPoint;
    } else if (originPiece_->isChecked()) {
        result.board.origin = vision::BoardOrigin::PieceCenter;
    } else {
        result.board.origin = vision::BoardOrigin::PieceBounds;
    }
    result.board.fixedPoint = fixedPoint_;
    result.maxOffsetPx = maxOffset_->value();
    result.maxAngleDeg = maxAngle_->value();
    result.board.manualOffset = {static_cast<float>(offsetX_->value()),
                                 static_cast<float>(offsetY_->value())};
    result.board.followPieceAngle = followAngle_->isChecked();
    return result;
}

}  // namespace pci::ui
