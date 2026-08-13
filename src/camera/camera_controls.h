#pragma once

#include <QMetaType>

#include <opencv2/videoio.hpp>

#include <functional>
#include <optional>
#include <string>
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

// Los valores con los que se abre una cámara que el operador no ha configurado
// nunca. No son «los buenos»: son los que hacen que dos medidas de la misma
// pieza den lo mismo, que es otra cosa y es la que importa en una estación de
// inspección.
//
// Esto apaga los AUTOMÁTICOS y nada más. El valor de exposición no sale de
// aquí: sale de `chooseExposure`, y la razón está medida.
//
// El primer intento fue «congelar cada control manual donde la cámara ya lo
// tiene», que suena inmejorable —repetibilidad sin cambiar la imagen— y sobre
// la cámara real hizo la captura **3,7× más lenta**: 30,1 fps antes, 8,0 fps
// después. La causa es que con el automático puesto, `get(CAP_PROP_EXPOSURE)`
// devuelve el nominal (−3, el más largo del rango) mientras el sensor está
// usando exposiciones cortas de verdad. **El valor reportado bajo automático
// miente**, igual que miente el del propio interruptor —que devuelve −1 pase
// lo que pase—, así que congelarlo no congela nada: escribe otra cosa.
//
// La moraleja, que vale para toda esta capa: de una cámara no se cree lo que
// dice, se mide lo que hace.
//
// `probed` es lo que dijo `probeControls` y `saved` lo que el operador guardó.
// Un control que el operador tocó alguna vez NO se toca: sus ajustes mandan
// sobre el perfil, siempre.
[[nodiscard]] std::vector<CameraControlValue> measurementDefaults(
    const std::vector<CameraControlState>& probed,
    const std::vector<CameraControlValue>& saved);

// El automático que corresponde a un control manual, si lo tiene. Es lo que
// empareja Exposure con AutoExposure y Focus con AutoFocus.
[[nodiscard]] bool isAutomaticOf(CameraProperty automatic, CameraProperty manual);

// Una exposición probada y los fps que dio.
struct ExposureFpsSample {
    double exposure = 0.0;
    double fps = 0.0;
};

// La exposición que se deja puesta: **la más larga que todavía da la velocidad
// máxima**. O sea, toda la luz que no cuesta fps.
//
// Sale de medir el rango entero en la cámara real. Los fps NO bajan poco a poco
// con la exposición: se mantienen planos —30,2 a 30,3— desde −11 hasta −5, y
// solo se desploman en el extremo largo (−4 da 16,0 y −3 da 8,0), porque el
// tiempo de integración pasa a ser mayor que el periodo del frame. Con esa
// forma, «la más corta» tiraría luz a cambio de nada y «la que trae» puede
// costar 3,7×: el punto bueno es el codo.
//
// `tolerance` es cuánta velocidad se acepta perder respecto a la mejor
// observada (0,05 = un 5 %). No es cero porque dos medidas de fps sobre una
// cámara real nunca salen idénticas, y exigir igualdad exacta elegiría siempre
// la más corta por ruido de medida.
//
// Devuelve `std::nullopt` si el barrido no permite decidir (vacío, o ninguna
// muestra con fps utilizable): entonces no se toca la exposición, que es mejor
// que elegirla a ciegas.
[[nodiscard]] std::optional<double> chooseExposure(
    const std::vector<ExposureFpsSample>& sweep, double tolerance = 0.05);

// Las exposiciones que vale la pena probar dentro de un rango medido, de la más
// larga a la más corta. En ese orden porque el barrido puede pararse en cuanto
// encuentra el codo, y el codo está por el lado largo.
[[nodiscard]] std::vector<double> exposureCandidates(double min, double max);

// Qué hacer con el resultado del perfil, comparando la imagen que había ANTES
// (en automático) con la que hay DESPUÉS de fijar los ajustes.
//
// Existe porque la ganancia obtenida puede no pagar lo que cuesta, y eso solo
// se sabe con las dos medidas delante. Medido en la cámara de esta máquina: al
// apagar el automático los fps subieron de 29,7 a 30,5 —un 3 %, nada— y el
// contraste de la imagen se hundió, porque en automático la cámara gobierna
// también la GANANCIA y aquí `gain` sale no ajustable, así que ese refuerzo se
// pierde y no hay con qué reponerlo.
//
// Cambiar una imagen buena por un 3 % de velocidad es un mal negocio, y hacerlo
// en silencio es peor. La regla: **el perfil se queda solo si se lo gana**.
struct ProfileVerdict {
    bool keep = true;
    std::string reason;  // vacío si se queda sin peros
};

// `contrast` es la desviación típica de la imagen, que es la medida directa de
// «¿se puede separar la pieza del fondo aquí?». `fps` habla por sí solo.
[[nodiscard]] ProfileVerdict judgeProfile(double fpsBefore, double contrastBefore,
                                          double fpsAfter, double contrastAfter);

// Lo que se ve de la escena en una ventana de medida. Velocidad y contraste van
// juntos porque la decisión necesita los dos: una exposición que da muchos fps
// y deja la pieza indistinguible del fondo no sirve de nada.
struct SceneObservation {
    double fps = 0.0;
    double contrast = 0.0;
};

// La COSTURA del barrido: todo lo que la orquestación necesita de una cámara,
// que resulta ser muy poco —fijar la exposición, poner o quitar el automático,
// y mirar—. Con esto `runExposureProfile` no sabe qué es una `cv::VideoCapture`
// y se puede ejercitar entera contra una cámara de mentira, que es la única
// forma de ver correr el camino de ACEPTACIÓN: en la cámara de esta máquina hay
// poca luz y el perfil siempre acaba rechazado.
struct ExposureSweepCamera {
    std::function<void(double)> setExposure;
    std::function<void(bool)> setAutoExposure;
    std::function<SceneObservation()> observe;
};

// Cómo acabó el perfil. Son cuatro y no dos porque «no se aplicó» tiene tres
// motivos que al operador le importan de forma distinta.
enum class ExposureProfileOutcome {
    // No se escribió NADA en la cámara: no había recorrido de exposición que
    // barrer, y sin poder elegir la exposición tampoco se toca el automático.
    Untouched,
    // Se midió, pero el barrido no permite decidir. Vuelta al automático, sin
    // molestar al operador: no hay nada que contarle salvo en el log.
    Undecided,
    // Se probó de verdad y no compensaba: vuelta al automático, con motivo.
    Reverted,
    // La cámara aceptó las escrituras y no reaccionó a ninguna. NO es un
    // perfil aplicado, aunque lo parezca desde fuera.
    Ignored,
    // La exposición fija se quedó puesta porque se lo ganó.
    Applied,
};

// El resultado del perfil: qué quedó puesto, con qué se midió y por qué.
struct ExposureProfileResult {
    ExposureProfileOutcome outcome = ExposureProfileOutcome::Untouched;
    std::optional<double> exposure;  // la que quedó puesta, si quedó alguna
    std::vector<ExposureFpsSample> sweep;
    SceneObservation automatic;  // lo que daba la cámara en automático
    SceneObservation fixed;      // lo que dio con la exposición elegida
    std::string reason;          // por qué no quedó puesta, en castellano
};

// La orquestación entera del perfil de medición, sin tocar OpenCV: mide la
// referencia en automático, barre las candidatas con salida temprana, elige,
// mide el resultado, juzga si se lo ha ganado y deshace si no.
//
// Vive aquí y no en el controlador porque encadenada con una `cv::VideoCapture`
// delante no se puede probar, y es justo la parte donde un error sale caro: los
// dos diseños fallidos que hubo antes de este no fallaron en las piezas
// sueltas, fallaron en el orden en que se llamaban.
//
// `minExposure` y `maxExposure` son el rango MEDIDO por `probeControls`. Si no
// hay recorrido entre ellos la función se va sin escribir nada, que es la
// aplicación literal de la regla: no se apaga un automático que no se pueda
// sustituir.
[[nodiscard]] ExposureProfileResult runExposureProfile(const ExposureSweepCamera& camera,
                                                       double minExposure,
                                                       double maxExposure);

// El aviso de «escala calibrada + automático encendido», o vacío si no hay nada
// que decir.
//
// Es la combinación que produce números **creíbles y falsos**, que es la peor
// clase de error que puede dar este programa. La escala px→mm se fijó con una
// magnificación concreta; si el autofoco reenfoca, la magnificación cambia y
// **todas las cotas cambian a la vez**, proporcionalmente, sin que nada en
// pantalla lo delate. La exposición automática es más sutil pero del mismo
// tipo: mueve el umbral aparente del borde, así que la misma pieza sale más
// gorda o más fina según la luz que entre.
//
// El aviso se da SOLO con las dos condiciones juntas, y eso es una decisión de
// diseño, no una economía. Sin calibrar, el autofoco es una comodidad legítima
// —las medidas van en píxeles y nadie ha prometido milímetros—, así que avisar
// ahí sería ruido; y un aviso que salta siempre es un aviso que se aprende a
// ignorar, con lo que tampoco serviría donde de verdad importa.
[[nodiscard]] std::string automaticsWarning(bool calibrated, bool autoExposureOn,
                                            bool autoFocusOn);

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
