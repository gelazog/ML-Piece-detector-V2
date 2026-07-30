#include "camera/camera_controls.h"

#include <opencv2/videoio.hpp>

#include <cmath>

namespace pci::camera {

std::vector<CameraProperty> allCameraProperties() {
    return {CameraProperty::Brightness, CameraProperty::Contrast,   CameraProperty::Gain,
            CameraProperty::Exposure,   CameraProperty::AutoExposure, CameraProperty::Focus,
            CameraProperty::AutoFocus};
}

int captureProperty(CameraProperty property) {
    switch (property) {
        case CameraProperty::Brightness:
            return cv::CAP_PROP_BRIGHTNESS;
        case CameraProperty::Contrast:
            return cv::CAP_PROP_CONTRAST;
        case CameraProperty::Gain:
            return cv::CAP_PROP_GAIN;
        case CameraProperty::Exposure:
            return cv::CAP_PROP_EXPOSURE;
        case CameraProperty::AutoExposure:
            return cv::CAP_PROP_AUTO_EXPOSURE;
        case CameraProperty::Focus:
            return cv::CAP_PROP_FOCUS;
        case CameraProperty::AutoFocus:
            return cv::CAP_PROP_AUTOFOCUS;
    }
    return cv::CAP_PROP_BRIGHTNESS;
}

std::string_view propertyKey(CameraProperty property) {
    switch (property) {
        case CameraProperty::Brightness:
            return "cam_brightness";
        case CameraProperty::Contrast:
            return "cam_contrast";
        case CameraProperty::Gain:
            return "cam_gain";
        case CameraProperty::Exposure:
            return "cam_exposure";
        case CameraProperty::AutoExposure:
            return "cam_auto_exposure";
        case CameraProperty::Focus:
            return "cam_focus";
        case CameraProperty::AutoFocus:
            return "cam_autofocus";
    }
    return "cam_brightness";
}

const char* propertyLabel(CameraProperty property) {
    switch (property) {
        case CameraProperty::Brightness:
            return "Brillo";
        case CameraProperty::Contrast:
            return "Contraste";
        case CameraProperty::Gain:
            return "Ganancia";
        case CameraProperty::Exposure:
            return "Exposición";
        case CameraProperty::AutoExposure:
            return "Exposición automática";
        case CameraProperty::Focus:
            return "Enfoque";
        case CameraProperty::AutoFocus:
            return "Enfoque automático";
    }
    return "";
}

const char* propertyHelp(CameraProperty property) {
    switch (property) {
        case CameraProperty::Brightness:
            return "Brillo de la fuente. Súbelo si la pieza sale demasiado oscura,\n"
                   "antes de tocar el umbral de detección.";
        case CameraProperty::Contrast:
            return "Contraste de la fuente. Ayuda a separar la pieza del fondo.";
        case CameraProperty::Gain:
            return "Amplificación del sensor. Sube el brillo pero también el ruido:\n"
                   "úsala solo si no puedes dar más luz a la escena.";
        case CameraProperty::Exposure:
            return "Tiempo de exposición. Bájalo para congelar piezas en movimiento;\n"
                   "requiere desactivar la exposición automática.";
        case CameraProperty::AutoExposure:
            return "Con la exposición automática activa, la cámara cambia el brillo\n"
                   "sola y la detección puede oscilar. Para una línea estable,\n"
                   "desactívala y fija la exposición.";
        case CameraProperty::Focus:
            return "Enfoque manual. Requiere desactivar el enfoque automático.";
        case CameraProperty::AutoFocus:
            return "El enfoque automático puede 'bombear' entre frames y falsear las\n"
                   "medidas. Con la cámara fija, desactívalo y enfoca una vez.";
    }
    return "";
}

bool isToggle(CameraProperty property) {
    return property == CameraProperty::AutoExposure || property == CameraProperty::AutoFocus;
}

PropertyRange suggestedRange(CameraProperty property, double currentValue) {
    if (isToggle(property)) {
        return {0.0, 1.0, 1.0};
    }
    if (property == CameraProperty::Exposure) {
        // DirectShow expone la exposición como log2(segundos): valores
        // negativos pequeños. MSMF y V4L2 usan microsegundos o pasos enteros.
        if (currentValue <= 0.0 && currentValue >= -20.0) {
            return {-15.0, 5.0, 1.0};
        }
        return {0.0, std::max(1000.0, currentValue * 4.0), 1.0};
    }
    // Escala normalizada (0..1) frente a la escala 0..255 clásica: se decide
    // por el valor actual, que es lo único que la cámara nos dice.
    if (std::abs(currentValue) <= 1.0) {
        return {0.0, 1.0, 0.01};
    }
    return {0.0, std::max(255.0, std::ceil(currentValue)), 1.0};
}

}  // namespace pci::camera
