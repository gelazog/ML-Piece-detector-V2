#pragma once

#include <QImage>
#include <QString>
#include <QTimer>

#include <opencv2/videoio.hpp>

#include <atomic>
#include <thread>

#include "camera/frame_source.h"

namespace pci::camera {

// Una imagen de archivo, servida como si fuera una cámara.
//
// Reemite el MISMO frame a un ritmo bajo en vez de una sola vez, y eso es una
// decisión, no una pereza: media aplicación reacciona a «llegó un frame nuevo»
// —el análisis, el fixture, la zona de trabajo, el recuento—, así que emitir
// una vez dejaría la pantalla congelada en cuanto el operador tocara un ajuste
// de detección. Reemitir hace que todo eso siga vivo sin tener que ir buscando
// cada camino que necesita repintarse.
//
// Y a ritmo BAJO porque repetir treinta veces por segundo el análisis de una
// imagen que no cambia es quemar CPU para nada. Cuatro veces por segundo es
// suficiente para que un cambio de ajuste se vea al instante.
class StillImageSource : public FrameSource {
    Q_OBJECT

public:
    // Desde un fichero: se lee al arrancar y se avisa si no se puede.
    explicit StillImageSource(QString path, QObject* parent = nullptr);

    // Desde una imagen que ya se tiene en memoria — una FOTO recién congelada
    // del vídeo. `label` es lo que se le enseña al operador («Foto 10:57:12»),
    // porque aquí no hay nombre de fichero del que tirar.
    StillImageSource(QImage frame, QString label, SourceKind kind,
                     QObject* parent = nullptr);

    void start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override { return timer_.isActive(); }
    [[nodiscard]] SourceKind kind() const override { return kind_; }
    [[nodiscard]] QString describe() const override;

    // Cada cuánto se reemite. Público para que el test no tenga que esperar.
    static constexpr int kRepeatMs = 250;

private:
    void wire();

    QString path_;
    QString label_;
    QImage frame_;
    SourceKind kind_ = SourceKind::Image;
    QTimer timer_;
};

// Un vídeo de archivo. Va en su propio hilo, como la cámara, y por el mismo
// motivo: descodificar un frame de 1080p cuesta milisegundos y hacerlo en el
// hilo de la interfaz la dejaría a tirones. La regla de esta capa es que nada
// bloquea la UI.
//
// Al llegar al final vuelve a empezar. Un vídeo que se para en el último frame
// obligaría a reabrirlo para volver a mirar, y de un vídeo de una pieza lo que
// se quiere es justo verlo en bucle mientras se ajusta la detección.
class VideoFileSource : public FrameSource {
    Q_OBJECT

public:
    explicit VideoFileSource(QString path, QObject* parent = nullptr);
    ~VideoFileSource() override;

    void start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override { return running_.load(); }
    [[nodiscard]] SourceKind kind() const override { return SourceKind::Video; }
    [[nodiscard]] QString describe() const override;

private:
    void playLoop();

    QString path_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

// Convierte un frame de OpenCV (BGR o gris) a QImage RGB888, con los datos
// COPIADOS. La copia no es opcional: `cv::Mat` es de recuento de referencias y
// el `QImage` cruza al hilo de la interfaz, donde el `Mat` que lo respaldaba ya
// no existe.
[[nodiscard]] QImage toQImage(const cv::Mat& frame);

}  // namespace pci::camera
