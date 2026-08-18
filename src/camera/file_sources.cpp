#include "camera/file_sources.h"

#include <QFile>
#include <opencv2/imgcodecs.hpp>
#include <vector>
#include <QFileInfo>

#include <opencv2/imgproc.hpp>

#include <algorithm>
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
        // Qt no ha podido, así que lo intenta OpenCV. No es redundancia: son dos
        // lectores con catálogos distintos.
        //
        // Qt lee PNG y BMP de serie y delega JPEG, TIFF y compañía en
        // COMPLEMENTOS que hay que desplegar junto al ejecutable. En esta
        // instalación solo hay gif, ico y jpeg — no hay `qtiff`— y el diálogo de
        // la aplicación ofrece `.tif` y `.tiff`: se estaban ofreciendo formatos
        // que no se podían abrir. Con el JPEG pasa algo parecido y peor de
        // diagnosticar, porque depende de que el complemento se encuentre en
        // tiempo de ejecución.
        //
        // OpenCV trae sus decodificadores DENTRO de la biblioteca, y ya es una
        // dependencia de este programa. Los bytes se leen con `QFile` y no con
        // `cv::imread`: `imread` recibe la ruta como `std::string` y en Windows
        // eso rompe con acentos o con «ñ», que en español no es un caso raro.
        QFile file(path_);
        cv::Mat decoded;
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.readAll();
            const std::vector<uchar> buffer(bytes.begin(), bytes.end());
            decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
        }
        if (!decoded.empty()) {
            frame_ = toQImage(decoded);
            core::logInfo("La imagen «" + QFileInfo(path_).fileName().toStdString() +
                          "» no la pudo leer Qt y sí OpenCV: probablemente falta el "
                          "complemento de imagen de Qt para ese formato");
        } else {
            // El motivo tiene que ser accionable: el operador necesita saber si
            // se equivocó de fichero o si el formato no se lee.
            emit sourceError(
                tr("No se pudo abrir la imagen «%1», ni con Qt ni con OpenCV. Comprueba "
                   "que la ruta existe, que el fichero no está corrupto y que es una "
                   "imagen de verdad (PNG, JPG, BMP o TIFF).")
                    .arg(QFileInfo(path_).fileName()));
            emit stopped();
            return;
        }
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

void VideoFileSource::setPaused(bool paused) {
    paused_.store(paused);
}

void VideoFileSource::seekToFraction(double fraction) {
    // Se apunta y se aplica en el bucle. Guardarlo acotado evita que un valor
    // fuera de rango llegue a `CAP_PROP_POS_FRAMES`.
    seekRequest_.store(std::clamp(fraction, 0.0, 1.0));
}

void VideoFileSource::stepOneFrame() {
    stepRequest_.store(true);
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

    // Cuántos frames tiene. Puede no saberse —hay contenedores que no lo
    // dicen— y en ese caso se informa 0: quien pinte la barra tiene que
    // apagarla, no inventar una posición.
    const auto totalFrames =
        static_cast<qint64>(std::max(0.0, capture.get(cv::CAP_PROP_FRAME_COUNT)));

    cv::Mat frame;
    while (running_.load()) {
        const auto tick = std::chrono::steady_clock::now();

        // El salto se aplica AQUÍ y no donde se pide: mover el `VideoCapture`
        // desde el hilo de la interfaz mientras este lee es pedir una
        // corrupción.
        if (const double wanted = seekRequest_.exchange(-1.0); wanted >= 0.0) {
            // Se salta por MILISEGUNDOS y no por número de frame.
            //
            // `CAP_PROP_POS_FRAMES` sobre un MP4 con H.264 es de las cosas menos
            // fiables de OpenCV: el contenedor no indexa frame a frame, así que
            // pedir el 45.000 obliga a decodificar desde la última clave, y en
            // un vídeo de 50 minutos eso son segundos de espera con la interfaz
            // parada. Además, después devuelve la posición de la CLAVE y no la
            // pedida, que es por lo que el pulgar y el tiempo no cuadraban.
            //
            // `CAP_PROP_POS_MSEC` usa el índice de tiempo del contenedor, que es
            // justo para lo que está.
            const double clamped = std::clamp(wanted, 0.0, 1.0);
            const double totalMs = fps > 0.0 && totalFrames > 0
                                       ? 1000.0 * totalFrames / fps
                                       : 0.0;
            if (totalMs > 0.0) {
                capture.set(cv::CAP_PROP_POS_MSEC, clamped * totalMs);
            } else if (totalFrames > 0) {
                capture.set(cv::CAP_PROP_POS_FRAMES, clamped * (totalFrames - 1));
            }
        }

        // En pausa se sigue atendiendo: parar y saltar tienen que funcionar con
        // el vídeo detenido, que es justo cuando más se usan. Se espera a
        // trozos cortos en vez de dormir de una vez, o cerrar tardaría lo que
        // durase la siesta.
        const bool stepping = stepRequest_.exchange(false);
        if (paused_.load() && !stepping) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

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
        // La posición sale del tiempo y se convierte a frame: `POS_FRAMES` tras
        // un salto devuelve la clave, no donde se está.
        const double atMs = capture.get(cv::CAP_PROP_POS_MSEC);
        const auto atFrame =
            atMs > 0.0 && fps > 0.0
                ? static_cast<qint64>(atMs * fps / 1000.0)
                : static_cast<qint64>(std::max(0.0, capture.get(cv::CAP_PROP_POS_FRAMES)));
        emit positionChanged(atFrame, totalFrames, fps);
        if (stepping) {
            // Un paso deja el vídeo parado en el frame nuevo: es lo que se pide
            // cuando se está buscando EL frame.
            paused_.store(true);
            continue;
        }
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
