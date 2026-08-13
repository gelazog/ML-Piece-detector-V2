#pragma once

#include <QImage>
#include <QObject>
#include <QString>

namespace pci::camera {

// De dónde salen los frames.
//
// La observación que ordena todo esto: `MainWindow` cuelga entero de una señal
// —un `QImage` que llega— y todo lo que hay detrás (segmentar, contorno,
// fixture, herramientas, zona, recuento, medición automática, inspección) ya
// funciona sobre ese `QImage` sin preguntar de dónde salió. Lo único atado a la
// cámara es QUIÉN produce el frame.
//
// Así que «que lo del vídeo funcione sobre una imagen» no es un modo nuevo ni
// una pantalla nueva: es una FUENTE más. Hacerlo como modo aparte daría dos
// caminos que divergen, y este proyecto ya pagó eso una vez con los botones y
// otra con las tres paletas.

enum class SourceKind {
    Camera,  // cámara en vivo
    Image,   // una imagen de archivo
    Video,   // un vídeo de archivo
};

// Qué se puede hacer con cada fuente. No es cosmético: la interfaz tiene que
// deshabilitar CON MOTIVO lo que no aplica, y para eso hay que preguntárselo a
// la fuente en vez de escribir `if (esCamara)` repartido por la ventana.
struct SourceCapabilities {
    bool adjustableControls = false;  // brillo, exposición, enfoque…
    bool selectableResolution = false;
    bool meaningfulCaptureFps = false;  // ¿los fps de captura dicen algo?
    bool focusable = false;             // ¿tiene sentido el asistente de enfoque?
};

[[nodiscard]] SourceCapabilities capabilitiesOf(SourceKind kind);

// Por qué NO se puede tocar algo, en castellano y dicho al operador. Un control
// muerto sin explicación es peor que un control ausente: el operador se queda
// pensando que la aplicación está rota.
[[nodiscard]] QString whyNotAdjustable(SourceKind kind);

// La interfaz común. Deliberadamente pequeña: solo lo que TODA fuente puede
// prometer. Lo que es propio de la cámara —sondear controles, elegir
// resolución, medir el perfil de exposición— se queda en `CameraController` y
// no se finge aquí, porque una interfaz que promete lo que sus
// implementaciones no pueden cumplir obliga a rellenarla con métodos vacíos.
class FrameSource : public QObject {
    Q_OBJECT

public:
    explicit FrameSource(QObject* parent = nullptr) : QObject(parent) {}
    ~FrameSource() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;
    [[nodiscard]] virtual SourceKind kind() const = 0;
    // Cómo se llama esta fuente para el operador: el nombre del fichero, no una
    // ruta de 200 caracteres ni un índice.
    [[nodiscard]] virtual QString describe() const = 0;

signals:
    void frameReady(const QImage& frame);
    void statsUpdated(double fps, int width, int height);
    void sourceError(const QString& message);
    void stopped();
};

}  // namespace pci::camera
