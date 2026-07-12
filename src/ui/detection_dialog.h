#pragma once

#include <QDialog>

#include "vision/segmentation.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;

namespace pci::ui {

// Controles de detección del contorno automático: umbral (Otsu o manual),
// polaridad de la pieza, suavizado y limpieza morfológica — para pelear
// contra luces y sombras difíciles. Los cambios aplican al aceptar y se ven
// al instante en el video en vivo.
class DetectionDialog : public QDialog {
    Q_OBJECT

public:
    DetectionDialog(vision::SegmentationOptions current, QWidget* parent = nullptr);

    [[nodiscard]] vision::SegmentationOptions options() const;

private slots:
    void onAutoThresholdToggled(bool automatic);
    void onThresholdMoved(int value);

private:
    QCheckBox* autoThreshold_ = nullptr;
    QSlider* threshold_ = nullptr;
    QLabel* thresholdValue_ = nullptr;
    QComboBox* polarity_ = nullptr;
    QSpinBox* blur_ = nullptr;
    QSpinBox* morph_ = nullptr;
};

}  // namespace pci::ui
