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

    // --- Control de reproducción ---
    //
    // Un vídeo sin pausa ni barra de tiempo no se puede usar para lo que se
    // abre un vídeo: encontrar EL frame en el que la pieza se ve bien y trabajar
    // sobre él. Sin esto había que reabrirlo y esperar a que el bucle volviera a
    // pasar por donde uno quería.
    //
    // Todo lo que cruza al hilo de reproducción va en atómicos: el bucle vive en
    // su propio hilo y la interfaz lo toca desde el suyo.
    void setPaused(bool paused);
    [[nodiscard]] bool isPaused() const { return paused_.load(); }
    // Salta a una fracción del vídeo (0 = principio, 1 = final). Se pide y se
    // aplica en el bucle: mover el `VideoCapture` desde otro hilo mientras está
    // leyendo es pedir una corrupción.
    void seekToFraction(double fraction);
    // Avanza UN frame y se queda en pausa. Es lo que hace falta para elegir el
    // frame exacto, y con la barra no se puede: un píxel de barra son varios
    // frames en un vídeo largo.
    void stepOneFrame();

signals:
    // Dónde va la reproducción. `total` es 0 cuando el contenedor no sabe
    // cuántos frames tiene —pasa, y más de lo que parece—, y entonces la barra
    // no puede colocarse: quien la pinta debe apagarla en vez de inventar una
    // posición.
    void positionChanged(qint64 frame, qint64 total, double fps);

private:
    void playLoop();

    QString path_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    // −1 = sin salto pendiente. Se usa un doble porque la petición viene en
    // fracción y el número de frames solo se conoce dentro del bucle.
    std::atomic<double> seekRequest_{-1.0};
    std::atomic<bool> stepRequest_{false};
};

// Convierte un frame de OpenCV (BGR o gris) a QImage RGB888, con los datos
// COPIADOS. La copia no es opcional: `cv::Mat` es de recuento de referencias y
// el `QImage` cruza al hilo de la interfaz, donde el `Mat` que lo respaldaba ya
// no existe.
[[nodiscard]] QImage toQImage(const cv::Mat& frame);

}  // namespace pci::camera
