#include "camera/file_sources.h"

#include <QFileInfo>

#include <opencv2/imgproc.hpp>

#include <chrono>
#include <utility>

#include "core/logging.h"

namespace pci::camera {

namespace {

// Frames por segundo con los que se reproduce un vídeo cuando el fichero no
// dice los suyos. Algunos contenedores devuelven 0 o un número absurdo, y
// dividir por eso da o una espera infinita o una ráfaga a máxima velocidad.
constexpr double kFallbackFps = 25.0;
constexpr double kMaxSaneFps = 240.0;

}  // namespace

QImage toQImage(const cv::Mat& frame) {
    if (frame.empty()) {
        return {};
    }
    cv::Mat rgb;
    if (frame.channels() == 1) {
        cv::cvtColor(frame, rgb, cv::COLOR_GRAY2RGB);
    } else if (frame.channels() == 4) {
        cv::cvtColor(frame, rgb, cv::COLOR_BGRA2RGB);
    } else {
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    }
    // `copy()` porque el QImage sobrevive al `Mat`: sin ella se leería memoria
    // liberada en cuanto la señal cruzara de hilo.
    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
                  QImage::Format_RGB888)
        .copy();
}

// ---------------------------------------------------------------------------
// Imagen
// ---------------------------------------------------------------------------

StillImageSource::StillImageSource(QString path, QObject* parent)
    : FrameSource(parent), path_(std::move(path)) {
    wire();
}

StillImageSource::StillImageSource(QImage frame, QString label, SourceKind kind, QObject* parent)
    : FrameSource(parent), label_(std::move(label)), frame_(std::move(frame)), kind_(kind) {
    // A RGB888 aquí y no al emitir: la conversión cuesta lo mismo una vez que
    // cuatro veces por segundo.
    if (!frame_.isNull() && frame_.format() != QImage::Format_RGB888) {
        frame_ = frame_.convertToFormat(QImage::Format_RGB888);
    }
    wire();
}

void StillImageSource::wire() {
    timer_.setInterval(kRepeatMs);
    connect(&timer_, &QTimer::timeout, this, [this] {
        emit frameReady(frame_);
        // Los fps de una imagen fija no significan nada, y decir «4 fps» sería
        // una respuesta a una pregunta que nadie hizo. Se manda el TAMAÑO, que
        // sí importa —la calibración depende de él— y los fps a cero para que
        // la barra de estado sepa que aquí no hay ritmo que enseñar.
        emit statsUpdated(0.0, frame_.width(), frame_.height());
    });
}

QString StillImageSource::describe() const {
    return path_.isEmpty() ? label_ : QFileInfo(path_).fileName();
}

void StillImageSource::start() {
    // Con la imagen ya en memoria —una foto congelada— no hay nada que leer.
    if (path_.isEmpty()) {
        if (frame_.isNull()) {
            emit sourceError(tr("La foto llegó vacía: no hay nada que analizar."));
            emit stopped();
            return;
        }
        core::logInfo("Fuente: foto congelada (" + std::to_string(frame_.width()) + "x" +
                      std::to_string(frame_.height()) + ")");
        emit frameReady(frame_);
        emit statsUpdated(0.0, frame_.width(), frame_.height());
        timer_.start();
        return;
    }
    if (!frame_.load(path_)) {
        // El motivo tiene que ser accionable: el operador necesita saber si se
        // equivocó de fichero o si el formato no se lee.
        emit sourceError(tr("No se pudo abrir la imagen «%1». Comprueba que la ruta existe y "
                            "que el formato es uno que la aplicación lee (PNG, JPG, BMP).")
                             .arg(QFileInfo(path_).fileName()));
        emit stopped();
        return;
    }
    // A RGB888 como hace la cámara: el resto de la aplicación cuenta con ese
    // formato, y un PNG con canal alfa o indexado llegaría de otra forma.
    if (frame_.format() != QImage::Format_RGB888) {
        frame_ = frame_.convertToFormat(QImage::Format_RGB888);
    }
    core::logInfo("Fuente: imagen " + describe().toStdString() + " (" +
                  std::to_string(frame_.width()) + "x" + std::to_string(frame_.height()) + ")");
    // El primero se emite ya, sin esperar al temporizador: si no, la ventana se
    // queda un cuarto de segundo en negro después de abrir el fichero, y eso se
    // lee como «no ha cargado».
    emit frameReady(frame_);
    emit statsUpdated(0.0, frame_.width(), frame_.height());
    timer_.start();
}

void StillImageSource::stop() {
    if (!timer_.isActive()) {
        return;
    }
    timer_.stop();
    emit stopped();
}

// ---------------------------------------------------------------------------
// Vídeo
// ---------------------------------------------------------------------------

VideoFileSource::VideoFileSource(QString path, QObject* parent)
    : FrameSource(parent), path_(std::move(path)) {}

VideoFileSource::~VideoFileSource() {
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
}

QString VideoFileSource::describe() const { return QFileInfo(path_).fileName(); }

void VideoFileSource::start() {
    if (running_.load()) {
        return;
    }
    running_.store(true);
    worker_ = std::thread([this] { playLoop(); });
}

void VideoFileSource::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    emit stopped();
}

void VideoFileSource::playLoop() {
    cv::VideoCapture capture(path_.toStdString());
    if (!capture.isOpened()) {
        running_.store(false);
        emit sourceError(tr("No se pudo abrir el vídeo «%1». Puede que falte el códec: OpenCV "
                            "lee sobre todo MP4 (H.264), AVI y MKV.")
                             .arg(QFileInfo(path_).fileName()));
        emit stopped();
        return;
    }

    double fps = capture.get(cv::CAP_PROP_FPS);
    if (!(fps > 0.0) || fps > kMaxSaneFps) {
        // No se cree lo que el fichero dice, se comprueba que sea creíble. Es
        // la misma regla que con la cámara, y por el mismo motivo: un
        // contenedor puede devolver 0 o 1000, y de ahí sale o una espera
        // infinita o una ráfaga a toda velocidad.
        core::logWarning("El vídeo declara " + std::to_string(fps) +
                         " fps, que no es creíble: se reproduce a " +
                         std::to_string(static_cast<int>(kFallbackFps)));
        fps = kFallbackFps;
    }
    const auto period = std::chrono::microseconds(static_cast<long long>(1e6 / fps));

    cv::Mat frame;
    while (running_.load()) {
        const auto tick = std::chrono::steady_clock::now();
        if (!capture.read(frame) || frame.empty()) {
            // Fin del fichero: se vuelve al principio. Un vídeo parado en el
            // último frame obligaría a reabrirlo para volver a mirar, y de un
            // vídeo de una pieza lo que se quiere es verlo en bucle mientras se
            // ajusta la detección.
            capture.set(cv::CAP_PROP_POS_FRAMES, 0);
            if (!capture.read(frame) || frame.empty()) {
                break;  // no era el final: el fichero no da frames
            }
        }
        emit frameReady(toQImage(frame));
        emit statsUpdated(fps, frame.cols, frame.rows);
        std::this_thread::sleep_until(tick + period);
    }

    const bool ranOut = running_.exchange(false);
    if (ranOut) {
        // Salió del bucle sin que nadie pidiera parar: el fichero se agotó de
        // verdad y hay que decirlo, no quedarse mudo.
        emit sourceError(tr("El vídeo «%1» no devuelve frames.").arg(QFileInfo(path_).fileName()));
        emit stopped();
    }
}

}  // namespace pci::camera
