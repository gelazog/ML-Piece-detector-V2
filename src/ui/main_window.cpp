#include "ui/main_window.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "camera/camera_enumerator.h"
#include "camera/frame_utils.h"
#include "core/logging.h"
#include "inspection_editor/editor_window.h"
#include "repositories/piece_repository.h"
#include "repositories/settings_repository.h"
#include "ui/video_widget.h"
#include "vision/pipeline.h"

namespace pci::ui {

namespace {

// Corre en un hilo del pool de QtConcurrent; solo toca datos propios.
AnalysisOverlay buildOverlay(const QImage& frame) {
    AnalysisOverlay overlay;
    overlay.frameSize = frame.size();

    const auto analysis = vision::analyzeFrame(camera::qImageToMat(frame));
    if (!analysis.isOk()) {
        overlay.error = QString::fromStdString(analysis.error().message);
        return overlay;
    }

    overlay.valid = true;
    overlay.contour.reserve(static_cast<qsizetype>(analysis.value().contour.points.size()));
    for (const cv::Point& p : analysis.value().contour.points) {
        overlay.contour << QPointF(p.x, p.y);
    }
    overlay.centroid = QPointF(analysis.value().fixture.origin.x,
                               analysis.value().fixture.origin.y);
    overlay.angleDeg = analysis.value().fixture.angleDeg;
    return overlay;
}

}  // namespace

namespace {
const char* const kSettingCameraIndex = "camera_index";
}

MainWindow::MainWindow(AppRepositories repositories, QWidget* parent)
    : QMainWindow(parent), repos_(repositories) {
    setWindowTitle(tr("PC Inspector — Demo de inspección visual"));
    resize(900, 600);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* controlsLayout = new QHBoxLayout();
    controlsLayout->addWidget(new QLabel(tr("Cámara:"), central));

    cameraCombo_ = new QComboBox(central);
    cameraCombo_->setMinimumWidth(200);
    controlsLayout->addWidget(cameraCombo_, 1);

    refreshButton_ = new QPushButton(tr("Actualizar"), central);
    controlsLayout->addWidget(refreshButton_);

    startStopButton_ = new QPushButton(tr("Iniciar"), central);
    controlsLayout->addWidget(startStopButton_);

    analysisCheck_ = new QCheckBox(tr("Mostrar análisis"), central);
    controlsLayout->addWidget(analysisCheck_);

    editorButton_ = new QPushButton(tr("Plantilla…"), central);
    editorButton_->setToolTip(
        tr("Editor de herramientas de medición (usa el último frame o una imagen)"));
    controlsLayout->addWidget(editorButton_);

    rootLayout->addLayout(controlsLayout);

    video_ = new VideoWidget(central);
    rootLayout->addWidget(video_, 1);

    setCentralWidget(central);

    statsLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(statsLabel_);

    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::refreshCameras);
    connect(startStopButton_, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    connect(editorButton_, &QPushButton::clicked, this, &MainWindow::onOpenEditorClicked);
    connect(analysisCheck_, &QCheckBox::toggled, this, &MainWindow::onAnalysisToggled);
    connect(&enumerationWatcher_, &QFutureWatcher<std::vector<camera::CameraInfo>>::finished,
            this, &MainWindow::onCamerasEnumerated);
    connect(&analysisWatcher_, &QFutureWatcher<AnalysisOverlay>::finished, this,
            &MainWindow::onAnalysisFinished);

    connect(&controller_, &camera::CameraController::frameReady, this, &MainWindow::onFrame);
    connect(&controller_, &camera::CameraController::statsUpdated, this, &MainWindow::onStats);
    connect(&controller_, &camera::CameraController::cameraError, this,
            &MainWindow::onCameraError);
    connect(&controller_, &camera::CameraController::stopped, this, &MainWindow::onStreamStopped);

    refreshCameras();
}

MainWindow::~MainWindow() {
    controller_.stop();
    enumerationWatcher_.waitForFinished();
    analysisWatcher_.waitForFinished();
}

void MainWindow::refreshCameras() {
    if (enumerationWatcher_.isRunning()) {
        return;
    }
    setControlsEnabled(false);
    cameraCombo_->clear();
    cameraCombo_->addItem(tr("Buscando cámaras…"));
    statusBar()->showMessage(tr("Buscando cámaras conectadas…"));

    enumerationWatcher_.setFuture(
        QtConcurrent::run([] { return camera::CameraEnumerator::enumerate(); }));
}

void MainWindow::onCamerasEnumerated() {
    cameras_ = enumerationWatcher_.result();
    cameraCombo_->clear();

    if (cameras_.empty()) {
        cameraCombo_->addItem(tr("No se encontraron cámaras"));
        statusBar()->showMessage(tr("No se detectó ninguna cámara. Conecta una y actualiza."));
        refreshButton_->setEnabled(true);
        startStopButton_->setEnabled(false);
        return;
    }

    for (const auto& cam : cameras_) {
        cameraCombo_->addItem(QString::fromStdString(cam.name) +
                              QStringLiteral(" (%1x%2)").arg(cam.width).arg(cam.height));
    }

    // Restaurar la última cámara elegida por el usuario (si sigue conectada).
    if (repos_.settings != nullptr) {
        const auto saved = repos_.settings->getInt(kSettingCameraIndex, -1);
        if (saved.isOk() && saved.value() >= 0) {
            for (std::size_t i = 0; i < cameras_.size(); ++i) {
                if (cameras_[i].index == saved.value()) {
                    cameraCombo_->setCurrentIndex(static_cast<int>(i));
                    break;
                }
            }
        }
    }

    statusBar()->showMessage(tr("%n cámara(s) detectada(s)", nullptr,
                                static_cast<int>(cameras_.size())));
    setControlsEnabled(true);
}

void MainWindow::onStartStopClicked() {
    if (streaming_) {
        // stop() une el hilo de captura; la UI se restablece en onStreamStopped.
        startStopButton_->setEnabled(false);
        controller_.stop();
        return;
    }

    const int comboIndex = cameraCombo_->currentIndex();
    if (comboIndex < 0 || comboIndex >= static_cast<int>(cameras_.size())) {
        return;
    }

    if (repos_.settings != nullptr) {
        if (auto saved =
                repos_.settings->setInt(kSettingCameraIndex, cameras_[comboIndex].index);
            !saved.isOk()) {
            core::logWarning("No se pudo guardar la cámara elegida: " + saved.error().message);
        }
    }

    streaming_ = true;
    startStopButton_->setText(tr("Detener"));
    cameraCombo_->setEnabled(false);
    refreshButton_->setEnabled(false);
    statusBar()->showMessage(tr("Transmitiendo desde %1")
                                 .arg(QString::fromStdString(cameras_[comboIndex].name)));
    controller_.start(cameras_[comboIndex].index);
}

void MainWindow::onFrame(const QImage& frame) {
    video_->setFrame(frame);
    lastFrame_ = frame;
    if (streaming_ && analysisCheck_->isChecked()) {
        pendingAnalysisFrame_ = frame;
        maybeStartAnalysis();
    }
}

void MainWindow::onStats(double fps, int width, int height) {
    statsLabel_->setText(QStringLiteral("%1x%2 — %3 fps")
                             .arg(width)
                             .arg(height)
                             .arg(fps, 0, 'f', 1));
}

void MainWindow::onCameraError(const QString& message) {
    core::logError("Error de cámara: " + message.toStdString());
    statusBar()->showMessage(tr("Error: %1").arg(message));
}

void MainWindow::onStreamStopped() {
    streaming_ = false;
    startStopButton_->setText(tr("Iniciar"));
    startStopButton_->setEnabled(!cameras_.empty());
    cameraCombo_->setEnabled(true);
    refreshButton_->setEnabled(true);
    statsLabel_->clear();
    pendingAnalysisFrame_ = QImage();
    video_->clear();
}

void MainWindow::onAnalysisToggled(bool enabled) {
    if (!enabled) {
        pendingAnalysisFrame_ = QImage();
        video_->clearOverlay();
    }
}

void MainWindow::onAnalysisFinished() {
    if (streaming_ && analysisCheck_->isChecked()) {
        video_->setOverlay(analysisWatcher_.result());
        maybeStartAnalysis();
    }
}

// Como máximo un análisis en vuelo; si la visión va más lenta que la cámara,
// se procesan solo los frames más recientes (se descartan los intermedios).
void MainWindow::maybeStartAnalysis() {
    if (analysisWatcher_.isRunning() || pendingAnalysisFrame_.isNull()) {
        return;
    }
    const QImage frame = pendingAnalysisFrame_;
    pendingAnalysisFrame_ = QImage();
    analysisWatcher_.setFuture(QtConcurrent::run(buildOverlay, frame));
}

// Abre el editor de plantilla con el último frame de la cámara o, si no hay,
// con una imagen elegida por el usuario (permite probar el editor sin cámara).
void MainWindow::onOpenEditorClicked() {
    QImage reference = lastFrame_;
    if (reference.isNull()) {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Elegir imagen de referencia"), QString(),
            tr("Imágenes (*.png *.jpg *.jpeg *.bmp)"));
        if (path.isEmpty()) {
            return;
        }
        reference = QImage(path);
        if (reference.isNull()) {
            QMessageBox::warning(this, tr("Imagen inválida"),
                                 tr("No se pudo cargar la imagen."));
            return;
        }
        reference = reference.convertToFormat(QImage::Format_BGR888);
    }

    const auto analysis = vision::analyzeFrame(camera::qImageToMat(reference));
    if (!analysis.isOk()) {
        QMessageBox::warning(this, tr("Sin pieza detectada"),
                             tr("No se pudo analizar la imagen: %1")
                                 .arg(QString::fromStdString(analysis.error().message)));
        return;
    }

    // Pieza "demo" hasta que la fase 6 traiga el registro completo de piezas.
    std::int64_t pieceId = -1;
    if (repos_.pieces != nullptr) {
        if (auto created = repos_.pieces->createPiece("demo"); created.isOk()) {
            pieceId = created.value();
        } else if (auto pieces = repos_.pieces->listPieces(); pieces.isOk()) {
            for (const auto& piece : pieces.value()) {
                if (piece.name == "demo") {
                    pieceId = piece.id;
                    break;
                }
            }
        }
    }

    inspection::EditorWindow editor(reference, analysis.value().fixture, pieceId,
                                    pieceId >= 0 ? repos_.tools : nullptr, this);
    editor.exec();
}

void MainWindow::setControlsEnabled(bool enabled) {
    cameraCombo_->setEnabled(enabled);
    refreshButton_->setEnabled(enabled);
    startStopButton_->setEnabled(enabled && !cameras_.empty());
}

}  // namespace pci::ui
