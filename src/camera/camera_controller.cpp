#include "camera/camera_controller.h"

#include <opencv2/videoio.hpp>

#include <chrono>

#include "camera/frame_utils.h"
#include "core/fps_counter.h"
#include "core/logging.h"

namespace pci::camera {

namespace {

constexpr int kBackend =
#ifdef _WIN32
    cv::CAP_MSMF;
#else
    cv::CAP_V4L2;
#endif

// ~1 segundo sin frames a 30 fps antes de declarar la cámara perdida.
constexpr int kMaxConsecutiveFailures = 30;
constexpr auto kStatsInterval = std::chrono::milliseconds(500);

}  // namespace

CameraController::CameraController(QObject* parent) : QObject(parent) {}

CameraController::~CameraController() {
    stop();
}

void CameraController::start(int cameraIndex) {
    stop();
    running_ = true;
    worker_ = std::thread(&CameraController::captureLoop, this, cameraIndex);
}

void CameraController::stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void CameraController::captureLoop(int cameraIndex) {
    cv::VideoCapture capture;
    try {
        capture.open(cameraIndex, kBackend);
    } catch (const cv::Exception& e) {
        core::logError(std::string("Excepción de OpenCV abriendo cámara: ") + e.what());
    }

    if (!capture.isOpened()) {
        core::logError("No se pudo abrir la cámara " + std::to_string(cameraIndex));
        running_ = false;
        emit cameraError(tr("No se pudo abrir la cámara %1").arg(cameraIndex));
        emit stopped();
        return;
    }

    // Buffer mínimo: preferimos perder frames viejos a acumular latencia.
    capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
    core::logInfo("Captura iniciada en cámara " + std::to_string(cameraIndex));

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
                core::logError("Cámara " + std::to_string(cameraIndex) +
                               " desconectada o sin señal");
                emit cameraError(tr("Cámara desconectada o sin señal"));
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        consecutiveFailures = 0;
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
    core::logInfo("Captura detenida en cámara " + std::to_string(cameraIndex));
    emit stopped();
}

}  // namespace pci::camera
