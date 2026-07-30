#pragma once

#include <string_view>
#include <vector>

namespace pci::camera {

// Controles de la FUENTE (la cámara), a diferencia de los ajustes de
// `Detección…`, que son post-proceso sobre el frame ya capturado. Solo se
// exponen los que un operador de línea necesita tocar.
enum class CameraProperty {
    Brightness,
    Contrast,
    Gain,
    Exposure,
    AutoExposure,
    Focus,
    AutoFocus,
};

// Todos los controles, en el orden en que se muestran.
[[nodiscard]] std::vector<CameraProperty> allCameraProperties();

// Identificador de `cv::CAP_PROP_*` correspondiente.
[[nodiscard]] int captureProperty(CameraProperty property);

// Clave estable para persistir en Settings (no cambiar: hay valores guardados).
[[nodiscard]] std::string_view propertyKey(CameraProperty property);

// Nombre visible (UTF-8, español) y explicación para el operador.
[[nodiscard]] const char* propertyLabel(CameraProperty property);
[[nodiscard]] const char* propertyHelp(CameraProperty property);

// Los controles de encendido/apagado se pintan como casilla, no como deslizador.
[[nodiscard]] bool isToggle(CameraProperty property);

// Rango sugerido para el control. OpenCV NO expone mínimo ni máximo, y cada
// backend usa su propia escala (brillo 0..255 en DirectShow pero 0..1 en MSMF,
// exposición en log2 segundos y negativa...). Se deduce del valor que la cámara
// devuelve al abrirse: es lo único fiable sin tocar la cámara.
struct PropertyRange {
    double min = 0.0;
    double max = 255.0;
    double step = 1.0;
};
[[nodiscard]] PropertyRange suggestedRange(CameraProperty property, double currentValue);

// Estado de un control tal y como lo reportó la cámara al abrirla.
struct CameraControlState {
    CameraProperty property = CameraProperty::Brightness;
    bool supported = false;  // false = la cámara no expone la propiedad
    double value = 0.0;
};

// Valor que el operador quiere aplicar (se envía al hilo de captura).
struct CameraControlValue {
    CameraProperty property = CameraProperty::Brightness;
    double value = 0.0;
};

}  // namespace pci::camera
