#pragma once

#include <QDialog>

#include <cstdint>

#include "repositories/detection_profile_repository.h"
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
    // profiles puede ser nulo (sin base de datos): el diálogo funciona igual,
    // solo sin la parte de perfiles con nombre (O3).
    DetectionDialog(vision::SegmentationOptions current, QWidget* parent = nullptr,
                    repositories::DetectionProfileRepository* profiles = nullptr,
                    std::int64_t selectedProfileId = 0);

    [[nodiscard]] vision::SegmentationOptions options() const;
    // Perfil elegido al aceptar: 0 = ninguno (ajustes sueltos, como antes).
    [[nodiscard]] std::int64_t selectedProfileId() const;

private slots:
    void onAutoThresholdToggled(bool automatic);
    void onThresholdMoved(int value);
    void onProfileChosen(int index);  // vuelca el perfil en los controles
    void onSaveProfile();             // guarda los controles como perfil
    void onDeleteProfile();

private:
    void reloadProfiles(std::int64_t selectId);
    void applyOptions(const vision::SegmentationOptions& options);

    repositories::DetectionProfileRepository* profiles_ = nullptr;
    QComboBox* profileCombo_ = nullptr;
    QCheckBox* autoThreshold_ = nullptr;
    QSlider* threshold_ = nullptr;
    QLabel* thresholdValue_ = nullptr;
    QComboBox* polarity_ = nullptr;
    QSpinBox* blur_ = nullptr;
    QSpinBox* morph_ = nullptr;
};

}  // namespace pci::ui
