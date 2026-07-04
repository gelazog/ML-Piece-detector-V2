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
    RegistrationWizard(camera::CameraController* controller, engine::EmbedFn embedFn,
                       repositories::PieceRepository* pieces, QWidget* parent = nullptr);
    ~RegistrationWizard() override;

    [[nodiscard]] std::int64_t createdPieceId() const { return createdPieceId_; }

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
};

}  // namespace pci::ui
