#include "ui/detection_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pci::ui {

DetectionDialog::DetectionDialog(vision::SegmentationOptions current, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Ajustes de detección del contorno"));
    resize(460, 360);

    auto* rootLayout = new QVBoxLayout(this);
    auto* help = new QLabel(
        tr("Si las luces o sombras arruinan el contorno automático: fija el umbral a "
           "mano, dile si la pieza es oscura o clara, y sube el suavizado. Combínalo "
           "con la \"Zona de detección\" para ignorar todo lo que quede fuera."),
        this);
    help->setWordWrap(true);
    rootLayout->addWidget(help);

    auto* form = new QFormLayout();

    autoThreshold_ = new QCheckBox(tr("Automático (Otsu)"), this);
    autoThreshold_->setChecked(current.manualThreshold < 0);
    form->addRow(tr("Umbral:"), autoThreshold_);

    threshold_ = new QSlider(Qt::Horizontal, this);
    threshold_->setRange(0, 255);
    threshold_->setValue(current.manualThreshold >= 0 ? current.manualThreshold : 128);
    threshold_->setEnabled(current.manualThreshold >= 0);
    thresholdValue_ = new QLabel(QString::number(threshold_->value()), this);
    auto* sliderRow = new QHBoxLayout();
    sliderRow->addWidget(threshold_, 1);
    sliderRow->addWidget(thresholdValue_);
    form->addRow(tr("Umbral manual:"), sliderRow);

    polarity_ = new QComboBox(this);
    polarity_->addItem(tr("Automática (el fondo domina el borde)"));
    polarity_->addItem(tr("Pieza oscura sobre fondo claro"));
    polarity_->addItem(tr("Pieza clara sobre fondo oscuro"));
    polarity_->setCurrentIndex(static_cast<int>(current.polarity));
    form->addRow(tr("Polaridad:"), polarity_);

    blur_ = new QSpinBox(this);
    blur_->setRange(1, 31);
    blur_->setSingleStep(2);
    blur_->setValue(current.blurKernel);
    blur_->setToolTip(tr("Suavizado previo: más alto = menos ruido, bordes menos finos"));
    form->addRow(tr("Suavizado (px):"), blur_);

    morph_ = new QSpinBox(this);
    morph_->setRange(1, 31);
    morph_->setSingleStep(2);
    morph_->setValue(current.morphKernel);
    morph_->setToolTip(
        tr("Limpieza morfológica: elimina motas y rellena huecos de ese tamaño"));
    form->addRow(tr("Limpieza (px):"), morph_);

    rootLayout->addLayout(form);
    rootLayout->addStretch(1);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    rootLayout->addWidget(buttons);

    connect(autoThreshold_, &QCheckBox::toggled, this,
            &DetectionDialog::onAutoThresholdToggled);
    connect(threshold_, &QSlider::valueChanged, this, &DetectionDialog::onThresholdMoved);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void DetectionDialog::onAutoThresholdToggled(bool automatic) {
    threshold_->setEnabled(!automatic);
}

void DetectionDialog::onThresholdMoved(int value) {
    thresholdValue_->setText(QString::number(value));
}

vision::SegmentationOptions DetectionDialog::options() const {
    vision::SegmentationOptions result;
    result.manualThreshold = autoThreshold_->isChecked() ? -1 : threshold_->value();
    result.polarity = static_cast<vision::SegmentationPolarity>(polarity_->currentIndex());
    result.blurKernel = blur_->value();
    result.morphKernel = morph_->value();
    return result;
}

}  // namespace pci::ui
