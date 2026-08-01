#include "camera/camera_controls.h"

#include <opencv2/videoio.hpp>

#include <algorithm>
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

PropertyRange rangeFor(CameraProperty property, double min, double max) {
    PropertyRange range;
    if (isToggle(property)) {
        return {0.0, 1.0, 1.0};
    }
    range.min = std::min(min, max);
    range.max = std::max(min, max);
    if (!(range.max > range.min)) {
        // Rango degenerado (la camara no dijo nada util): deja al menos un
        // recorrido usable alrededor del valor.
        range.max = range.min + 1.0;
    }
    const double span = range.max - range.min;
    // Escala normalizada (0..1 de MSMF) o recorridos muy cortos necesitan paso
    // decimal; los recorridos en unidades (0..255, -11..-3) van de uno en uno.
    range.step = (span <= 4.0) ? span / 100.0 : 1.0;
    if (range.step <= 0.0) {
        range.step = 1.0;
    }
    return range;
}

namespace {

// Lee una propiedad sin dejar que una excepcion de OpenCV rompa el sondeo.
double readProperty(cv::VideoCapture& capture, int id) {
    try {
        return capture.get(id);
    } catch (const cv::Exception&) {
        return -1.0;
    }
}

bool writeProperty(cv::VideoCapture& capture, int id, double value) {
    try {
        return capture.set(id, value);
    } catch (const cv::Exception&) {
        return false;
    }
}

}  // namespace

void coalesceControls(std::vector<CameraControlValue>& pending,
                      const std::vector<CameraControlValue>& incoming) {
    for (const auto& control : incoming) {
        auto existing = std::find_if(pending.begin(), pending.end(),
                                     [&control](const CameraControlValue& queued) {
                                         return queued.property == control.property;
                                     });
        if (existing != pending.end()) {
            existing->value = control.value;
        } else {
            pending.push_back(control);
        }
    }
}

std::vector<CameraControlState> probeControls(cv::VideoCapture& capture) {
    std::vector<CameraControlState> states;
    for (const CameraProperty property : allCameraProperties()) {
        const int id = captureProperty(property);
        CameraControlState state;
        state.property = property;
        state.value = readProperty(capture, id);

        // Empujar a los extremos revela el rango real; la camara recorta al
        // maximo y al minimo que admite. Despues se restaura el valor original.
        const bool wroteHigh = writeProperty(capture, id, 1.0e5);
        const double high = readProperty(capture, id);
        const bool wroteLow = writeProperty(capture, id, -1.0e5);
        const double low = readProperty(capture, id);
        const bool restored = writeProperty(capture, id, state.value);

        // Soportado = la camara acepta ESCRIBIR. Un get() valido no basta: hay
        // camaras que informan el brillo pero rechazan cambiarlo.
        state.supported = (wroteHigh || wroteLow || restored) && high != low;
        if (isToggle(property)) {
            state.supported = wroteHigh || wroteLow || restored;
            state.min = 0.0;
            state.max = 1.0;
            if (state.value < 0.0) {
                state.value = 0.0;  // -1 = la camara no informa; se asume apagado
            }
        } else {
            state.min = std::min(low, high);
            state.max = std::max(low, high);
            if (state.value < state.min || state.value > state.max) {
                state.value = std::clamp(state.value, state.min, state.max);
            }
        }
        states.push_back(state);
    }
    return states;
}

}  // namespace pci::camera
