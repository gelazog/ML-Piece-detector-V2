#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <QImage>
#include <QTimer>

#include <cstdint>
#include <memory>

#include "core/result.h"
#include "engine/registration_session.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace pci::camera {
class CameraController;
}
namespace pci::repositories {
class PieceRepository;
}

namespace pci::ui {

class VideoWidget;

// Registro guiado de una pieza nueva: captura manual o automática desde la
// cámara (o imágenes desde archivo para equipos sin cámara), validación de
// calidad por captura con motivo de rechazo, y guardado de la referencia.
class RegistrationWizard : public QDialog {
    Q_OBJECT

public:
    // `pipeline` es la detección que el operador tiene configurada, y no es un
    // parámetro de adorno: la sesión de registro SEGMENTA cada captura para
    // sacar el recorte del que nace el embedding. Sin esto, el asistente
    // aprendía la pieza con los valores de fábrica mientras el botón de
    // «Registrar y activar» —que hace lo mismo— usaba los configurados.
    //
    // Dos caminos para la misma operación con dos detecciones distintas. Sobre
    // una mesa de color, el asistente aprendía la pieza de una segmentación
    // rota, y esa referencia torcida se compara luego contra inspecciones bien
    // detectadas.
    RegistrationWizard(camera::CameraController* controller, engine::EmbedFn embedFn,
                       repositories::PieceRepository* pieces,
                       const vision::PipelineConfig& pipeline = {},
                       QWidget* parent = nullptr);

    // REGISTRAR OTRO ACABADO de una pieza que YA existe.
    //
    // Es el mismo flujo de capturas apuntando a otro sitio: en vez de crear una
    // pieza, se guarda una variante mas de la que ya hay. Existe porque meter
    // dos acabados admisibles en la misma media no da falsos NG — deja CIEGA la
    // referencia, y esa es la unica forma que habia de registrarlos hasta ahora
    // (`ml/reference.h` lleva las cifras).
    //
    // Y no vale con «registrar la pieza otra vez»: eso crearia una pieza
    // distinta, con sus herramientas y su historial aparte, cuando lo que hay
    // delante es la MISMA pieza con otro acabado.
    RegistrationWizard(camera::CameraController* controller, engine::EmbedFn embedFn,
                       repositories::PieceRepository* pieces, std::int64_t existingPieceId,
                       const QString& pieceName,
                       const vision::PipelineConfig& pipeline = {},
                       QWidget* parent = nullptr);
    ~RegistrationWizard() override;

    [[nodiscard]] std::int64_t createdPieceId() const { return createdPieceId_; }
    // El nombre de la variante que se acabo de guardar, vacio si se registro una
    // pieza nueva. Lo usan la ventana —para decirlo— y las pruebas.
    [[nodiscard]] QString savedVariant() const { return savedVariant_; }

private slots:
    void onFrame(const QImage& frame);
    void onCaptureClicked();
    void onAutoToggled(bool enabled);
    void onAutoTick();
    void onFilesClicked();
    void onCaptureProcessed();
    void onFinishClicked();

private:
    void processFrame(const QImage& frame);
    void updateProgress();

    VideoWidget* preview_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QLabel* feedbackLabel_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QPushButton* captureButton_ = nullptr;
    QCheckBox* autoCheck_ = nullptr;
    QPushButton* finishButton_ = nullptr;
    QTimer autoTimer_;

    camera::CameraController* controller_ = nullptr;
    repositories::PieceRepository* pieces_ = nullptr;
    std::unique_ptr<engine::RegistrationSession> session_;
    QFutureWatcher<core::Result<engine::RegistrationSession::SampleFeedback>> watcher_;
    QImage lastFrame_;
    std::int64_t createdPieceId_ = -1;
    // >= 0 cuando se esta registrando un acabado de una pieza que ya existe.
    std::int64_t targetPieceId_ = -1;
    QString savedVariant_;
    void buildUi(const QString& fixedPieceName);
};

}  // namespace pci::ui
