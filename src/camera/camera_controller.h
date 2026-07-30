#pragma once

#include <QImage>
#include <QObject>

#include <opencv2/videoio.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "camera/camera_controls.h"
#include "camera/camera_info.h"

namespace pci::camera {

// Captura frames en su propio hilo y entrega copias QImage al hilo de UI por
// señales encoladas. Nunca bloquea la UI; la desconexión en caliente se
// detecta por lecturas fallidas consecutivas y termina con cameraError.
class CameraController : public QObject {
    Q_OBJECT

public:
    explicit CameraController(QObject* parent = nullptr);
    ~CameraController() override;

    [[nodiscard]] bool isRunning() const { return running_.load(); }

    // Abre la cámara con el backend con el que fue detectada. Los controles
    // (brillo, exposición…) se aplican en cuanto la cámara está abierta.
    void start(const CameraInfo& camera,
               const std::vector<CameraControlValue>& initialControls = {});
    void stop();

    // Pide aplicar controles de la fuente. Se encolan y los aplica el HILO DE
    // CAPTURA: cv::VideoCapture no es thread-safe y tocarla desde la UI puede
    // colgar o corromper el driver. Sin transmisión en curso, no hace nada.
    void requestControls(const std::vector<CameraControlValue>& controls);

signals:
    void frameReady(const QImage& frame);
    // Estado de los controles leído al abrir la cámara: qué soporta y con qué
    // valor arranca. La UI deshabilita lo no soportado en vez de mentir.
    void controlsProbed(const std::vector<pci::camera::CameraControlState>& controls);
    void statsUpdated(double fps, int width, int height);
    void cameraError(const QString& message);
    void stopped();

private:
    void captureLoop(CameraInfo camera);
    void captureLoopBody(CameraInfo camera);
    // Vacía la cola de peticiones sobre la cámara ya abierta (hilo de captura).
    void drainControlRequests(cv::VideoCapture& capture);

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex controlsMutex_;
    std::vector<CameraControlValue> pendingControls_;
};

}  // namespace pci::camera

// La lista de controles cruza de hilo por una conexión encolada.
Q_DECLARE_METATYPE(std::vector<pci::camera::CameraControlState>)
