#include "camera/camera_controller.h"

#include <opencv2/videoio.hpp>

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
    for (const auto& control : controls) {
        pendingControls_.push_back(control);
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

    // Sondeo: OpenCV no dice qué propiedades existen, así que se lee cada una y
    // se considera no soportada si devuelve -1 o algo no finito. Es la única
    // comprobación posible que no toca la cámara.
    std::vector<CameraControlState> probed;
    for (const CameraProperty property : allCameraProperties()) {
        CameraControlState state;
        state.property = property;
        try {
            state.value = capture.get(captureProperty(property));
        } catch (const cv::Exception& e) {
            core::logWarning(std::string("OpenCV lanzó al leer un control: ") + e.what());
            state.value = -1.0;
        }
        state.supported = std::isfinite(state.value) && state.value != -1.0;
        probed.push_back(state);
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
        drainControlRequests(capture);  // cambios pedidos desde la UI
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
