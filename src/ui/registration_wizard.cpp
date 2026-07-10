#include "ui/registration_wizard.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "camera/camera_controller.h"
#include "camera/frame_utils.h"
#include "core/logging.h"
#include "engine/inspection_engine.h"
#include "repositories/piece_repository.h"
#include "ui/video_widget.h"

namespace pci::ui {

namespace {
constexpr int kTargetCaptures = 30;
constexpr int kMinimumCaptures = 5;
}  // namespace

RegistrationWizard::RegistrationWizard(camera::CameraController* controller,
                                       engine::EmbedFn embedFn,
                                       repositories::PieceRepository* pieces,
                                       QWidget* parent)
    : QDialog(parent), controller_(controller), pieces_(pieces),
      session_(std::make_unique<engine::RegistrationSession>(std::move(embedFn),
                                                             kTargetCaptures,
                                                             kMinimumCaptures)) {
    setWindowTitle(tr("Registrar pieza nueva"));
    resize(900, 640);

    auto* rootLayout = new QVBoxLayout(this);

    auto* nameLayout = new QHBoxLayout();
    nameLayout->addWidget(new QLabel(tr("Nombre de la pieza:"), this));
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setPlaceholderText(tr("p. ej. Engranaje A"));
    nameLayout->addWidget(nameEdit_, 1);
    rootLayout->addLayout(nameLayout);

    preview_ = new VideoWidget(this);
    rootLayout->addWidget(preview_, 1);

    progressLabel_ = new QLabel(this);
    rootLayout->addWidget(progressLabel_);
    feedbackLabel_ = new QLabel(tr("Coloca la pieza completa sobre fondo contrastante."), this);
    feedbackLabel_->setWordWrap(true);
    rootLayout->addWidget(feedbackLabel_);

    auto* buttonsLayout = new QHBoxLayout();
    captureButton_ = new QPushButton(tr("Capturar"), this);
    buttonsLayout->addWidget(captureButton_);
    autoCheck_ = new QCheckBox(tr("Captura automática"), this);
    buttonsLayout->addWidget(autoCheck_);
    auto* filesButton = new QPushButton(tr("Agregar imágenes…"), this);
    buttonsLayout->addWidget(filesButton);
    buttonsLayout->addStretch(1);
    finishButton_ = new QPushButton(tr("Finalizar registro"), this);
    finishButton_->setEnabled(false);
    buttonsLayout->addWidget(finishButton_);
    rootLayout->addLayout(buttonsLayout);

    if (controller_ != nullptr) {
        connect(controller_, &camera::CameraController::frameReady, this,
                &RegistrationWizard::onFrame);
    }
    connect(captureButton_, &QPushButton::clicked, this,
            &RegistrationWizard::onCaptureClicked);
    connect(autoCheck_, &QCheckBox::toggled, this, &RegistrationWizard::onAutoToggled);
    connect(&autoTimer_, &QTimer::timeout, this, &RegistrationWizard::onAutoTick);
    connect(filesButton, &QPushButton::clicked, this, &RegistrationWizard::onFilesClicked);
    connect(&watcher_, &QFutureWatcher<
                           core::Result<engine::RegistrationSession::SampleFeedback>>::finished,
            this, &RegistrationWizard::onCaptureProcessed);
    connect(finishButton_, &QPushButton::clicked, this, &RegistrationWizard::onFinishClicked);

    autoTimer_.setInterval(600);
    updateProgress();
}

RegistrationWizard::~RegistrationWizard() {
    autoTimer_.stop();
    watcher_.waitForFinished();
}

void RegistrationWizard::onFrame(const QImage& frame) {
    lastFrame_ = frame;
    preview_->setFrame(frame);
}

void RegistrationWizard::onCaptureClicked() {
    if (lastFrame_.isNull()) {
        feedbackLabel_->setText(
            tr("No hay video en vivo: inicia la cámara o usa \"Agregar imágenes…\"."));
        return;
    }
    processFrame(lastFrame_);
}

void RegistrationWizard::onAutoToggled(bool enabled) {
    if (enabled) {
        autoTimer_.start();
    } else {
        autoTimer_.stop();
    }
}

void RegistrationWizard::onAutoTick() {
    if (!lastFrame_.isNull() && !watcher_.isRunning() &&
        session_->count() < session_->target()) {
        processFrame(lastFrame_);
    }
}

// La validación + embedding tardan; corren en el pool con un solo vuelo.
void RegistrationWizard::processFrame(const QImage& frame) {
    if (watcher_.isRunning()) {
        return;
    }
    captureButton_->setEnabled(false);
    watcher_.setFuture(QtConcurrent::run([this, frame] {
        return session_->addFrame(camera::qImageToMat(frame));
    }));
}

void RegistrationWizard::onCaptureProcessed() {
    captureButton_->setEnabled(true);
    const auto result = watcher_.result();
    if (!result.isOk()) {
        feedbackLabel_->setStyleSheet(QStringLiteral("color: #ff5555;"));
        feedbackLabel_->setText(QString::fromStdString(result.error().message));
        return;
    }
    const auto& feedback = result.value();
    if (feedback.accepted) {
        feedbackLabel_->setStyleSheet(QStringLiteral("color: #22cc44;"));
        feedbackLabel_->setText(tr("Captura %1 aceptada").arg(feedback.count));
    } else {
        feedbackLabel_->setStyleSheet(QStringLiteral("color: #ff9944;"));
        feedbackLabel_->setText(tr("Rechazada: %1")
                                    .arg(QString::fromStdString(feedback.reason)));
    }
    updateProgress();

    if (session_->count() >= session_->target()) {
        autoCheck_->setChecked(false);
    }
}

void RegistrationWizard::onFilesClicked() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Elegir imágenes de la pieza"), QString(),
        tr("Imágenes (*.png *.jpg *.jpeg *.bmp)"));
    if (paths.isEmpty()) {
        return;
    }

    int accepted = 0;
    QString lastReason;
    for (const QString& path : paths) {
        QImage image(path);
        if (image.isNull()) {
            lastReason = tr("no se pudo cargar %1").arg(path);
            continue;
        }
        const auto result =
            session_->addFrame(camera::qImageToMat(image.convertToFormat(
                QImage::Format_BGR888)));
        if (!result.isOk()) {
            lastReason = QString::fromStdString(result.error().message);
            continue;
        }
        if (result.value().accepted) {
            ++accepted;
        } else {
            lastReason = QString::fromStdString(result.value().reason);
        }
    }

    feedbackLabel_->setStyleSheet(QString());
    feedbackLabel_->setText(lastReason.isEmpty()
                                ? tr("%1 imagen(es) aceptadas").arg(accepted)
                                : tr("%1 aceptadas — último rechazo: %2")
                                      .arg(accepted)
                                      .arg(lastReason));
    updateProgress();
}

void RegistrationWizard::updateProgress() {
    progressLabel_->setText(tr("Capturas válidas: %1 / %2 (mínimo %3)")
                                .arg(session_->count())
                                .arg(session_->target())
                                .arg(session_->minimum()));
    finishButton_->setEnabled(session_->readyToFinish() && pieces_ != nullptr);
}

void RegistrationWizard::onFinishClicked() {
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Falta el nombre"),
                             tr("Ponle un nombre a la pieza antes de finalizar."));
        return;
    }

    if (session_->count() < session_->target()) {
        const auto answer = QMessageBox::question(
            this, tr("Pocas capturas"),
            tr("Tienes %1 capturas (recomendado: %2). ¿Guardar igualmente como "
               "referencia de prueba?")
                .arg(session_->count())
                .arg(session_->target()));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    auto reference = session_->finish();
    if (!reference.isOk()) {
        QMessageBox::warning(this, tr("Registro incompleto"),
                             QString::fromStdString(reference.error().message));
        return;
    }

    auto pieceId = pieces_->createPiece(name.toStdString());
    if (!pieceId.isOk()) {
        // Mensaje ya amigable ("Ya existe una pieza llamada…"): dejar al
        // usuario corregir el nombre sin perder las capturas.
        QMessageBox::warning(this, tr("No se pudo crear la pieza"),
                             QString::fromStdString(pieceId.error().message));
        nameEdit_->setFocus();
        nameEdit_->selectAll();
        return;
    }
    auto saved = pieces_->saveReference(pieceId.value(), reference.value());
    if (!saved.isOk()) {
        QMessageBox::warning(this, tr("No se pudo guardar la referencia"),
                             QString::fromStdString(saved.error().message));
        return;
    }
    const auto thumbnail = engine::encodeThumbnailJpeg(session_->firstNormalized(), 256);
    if (!thumbnail.empty()) {
        if (auto savedThumb = pieces_->saveThumbnail(pieceId.value(), thumbnail);
            !savedThumb.isOk()) {
            pci::core::logWarning("No se pudo guardar la miniatura: " +
                                  savedThumb.error().message);
        }
    }

    createdPieceId_ = pieceId.value();
    QMessageBox::information(
        this, tr("Pieza registrada"),
        tr("'%1' registrada con %2 capturas (referencia v%3).")
            .arg(name)
            .arg(session_->count())
            .arg(saved.value()));
    accept();
}

}  // namespace pci::ui
