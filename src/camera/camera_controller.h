#pragma once

#include <QImage>
#include <QObject>

#include <atomic>
#include <thread>

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

    // Abre la cámara con el backend con el que fue detectada.
    void start(const CameraInfo& camera);
    void stop();

signals:
    void frameReady(const QImage& frame);
    void statsUpdated(double fps, int width, int height);
    void cameraError(const QString& message);
    void stopped();

private:
    void captureLoop(CameraInfo camera);

    std::thread worker_;
    std::atomic<bool> running_{false};
};

}  // namespace pci::camera
