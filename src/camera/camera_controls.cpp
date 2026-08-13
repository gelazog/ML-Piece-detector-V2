#include "camera/camera_controls.h"

#include <opencv2/videoio.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

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

std::vector<CameraResolution> candidateResolutions() {
    return {{320, 240},   {640, 480},   {800, 600},   {1024, 768}, {1280, 720},
            {1280, 960},  {1600, 1200}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
}

CameraResolution currentResolution(cv::VideoCapture& capture) {
    CameraResolution resolution;
    try {
        resolution.width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
        resolution.height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    } catch (const cv::Exception&) {
        resolution = {};
    }
    return resolution;
}

std::vector<CameraResolution> probeResolutions(cv::VideoCapture& capture) {
    const CameraResolution original = currentResolution(capture);
    std::vector<CameraResolution> found;
    const auto remember = [&found](const CameraResolution& resolution) {
        if (!resolution.valid()) {
            return;
        }
        if (std::find(found.begin(), found.end(), resolution) == found.end()) {
            found.push_back(resolution);
        }
    };
    remember(original);

    for (const auto& candidate : candidateResolutions()) {
        try {
            capture.set(cv::CAP_PROP_FRAME_WIDTH, candidate.width);
            capture.set(cv::CAP_PROP_FRAME_HEIGHT, candidate.height);
        } catch (const cv::Exception&) {
            continue;
        }
        // Lo que importa no es si set() dijo que si, sino lo que la camara
        // acabo dando: los backends ajustan a la resolucion admitida mas
        // cercana sin avisar.
        remember(currentResolution(capture));
    }

    if (original.valid()) {
        try {
            capture.set(cv::CAP_PROP_FRAME_WIDTH, original.width);
            capture.set(cv::CAP_PROP_FRAME_HEIGHT, original.height);
        } catch (const cv::Exception&) {
            // Si no se puede restaurar, el bucle de captura seguira con la
            // ultima que haya quedado; se vera en la barra de estado.
        }
    }

    std::sort(found.begin(), found.end(),
              [](const CameraResolution& a, const CameraResolution& b) {
                  return a.width * a.height < b.width * b.height;
              });
    return found;
}

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

bool isAutomaticOf(CameraProperty automatic, CameraProperty manual) {
    return (automatic == CameraProperty::AutoExposure &&
            manual == CameraProperty::Exposure) ||
           (automatic == CameraProperty::AutoFocus && manual == CameraProperty::Focus);
}

std::vector<CameraControlValue> measurementDefaults(
    const std::vector<CameraControlState>& probed,
    const std::vector<CameraControlValue>& saved) {
    const auto wasSavedByOperator = [&saved](CameraProperty property) {
        return std::any_of(saved.begin(), saved.end(), [property](const auto& value) {
            return value.property == property;
        });
    };
    std::vector<CameraControlValue> defaults;
    for (const auto& state : probed) {
        if (!state.supported || !isToggle(state.property)) {
            continue;
        }
        // La exposicion automatica NO se decide aqui. Apagarla puede salir
        // cara —en automatico la camara gobierna tambien la ganancia, y si la
        // ganancia no es ajustable ese refuerzo se pierde sin repuesto—, y si
        // compensa o no depende de la luz que haya en esa nave. Eso lo mide el
        // barrido de exposicion, con la imagen delante. Aqui solo esta lo que
        // no tiene nada que sopesar: el autofoco, que cambia la magnificacion
        // y por tanto TODAS las cotas a la vez.
        if (state.property == CameraProperty::AutoExposure) {
            continue;
        }
        // NO SE APAGA UN AUTOMATICO QUE NO SE PUEDA SUSTITUIR. Medido en la
        // camara de esta maquina y contundente: escribir solo
        // `auto_exposure = 0` la dejo en 8,0 fps viniendo de 29,7, porque al
        // quitarle el automatico la camara se cae a su valor manual — que era
        // el mas largo del rango. Apagar el automatico sin poder elegir el
        // valor no es neutral: es elegir el peor.
        const bool canReplaceIt =
            std::any_of(probed.begin(), probed.end(), [&state](const auto& manual) {
                return isAutomaticOf(state.property, manual.property) && manual.supported;
            });
        if (!canReplaceIt) {
            continue;
        }
        // Y si el operador lo dejo puesto a proposito, se respeta: el perfil es
        // para una camara sin configurar, no una opinion que se impone cada
        // arranque.
        if (!wasSavedByOperator(state.property)) {
            defaults.push_back({state.property, 0.0});
        }

    }
    return defaults;
}

std::vector<double> exposureCandidates(double min, double max) {
    std::vector<double> candidates;
    if (!(max > min)) {
        return candidates;
    }
    // De la más larga a la más corta. La escala de la exposición depende del
    // backend —en DirectShow va en log2 de segundos, así que un paso entero
    // DUPLICA el tiempo— y por eso se recorre el rango medido en pasos
    // uniformes en vez de en unidades absolutas que no significarían lo mismo
    // en otra cámara.
    constexpr int kSteps = 8;
    for (int i = 0; i <= kSteps; ++i) {
        candidates.push_back(max - (max - min) * i / kSteps);
    }
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

std::optional<double> chooseExposure(const std::vector<ExposureFpsSample>& sweep,
                                     double tolerance) {
    double best = 0.0;
    for (const auto& sample : sweep) {
        best = std::max(best, sample.fps);
    }
    if (best <= 0.0) {
        return std::nullopt;
    }
    const double floorFps = best * (1.0 - std::clamp(tolerance, 0.0, 1.0));

    std::optional<double> chosen;
    for (const auto& sample : sweep) {
        if (sample.fps + 1e-9 < floorFps) {
            continue;
        }
        // La más LARGA de las que aguantan la velocidad: más luz por el mismo
        // precio. Una exposición más corta solo serviría para congelar
        // movimiento, y una pieza sobre una mesa no se mueve.
        if (!chosen.has_value() || sample.exposure > *chosen) {
            chosen = sample.exposure;
        }
    }
    return chosen;
}

ProfileVerdict judgeProfile(double fpsBefore, double contrastBefore, double fpsAfter,
                            double contrastAfter) {
    ProfileVerdict verdict;
    if (fpsBefore <= 0.0 || contrastBefore <= 0.0) {
        return verdict;  // sin referencia no se puede juzgar: se deja lo hecho
    }
    const double speedGain = fpsAfter / fpsBefore;
    const double contrastKept = contrastAfter / contrastBefore;

    // Perder contraste es aceptable si a cambio se gana velocidad de verdad. Un
    // 3 % no es velocidad de verdad; 3,8x sí lo es, y ahí la imagen mas oscura
    // se compensa con luz, que es lo que toca en una estacion de inspeccion.
    constexpr double kRealSpeedGain = 1.25;
    constexpr double kContrastFloor = 0.6;
    if (contrastKept >= kContrastFloor || speedGain >= kRealSpeedGain) {
        return verdict;
    }
    verdict.keep = false;
    verdict.reason =
        "fijar la exposicion dejo la imagen con el " +
        std::to_string(static_cast<int>(contrastKept * 100.0)) +
        "% del contraste y solo subio los fps un " +
        std::to_string(static_cast<int>((speedGain - 1.0) * 100.0)) +
        "%: no compensa. Con mas luz sobre la pieza si compensaria, porque "
        "entonces la exposicion fija no oscurece nada";
    return verdict;
}

namespace {

// Dos medidas que no se distinguen NI en velocidad NI en contraste. Se exige
// igualdad casi exacta a proposito: en una camara viva, dos ventanas de medida
// sobre la misma escena no dan el mismo contraste ni con el mismo ajuste, asi
// que esto solo se cumple cuando la camara esta devolviendo una respuesta
// enlatada pase lo que pase. Con un margen generoso aqui se rechazarian
// camaras buenas, y eso es peor.
bool sameObservation(const SceneObservation& a, const SceneObservation& b) {
    constexpr double kEpsilon = 1e-9;
    return std::abs(a.fps - b.fps) <= kEpsilon && std::abs(a.contrast - b.contrast) <= kEpsilon;
}

// Redondeo a entero para los mensajes: al operador le sirve "8 fps", no
// "8.000000", que es lo que da std::to_string con un double.
std::string round0(double value) {
    return std::to_string(static_cast<int>(std::lround(value)));
}

}  // namespace

ExposureProfileResult runExposureProfile(const ExposureSweepCamera& camera,
                                         double minExposure, double maxExposure) {
    ExposureProfileResult result;

    const std::vector<double> candidates = exposureCandidates(minExposure, maxExposure);
    if (candidates.size() < 2) {
        // Sin recorrido no hay exposicion que elegir, y sin exposicion que
        // elegir NO SE APAGA EL AUTOMATICO: se sale sin escribir una sola
        // propiedad. Medido: apagarlo a secas dejo la camara en 8,0 fps
        // viniendo de 29,7, porque se cae a su manual, que es el mas largo.
        result.reason = "la camara no deja recorrer la exposicion: se queda como estaba";
        return result;
    }

    // Primero, la referencia: que da la camara EN AUTOMATICO sobre esta escena.
    // Sin ella no hay forma de saber si fijar la exposicion mejora o empeora, y
    // ese fue exactamente el error de la primera version — se daba por hecho
    // que mejoraba.
    camera.setAutoExposure(true);
    result.automatic = camera.observe();
    camera.setAutoExposure(false);

    // Todo lo observado, para poder distinguir despues una camara que obedece
    // de una que dice que si y no hace nada.
    std::vector<SceneObservation> seen{result.automatic};
    const auto measure = [&camera, &seen](double exposure) {
        camera.setExposure(exposure);
        seen.push_back(camera.observe());
        return seen.back();
    };

    // La mas corta primero: es la que da la velocidad maxima alcanzable, y sin
    // ese techo no se puede saber cuando parar. Luego se baja desde la mas
    // larga y se para en la primera que lo alcanza — el codo esta por ese lado,
    // asi que se sale en dos o tres medidas en vez de en nueve.
    result.sweep.push_back({candidates.back(), measure(candidates.back()).fps});
    const double ceiling = result.sweep.front().fps;
    for (std::size_t i = 0; i + 1 < candidates.size(); ++i) {
        const double fps = measure(candidates[i]).fps;
        result.sweep.push_back({candidates[i], fps});
        if (fps + 1e-9 >= ceiling * 0.95) {
            break;
        }
    }

    const std::optional<double> chosen = chooseExposure(result.sweep);
    if (!chosen.has_value()) {
        camera.setAutoExposure(true);
        result.outcome = ExposureProfileOutcome::Undecided;
        result.reason = "el barrido de exposicion no permite decidir: se deja en automatico";
        return result;
    }

    // El veredicto: ¿se lo ha ganado? Se compara con la imagen que daba el
    // automatico sobre ESTA escena, no contra un numero de catalogo. Si no
    // compensa se vuelve al automatico y se dice por que — una estacion que
    // mide repetible pero no ve la pieza no mide nada.
    camera.setExposure(*chosen);
    result.fixed = camera.observe();
    seen.push_back(result.fixed);

    // La camara sorda: acepto todas las escrituras y dio exactamente la misma
    // medida para todas, incluida la del automatico. No se le ha cambiado
    // nada, asi que no puede decirse que quedo configurada — y decirlo seria
    // lo peor de todo, porque el operador se fiaria de una repetibilidad que
    // no tiene. Es lo que hace la camara real con CAP_PROP_AUTO_EXPOSURE, que
    // acepta el set() y devuelve -1 pase lo que pase.
    if (std::all_of(seen.begin(), seen.end(), [&seen](const SceneObservation& observation) {
            return sameObservation(observation, seen.front());
        })) {
        camera.setAutoExposure(true);
        result.outcome = ExposureProfileOutcome::Ignored;
        result.reason =
            "la camara acepta los cambios de exposicion y no reacciona a ninguno: dio la "
            "misma velocidad y el mismo contraste en todo el barrido, asi que no puede "
            "darse por configurada";
        return result;
    }

    // Y antes de juzgar el intercambio contraste/velocidad, lo que no admite
    // intercambio: el perfil NO puede dejar la camara mas lenta de lo que
    // estaba. Pasa cuando la medida del techo sale mal —un tropiezo en la
    // primera ventana da un techo de 0 fps, la salida temprana se dispara
    // enseguida y la elegida acaba siendo la mas larga—, y es exactamente el
    // desastre de 3,7x que este codigo existe para evitar. `judgeProfile` no
    // lo ve: solo se pregunta si la perdida de contraste se paga con
    // velocidad, y aqui no hay ganancia que repartir.
    constexpr double kSpeedFloor = 0.95;  // el mismo 5 % de ruido de medida
    if (result.automatic.fps > 0.0 &&
        result.fixed.fps + 1e-9 < result.automatic.fps * kSpeedFloor) {
        camera.setAutoExposure(true);
        result.outcome = ExposureProfileOutcome::Reverted;
        result.reason = "fijar la exposicion dejo la camara en " + round0(result.fixed.fps) +
                        " fps cuando el automatico daba " + round0(result.automatic.fps) +
                        ": cambiar a peor no es un perfil, se vuelve al automatico";
        return result;
    }

    const ProfileVerdict verdict = judgeProfile(result.automatic.fps, result.automatic.contrast,
                                                result.fixed.fps, result.fixed.contrast);
    if (!verdict.keep) {
        camera.setAutoExposure(true);
        result.outcome = ExposureProfileOutcome::Reverted;
        result.reason = verdict.reason;
        return result;
    }

    result.outcome = ExposureProfileOutcome::Applied;
    result.exposure = *chosen;
    return result;
}

std::string automaticsWarning(bool calibrated, bool autoExposureOn, bool autoFocusOn) {
    if (!calibrated || (!autoExposureOn && !autoFocusOn)) {
        return {};
    }
    // Se nombra cuál está encendido porque la consecuencia no es la misma y lo
    // que hay que hacer tampoco. El autofoco va primero: es el que puede
    // estropear todas las cotas de golpe.
    if (autoFocusOn && autoExposureOn) {
        return "el enfoque y la exposicion automaticos siguen encendidos: la escala se "
               "fijo con un enfoque concreto, asi que un reenfoque cambia TODAS las cotas "
               "a la vez";
    }
    if (autoFocusOn) {
        return "el enfoque automatico sigue encendido: la escala se fijo con un enfoque "
               "concreto, asi que un reenfoque cambia TODAS las cotas a la vez";
    }
    return "la exposicion automatica sigue encendida: mueve el umbral aparente del borde, "
           "asi que la misma pieza mide distinto segun la luz";
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
