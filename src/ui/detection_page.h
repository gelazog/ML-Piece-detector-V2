#pragma once

#include <QWidget>

#include <cstdint>

#include "repositories/detection_profile_repository.h"
#include "vision/pipeline.h"
#include "vision/segmentation.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QDoubleSpinBox;
class QSpinBox;

namespace pci::ui {

// Pagina «Deteccion» del panel Configurar: umbral (Otsu o manual), polaridad
// de la pieza, suavizado y limpieza morfologica — para pelear contra luces y
// sombras dificiles. Es un formulario: no aplica nada por su cuenta, la ventana
// le pide los valores cuando el operador pulsa Aplicar.
class DetectionPage : public QWidget {
    Q_OBJECT

public:
    // profiles puede ser nulo (sin base de datos): el diálogo funciona igual,
    // solo sin la parte de perfiles con nombre (O3).
    DetectionPage(vision::SegmentationOptions current, QWidget* parent = nullptr,
                    repositories::DetectionProfileRepository* profiles = nullptr,
                    std::int64_t selectedProfileId = 0,
                    double minAreaFraction = 0.005, double maxAreaFraction = 0.9);

    [[nodiscard]] vision::SegmentationOptions options() const;
    // Qué se acepta como pieza, en fracción del área de la imagen. Estaban
    // fijas en el código y decidían la frontera entre "no hay pieza" y "hay
    // pieza": con piezas pequeñas, el 0,5 % por defecto es justo esa frontera y
    // no se podía mover sin recompilar.
    [[nodiscard]] double minAreaFraction() const;
    [[nodiscard]] double maxAreaFraction() const;
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
    QDoubleSpinBox* minArea_ = nullptr;
    QDoubleSpinBox* maxArea_ = nullptr;
};

}  // namespace pci::ui
