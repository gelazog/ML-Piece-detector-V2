#pragma once

#include <QDialog>

#include <vector>

#include "camera/camera_controls.h"

class QCheckBox;
class QSlider;

namespace pci::camera {
class CameraController;
}

namespace pci::ui {

// Controles de la fuente (O2): brillo, contraste, ganancia, exposición y
// enfoque de la cámara en marcha. Cada cambio se aplica en vivo a través del
// controlador (que lo ejecuta en el hilo de captura) y se puede ver al momento
// sobre el vídeo, así que el diálogo es **no modal**.
//
// Lo que la cámara no soporta aparece deshabilitado en vez de fingir que
// funciona: OpenCV no expone la lista de propiedades y cada backend miente de
// forma distinta, así que solo se confía en lo que la cámara devolvió al abrir.
class CameraControlsDialog : public QDialog {
    Q_OBJECT

public:
    CameraControlsDialog(camera::CameraController& controller,
                         const std::vector<camera::CameraControlState>& probed,
                         QWidget* parent = nullptr);

signals:
    // Valor cambiado por el operador, para que la ventana lo persista.
    void controlChanged(const pci::camera::CameraControlValue& control);

private:
    struct Row {
        camera::CameraProperty property = camera::CameraProperty::Brightness;
        QSlider* slider = nullptr;
        QCheckBox* toggle = nullptr;
        bool supported = false;
    };

    void apply(camera::CameraProperty property, double value);
    // Un control manual no hace nada mientras su automático está activo: se
    // deshabilita en vez de dejar que el operador mueva algo inerte.
    void syncAutoDependencies();
    [[nodiscard]] bool autoActive(camera::CameraProperty autoProperty) const;

    camera::CameraController& controller_;
    std::vector<Row> rows_;
};

}  // namespace pci::ui
