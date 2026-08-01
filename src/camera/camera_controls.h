#pragma once

#include <QMetaType>

#include <opencv2/videoio.hpp>

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

// Rango de un control. OpenCV NO expone mínimo ni máximo y cada backend usa su
// propia escala (0..255 en DirectShow, 0..1 en MSMF, exposición en log2
// segundos y negativa...), así que el rango se MIDE empujando la propiedad a
// los extremos (ver probeControls) en vez de adivinarlo.
struct PropertyRange {
    double min = 0.0;
    double max = 255.0;
    double step = 1.0;
};
// Paso de ajuste razonable para un rango medido (decimal si el recorrido es
// pequeño, entero si va en unidades).
[[nodiscard]] PropertyRange rangeFor(CameraProperty property, double min, double max);

// Estado de un control tal y como lo reportó la cámara al abrirla, con su
// rango REAL medido (ver probeControls).
struct CameraControlState {
    CameraProperty property = CameraProperty::Brightness;
    bool supported = false;  // false = la cámara no deja CAMBIAR la propiedad
    double value = 0.0;
    double min = 0.0;
    double max = 0.0;
};

// Sondea de VERDAD qué controles acepta la cámara y con qué rango: lee el valor
// actual, intenta empujarlo a los extremos, anota dónde se queda y lo restaura.
// Es la única forma fiable — se comprobó con una cámara real que `get()` puede
// devolver un valor perfectamente válido (brillo 91) mientras `set()` lo
// rechaza y no cambia nada, así que mirar solo `get()` presentaba deslizadores
// muertos. Debe llamarse desde el hilo dueño de la captura.
[[nodiscard]] std::vector<CameraControlState> probeControls(cv::VideoCapture& capture);

// Valor que el operador quiere aplicar (se envía al hilo de captura).
struct CameraControlValue {
    CameraProperty property = CameraProperty::Brightness;
    double value = 0.0;
};

// Resolución de captura ofrecida por la cámara.
struct CameraResolution {
    int width = 0;
    int height = 0;

    [[nodiscard]] bool valid() const { return width > 0 && height > 0; }
    [[nodiscard]] bool operator==(const CameraResolution& other) const {
        return width == other.width && height == other.height;
    }
};

// Resoluciones candidatas que se prueban al sondear (de menor a mayor).
[[nodiscard]] std::vector<CameraResolution> candidateResolutions();

// Sondea qué resoluciones acepta REALMENTE la cámara: OpenCV no las lista, así
// que se pide cada candidata y se lee la que la cámara acabó dando (los
// backends ajustan a la más cercana admitida). Se devuelven las distintas que
// se consiguieron, sin repetir, y se restaura la que estaba al empezar.
// Cuesta un instante por candidata, así que se hace SOLO cuando el operador
// abre los controles, no en cada arranque. Debe llamarse desde el hilo dueño
// de la captura.
[[nodiscard]] std::vector<CameraResolution> probeResolutions(cv::VideoCapture& capture);

// Resolución con la que está trabajando la cámara ahora mismo.
[[nodiscard]] CameraResolution currentResolution(cv::VideoCapture& capture);

// Mezcla peticiones nuevas en la cola pendiente dejando SOLO el último valor de
// cada propiedad. Arrastrar un deslizador genera decenas de valores por segundo
// y cada capture.set() cuesta milisegundos en el hilo de captura: aplicarlos
// todos atascaba el vídeo, y de los intermedios no queda nada visible.
void coalesceControls(std::vector<CameraControlValue>& pending,
                      const std::vector<CameraControlValue>& incoming);

}  // namespace pci::camera

// Declarados junto al tipo (no en el controlador): cualquier archivo que use
// CameraResolution en un QVariant o en una señal encolada ve la declaración.
Q_DECLARE_METATYPE(pci::camera::CameraResolution)
Q_DECLARE_METATYPE(std::vector<pci::camera::CameraResolution>)
