#include "camera/camera_controller.h"

#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "camera/frame_utils.h"
#include "core/crash_guard.h"
#include "core/fps_counter.h"
#include "core/logging.h"

namespace pci::camera {

namespace {

// ~1 segundo sin frames a 30 fps antes de declarar la cámara perdida.
constexpr int kMaxConsecutiveFailures = 30;
constexpr auto kStatsInterval = std::chrono::milliseconds(500);

// Argumentos para abrir la cámara a través del blindaje SEH de core::runProtected.
struct OpenArgs {
    cv::VideoCapture* capture;
    int index;
    int backend;
};

// Trampolín sin objetos con destructor no trivial en su firma. Captura las
// excepciones de C++ AQUÍ (no cruzan la barrera SEH); las excepciones
// estructuradas del SO —la división por cero de un driver roto— sí la cruzan y
// las gestiona core::runProtected.
void openTrampoline(void* ctx) {
    auto* args = static_cast<OpenArgs*>(ctx);
    try {
        args->capture->open(args->index, args->backend);
    } catch (const cv::Exception& e) {
        core::logWarning(std::string("OpenCV lanzó al abrir la cámara: ") + e.what());
    } catch (...) {
        core::logWarning("Excepción C++ desconocida al abrir la cámara");
    }
}

std::string toHex(unsigned long value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", value);
    return buffer;
}

}  // namespace

CameraController::CameraController(QObject* parent) : QObject(parent) {
    qRegisterMetaType<std::vector<CameraControlState>>();
    qRegisterMetaType<std::vector<ExposureFpsSample>>();
    qRegisterMetaType<std::vector<CameraResolution>>();
    qRegisterMetaType<CameraResolution>();
}

CameraController::~CameraController() {
    stop();
}

void CameraController::start(const CameraInfo& camera,
                             const std::vector<CameraControlValue>& initialControls) {
    stop();
    {
        std::lock_guard<std::mutex> lock(controlsMutex_);
        pendingControls_ = initialControls;
    }
    running_ = true;
    worker_ = std::thread(&CameraController::captureLoop, this, camera);
}

void CameraController::requestControls(const std::vector<CameraControlValue>& controls) {
    std::lock_guard<std::mutex> lock(controlsMutex_);
    coalesceControls(pendingControls_, controls);
}

void CameraController::requestResolutionProbe() {
    std::lock_guard<std::mutex> lock(controlsMutex_);
    resolutionProbePending_ = true;
}

void CameraController::requestResolution(const CameraResolution& resolution) {
    if (!resolution.valid()) {
        return;
    }
    std::lock_guard<std::mutex> lock(controlsMutex_);
    // Solo la ultima pedida: cambiar de resolucion reinicia el flujo de la
    // camara, encadenar varias no tiene sentido.
    pendingResolution_ = resolution;
}

void CameraController::requestExposureSweep(double minExposure, double maxExposure) {
    std::lock_guard<std::mutex> lock(controlsMutex_);
    exposureSweepPending_ = true;
    exposureSweepMin_ = minExposure;
    exposureSweepMax_ = maxExposure;
}

namespace {

// Fps y contraste durante una ventana de tiempo. Se mide por TIEMPO y no por
// número de frames: contar 20 frames a 8 fps costaría 2,5 s por candidata, y lo
// que hay que distinguir es un acantilado (8 contra 30), no un matiz. Con
// 400 ms salen 3 frames en el caso lento y 12 en el rápido, que separa de
// sobra.
//
// El contraste es la desviación típica de la imagen, y se mide en la misma
// pasada y no aparte porque es la otra mitad de la decisión: una exposición que
// da muchos fps y deja la pieza indistinguible del fondo no sirve de nada.
SceneObservation observe(cv::VideoCapture& capture) {
    cv::Mat frame;
    // La cámara tarda un par de frames en aplicar un cambio; medirlos contaría
    // el tiempo de la exposición vieja.
    for (int i = 0; i < 2; ++i) {
        capture.read(frame);
    }
    const auto start = std::chrono::steady_clock::now();
    constexpr auto kWindow = std::chrono::milliseconds(400);
    int frames = 0;
    double contrastSum = 0.0;
    while (std::chrono::steady_clock::now() - start < kWindow) {
        if (capture.read(frame) && !frame.empty()) {
            ++frames;
            cv::Scalar mean;
            cv::Scalar stddev;
            cv::meanStdDev(frame, mean, stddev);
            contrastSum += stddev[0];
        }
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    SceneObservation observation;
    observation.fps = seconds > 0.0 ? frames / seconds : 0.0;
    observation.contrast = frames > 0 ? contrastSum / frames : 0.0;
    return observation;
}

}  // namespace

void CameraController::drainExposureSweep(cv::VideoCapture& capture) {
    double minExposure = 0.0;
    double maxExposure = 0.0;
    {
        std::lock_guard<std::mutex> lock(controlsMutex_);
        if (!exposureSweepPending_) {
            return;
        }
        exposureSweepPending_ = false;
        minExposure = exposureSweepMin_;
        maxExposure = exposureSweepMax_;
    }

    core::setBreadcrumb("midiendo los fps de cada exposición");

    // Cableado y nada más: la decisión entera vive en `runExposureProfile`, que
    // no sabe qué es una cv::VideoCapture y por eso se puede probar. Aquí solo
    // se le enseña esta cámara.
    ExposureSweepCamera seam;
    seam.setExposure = [&capture](double exposure) {
        capture.set(cv::CAP_PROP_EXPOSURE, exposure);
    };
    seam.setAutoExposure = [&capture](bool on) {
        capture.set(cv::CAP_PROP_AUTO_EXPOSURE, on ? 1.0 : 0.0);
    };
    seam.observe = [&capture] { return observe(capture); };

    const ExposureProfileResult result = runExposureProfile(seam, minExposure, maxExposure);
    for (const auto& sample : result.sweep) {
        core::logInfo("Exposición " + std::to_string(sample.exposure) + " -> " +
                      std::to_string(sample.fps) + " fps");
    }

    switch (result.outcome) {
        case ExposureProfileOutcome::Untouched:
        case ExposureProfileOutcome::Undecided:
            // Nada que contarle al operador: la cámara sigue como estaba y
            // avisar de cada no-cambio es la mejor forma de que deje de leer
            // los avisos.
            core::logWarning("Exposición sin fijar: " + result.reason);
            return;
        case ExposureProfileOutcome::Ignored:
        case ExposureProfileOutcome::Reverted:
            // Las dos acaban en automático, y el operador tiene que saberlo:
            // significa que sus medidas no son repetibles todavía.
            core::logWarning("Exposición fija descartada: " + result.reason);
            emit profileRejected(QString::fromStdString(result.reason));
            return;
        case ExposureProfileOutcome::Applied:
            core::logInfo("Exposición fija aceptada: " + std::to_string(result.fixed.fps) +
                          " fps con " +
                          std::to_string(static_cast<int>(result.fixed.contrast)) +
                          " de contraste (en automático daba " +
                          std::to_string(result.automatic.fps) + " fps con " +
                          std::to_string(static_cast<int>(result.automatic.contrast)) + ")");
            emit exposureChosen(*result.exposure, result.sweep);
            return;
    }
}

void CameraController::drainResolutionRequests(cv::VideoCapture& capture) {
    std::optional<CameraResolution> wanted;
    bool probe = false;
    {
        std::lock_guard<std::mutex> lock(controlsMutex_);
        wanted.swap(pendingResolution_);
        probe = resolutionProbePending_;
        resolutionProbePending_ = false;
    }

    if (wanted.has_value()) {
        try {
            capture.set(cv::CAP_PROP_FRAME_WIDTH, wanted->width);
            capture.set(cv::CAP_PROP_FRAME_HEIGHT, wanted->height);
        } catch (const cv::Exception& e) {
            core::logWarning(std::string("OpenCV lanzó al cambiar la resolución: ") +
                             e.what());
        }
        const CameraResolution applied = currentResolution(capture);
        core::logInfo("Resolución pedida " + std::to_string(wanted->width) + "x" +
                      std::to_string(wanted->height) + ", la cámara dio " +
                      std::to_string(applied.width) + "x" + std::to_string(applied.height));
    }

    if (probe) {
        core::setBreadcrumb("sondeando resoluciones de la cámara");
        const std::vector<CameraResolution> available = probeResolutions(capture);
        emit resolutionsProbed(available, currentResolution(capture));
    }
}

void CameraController::drainControlRequests(cv::VideoCapture& capture) {
    std::vector<CameraControlValue> pending;
    {
        std::lock_guard<std::mutex> lock(controlsMutex_);
        if (pendingControls_.empty()) {
            return;
        }
        pending.swap(pendingControls_);
    }
    for (const auto& control : pending) {
        bool applied = false;
        try {
            applied = capture.set(captureProperty(control.property), control.value);
        } catch (const cv::Exception& e) {
            core::logWarning(std::string("OpenCV lanzó al fijar un control de cámara: ") +
                             e.what());
        }
        if (!applied) {
            // Falla en silencio a propósito: muchas cámaras devuelven false
            // aunque el valor sí se aplique, y otras no soportan la propiedad.
            // Queda en el log y la UI muestra lo que la cámara devuelve.
            core::logWarning(std::string("La cámara rechazó el control ") +
                             std::string(propertyKey(control.property)) + " = " +
                             std::to_string(control.value));
        }
    }
}

void CameraController::stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

// Cualquier excepción que escape de un std::thread termina el proceso: el
// cuerpo completo va blindado para que un driver roto solo detenga el video.
void CameraController::captureLoop(CameraInfo camera) {
    try {
        captureLoopBody(std::move(camera));
    } catch (const std::exception& e) {
        core::logError(std::string("Fallo interno de captura: ") + e.what());
        running_ = false;
        emit cameraError(tr("Fallo interno de la cámara (ver log)"));
        emit stopped();
    } catch (...) {
        core::logError("Fallo interno de captura desconocido");
        running_ = false;
        emit cameraError(tr("Fallo interno de la cámara (ver log)"));
        emit stopped();
    }
}

void CameraController::captureLoopBody(CameraInfo camera) {
    cv::VideoCapture capture;

    // La apertura es el punto más peligroso: un driver de captura defectuoso
    // puede dividir por cero al negociar el formato y matar el proceso a nivel
    // del SO. La miga de pan deja constancia de qué se intentaba abrir por si el
    // fallo escapa incluso al blindaje SEH; runProtected atrapa la excepción
    // estructurada y la convierte en un simple "no se pudo abrir".
    core::setBreadcrumb("abriendo cámara '" + camera.name + "' (índice " +
                        std::to_string(camera.index) + ", backend " +
                        std::to_string(camera.backend) + ")");
    OpenArgs args{&capture, camera.index, camera.backend};
    unsigned long sehCode = 0;
    const bool survived = core::runProtected(&openTrampoline, &args, &sehCode);

    if (!survived) {
        core::logError("Excepción estructurada del SO abriendo " + camera.name +
                       " (código " + toHex(sehCode) +
                       "): driver de captura roto o cámara no lista "
                       "(¿AndroidCam sin conectar el celular?)");
        running_ = false;
        emit cameraError(
            tr("El dispositivo falló al abrir: driver defectuoso o cámara no "
               "lista (revisa el log)"));
        emit stopped();
        return;
    }

    if (!capture.isOpened()) {
        core::logError("No se pudo abrir " + camera.name);
        running_ = false;
        emit cameraError(tr("No se pudo abrir %1").arg(QString::fromStdString(camera.name)));
        emit stopped();
        return;
    }

    core::setBreadcrumb("cámara '" + camera.name + "' abierta, leyendo frames");

    // Buffer mínimo: preferimos perder frames viejos a acumular latencia.
    capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
    core::logInfo("Captura iniciada en " + camera.name);

    // Controles pedidos antes de abrir (los guardados de la sesión anterior).
    drainControlRequests(capture);

    // Sondeo real de los controles: qué acepta la cámara y con qué rango.
    // (Antes se miraba solo get() y salían deslizadores muertos: hay cámaras
    // que informan el brillo pero rechazan cambiarlo.)
    const std::vector<CameraControlState> probed = probeControls(capture);
    for (const auto& state : probed) {
        core::logInfo(std::string("Control ") + std::string(propertyKey(state.property)) +
                      (state.supported ? ": ajustable [" + std::to_string(state.min) + ", " +
                                             std::to_string(state.max) + "]"
                                       : ": no ajustable"));
    }
    emit controlsProbed(probed);

    cv::Mat frame;  // reutilizado entre iteraciones, sin allocar por frame
    core::FpsCounter fpsCounter;
    int consecutiveFailures = 0;
    auto lastStats = std::chrono::steady_clock::now();

    while (running_) {
        bool grabbed = false;
        try {
            grabbed = capture.read(frame);
        } catch (const cv::Exception& e) {
            core::logWarning(std::string("Excepción de OpenCV leyendo frame: ") + e.what());
        }

        if (!grabbed || frame.empty()) {
            if (++consecutiveFailures >= kMaxConsecutiveFailures) {
                core::logError(camera.name + " desconectada o sin señal");
                emit cameraError(tr("Cámara desconectada o sin señal"));
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        consecutiveFailures = 0;
        drainControlRequests(capture);     // cambios pedidos desde la UI
        drainResolutionRequests(capture);  // sondeo o cambio de resolución
        drainExposureSweep(capture);       // elegir exposición midiendo (C1)
        const auto now = std::chrono::steady_clock::now();
        fpsCounter.tick(now);
        emit frameReady(matToQImage(frame));

        if (now - lastStats >= kStatsInterval) {
            emit statsUpdated(fpsCounter.fps(now), frame.cols, frame.rows);
            lastStats = now;
        }
    }

    capture.release();
    running_ = false;
    core::logInfo("Captura detenida en " + camera.name);
    emit stopped();
}

}  // namespace pci::camera
