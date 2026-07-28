#include "ui/measurement_mode_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
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
        help->setStyleSheet(QStringLiteral("color:#999; margin-left:22px;"));
        modeLayout->addWidget(help);
        return radio;
    };
    realRadio_ = addMode(domain::MeasurementMode::Real);
    specialRadio_ = addMode(domain::MeasurementMode::Special);
    (current.mode == domain::MeasurementMode::Special ? specialRadio_ : realRadio_)
        ->setChecked(true);
    root->addWidget(modeBox);

    auto* boardBox = new QGroupBox(tr("Tablero de referencia (modo Especial)"), this);
    auto* boardLayout = new QVBoxLayout(boardBox);
    originPiece_ = new QRadioButton(tr("Cero en el centro de la pieza"), boardBox);
    originPiece_->setToolTip(
        tr("El cero viaja con la pieza: mide desviaciones respecto a su propio centro."));
    originImage_ = new QRadioButton(tr("Cero en el centro de la imagen"), boardBox);
    originImage_->setToolTip(
        tr("El cero queda fijo en pantalla: mide cuánto se desvía la pieza del centro\n"
           "del campo de visión (para centrarla en un soporte)."));
    originFixed_ = new QRadioButton(tr("Cero en un punto fijado a mano"), boardBox);
    originFixed_->setToolTip(
        tr("Usa el punto que hayas marcado con Ver ▸ Origen del tablero ▸\n"
           "Punto fijado a mano… (se conserva el que ya estuviera guardado)."));
    boardLayout->addWidget(originPiece_);
    boardLayout->addWidget(originImage_);
    boardLayout->addWidget(originFixed_);
    switch (current.board.origin) {
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

    followAngle_ = new QCheckBox(tr("Los ejes giran con la pieza"), boardBox);
    followAngle_->setToolTip(
        tr("Activado: se mide en el marco de la pieza.\n"
           "Desactivado: los ejes quedan alineados con la imagen (marco de la máquina)."));
    followAngle_->setChecked(current.board.followPieceAngle);
    boardLayout->addWidget(followAngle_);
    root->addWidget(boardBox);

    // Aviso de la combinación que no mide nada (hallazgo de la revisión de
    // diseño): Especial + cero en la propia pieza deja la desviación en 0 por
    // definición, así que el modo no aportaría reglas de posición.
    warning_ = new QLabel(this);
    warning_->setWordWrap(true);
    warning_->setStyleSheet(QStringLiteral("color:#ffb454;"));
    warning_->setText(
        tr("⚠ Con el cero en el centro de la pieza, su desviación es cero por "
           "definición: el modo Especial solo aportará el giro. Para vigilar el "
           "centrado, usa el centro de la imagen o un punto fijado a mano."));
    root->addWidget(warning_);

    connect(realRadio_, &QRadioButton::toggled, this, [this](bool) { syncBoardEnabled(); });
    for (auto* radio : {originPiece_, originImage_, originFixed_}) {
        connect(radio, &QRadioButton::toggled, this, [this](bool) { syncBoardEnabled(); });
    }
    syncBoardEnabled();

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void MeasurementModeDialog::syncBoardEnabled() {
    // En modo Real el tablero no interviene: se deja visible pero apagado para
    // que se entienda que pertenece al otro modo.
    const bool special = specialRadio_->isChecked();
    for (QWidget* widget : {static_cast<QWidget*>(originPiece_),
                            static_cast<QWidget*>(originImage_),
                            static_cast<QWidget*>(originFixed_),
                            static_cast<QWidget*>(followAngle_)}) {
        widget->setEnabled(special);
    }
    warning_->setVisible(special && originPiece_->isChecked());
}

repositories::PieceMeasurement MeasurementModeDialog::measurement() const {
    repositories::PieceMeasurement result;
    result.mode = specialRadio_->isChecked() ? domain::MeasurementMode::Special
                                             : domain::MeasurementMode::Real;
    if (originImage_->isChecked()) {
        result.board.origin = vision::BoardOrigin::ImageCenter;
    } else if (originFixed_->isChecked()) {
        result.board.origin = vision::BoardOrigin::FixedPoint;
    } else {
        result.board.origin = vision::BoardOrigin::PieceCenter;
    }
    result.board.fixedPoint = fixedPoint_;
    result.board.followPieceAngle = followAngle_->isChecked();
    return result;
}

}  // namespace pci::ui
