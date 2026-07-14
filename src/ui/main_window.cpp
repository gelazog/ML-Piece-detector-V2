#include "ui/main_window.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <functional>
#include <type_traits>
#include <variant>

#include "camera/camera_enumerator.h"
#include "camera/frame_utils.h"
#include "core/logging.h"
#include "inspection_editor/editor_window.h"
#include "repositories/inspection_repository.h"
#include "repositories/piece_repository.h"
#include "repositories/settings_repository.h"
#include "inspection_editor/canvas/tool_icons.h"
#include "repositories/tool_repository.h"
#include "ui/calibration_dialog.h"
#include "ui/detection_dialog.h"
#include "ui/inspection_result_dialog.h"
#include "ui/piece_manager_dialog.h"
#include "ui/registration_wizard.h"
#include "vision/fixture_stabilizer.h"
#include "vision/pipeline.h"
#include "vision/position_fixture.h"

namespace pci::ui {

namespace {

const char* const kSettingCameraIndex = "camera_index";
constexpr int kCaptureTarget = 30;
constexpr int kCaptureMinimum = 5;

// Corre en un hilo del pool de QtConcurrent; solo toca datos propios.
// El ancla (si la pieza tiene rasgo distintivo) fija la orientación del
// fixture aunque la pieza sea simétrica o llegue girada 180°. Las
// herramientas se miden sobre cada frame: las medidas salen en vivo.
AnalysisOverlay buildOverlay(const QImage& frame,
                             std::optional<vision::OrientationAnchor> anchor,
                             double orientationOffsetDeg,
                             const std::vector<inspection::ToolConfig>& tools,
                             const vision::PipelineConfig& pipeline,
                             std::optional<vision::Fixture> previousFixture,
                             double mmPerPixel, inspection::LengthUnit unit) {
    AnalysisOverlay overlay;
    overlay.frameSize = frame.size();

    // Blindaje total: una excepción que escape de un worker de QtConcurrent
    // se relanza en result() y tumbaría la aplicación.
    try {
        const cv::Mat image = camera::qImageToMat(frame);
        auto analysis = vision::analyzeFrame(image, pipeline);
        if (!analysis.isOk()) {
            overlay.error = QString::fromStdString(analysis.error().message);
            return overlay;
        }
        if (anchor.has_value()) {
            if (auto applied = vision::applyAnchor(image, *anchor, analysis.value());
                !applied.isOk()) {
                core::logWarning(applied.error().message);
            }
        }
        if (auto applied = vision::applyOrientationOffset(image, orientationOffsetDeg,
                                                          analysis.value());
            !applied.isOk()) {
            core::logWarning(applied.error().message);
        }

        // Estabilización temporal: quieto = clavado, movimiento real =
        // seguimiento suave, y continuidad anti-giro de 180° cuando la pieza
        // no tiene rasgo distintivo. Trazos, medidas y overlay comparten el
        // mismo fixture estabilizado.
        if (previousFixture.has_value()) {
            vision::StabilizerOptions stabilizer;
            stabilizer.resolveFlips = !anchor.has_value();
            bool flipped180 = false;
            const vision::Fixture stable = vision::stabilizeFixture(
                *previousFixture, analysis.value().fixture, stabilizer, flipped180);
            if (flipped180) {
                if (auto applied =
                        vision::applyOrientationOffset(image, 180.0, analysis.value());
                    !applied.isOk()) {
                    core::logWarning(applied.error().message);
                }
            }
            analysis.value().fixture = stable;
        }

        overlay.valid = true;
        overlay.contour.reserve(
            static_cast<qsizetype>(analysis.value().contour.points.size()));
        for (const cv::Point& p : analysis.value().contour.points) {
            overlay.contour << QPointF(p.x, p.y);
        }
        overlay.centroid = QPointF(analysis.value().fixture.origin.x,
                                   analysis.value().fixture.origin.y);
        overlay.angleDeg = analysis.value().fixture.angleDeg;
        overlay.normalized = camera::matToQImage(analysis.value().normalized);
        if (!tools.empty()) {
            overlay.toolResults =
                inspection::runTools(image, analysis.value().fixture, tools, mmPerPixel, unit);
        }
    } catch (const std::exception& e) {
        overlay.valid = false;
        overlay.error = QStringLiteral("Error interno de análisis");
        core::logError(std::string("Excepción en análisis en vivo: ") + e.what());
    } catch (...) {
        overlay.valid = false;
        overlay.error = QStringLiteral("Error interno de análisis");
        core::logError("Excepción desconocida en análisis en vivo");
    }
    return overlay;
}

QString toolTypeLabel(inspection::ToolType type) {
    switch (type) {
        case inspection::ToolType::Caliper: return QStringLiteral("Caliper");
        case inspection::ToolType::Circle: return QStringLiteral("Círculo");
        case inspection::ToolType::PointToLine: return QStringLiteral("Punto-Línea");
        case inspection::ToolType::EdgeFlaw: return QStringLiteral("Borde liso");
        case inspection::ToolType::Blob: return QStringLiteral("Blob");
        case inspection::ToolType::Ruler: return QStringLiteral("Regla");
    }
    return QStringLiteral("?");
}

double wrapAngleDeg(double angle) {
    while (angle >= 180.0) {
        angle -= 360.0;
    }
    while (angle < -180.0) {
        angle += 360.0;
    }
    return angle;
}

}  // namespace

MainWindow::MainWindow(AppRepositories repositories, QWidget* parent)
    : QMainWindow(parent), repos_(repositories) {
    setWindowTitle(tr("PC Inspector — Demo de inspección visual"));
    resize(1100, 760);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    // --- Fila 1: cámara ---
    auto* cameraLayout = new QHBoxLayout();
    cameraLayout->addWidget(new QLabel(tr("Cámara:"), central));
    cameraCombo_ = new QComboBox(central);
    cameraCombo_->setMinimumWidth(200);
    cameraLayout->addWidget(cameraCombo_, 1);
    refreshButton_ = new QPushButton(tr("Actualizar"), central);
    cameraLayout->addWidget(refreshButton_);
    startStopButton_ = new QPushButton(tr("Iniciar"), central);
    cameraLayout->addWidget(startStopButton_);
    calibrateButton_ = new QPushButton(tr("Calibrar mm…"), central);
    calibrateButton_->setToolTip(
        tr("Calibra la escala px → mm: dos clics sobre una distancia conocida, o\n"
           "la distancia de la cámara a la superficie + su FOV. Con la escala\n"
           "calibrada todas las medidas se muestran también en milímetros."));
    cameraLayout->addWidget(calibrateButton_);

    detectionButton_ = new QPushButton(tr("Detección…"), central);
    detectionButton_->setToolTip(
        tr("Ajustes del contorno automático contra luces y sombras: umbral manual\n"
           "u Otsu, polaridad de la pieza, suavizado y limpieza morfológica."));
    cameraLayout->addWidget(detectionButton_);

    roiButton_ = new QPushButton(tr("Zona de detección"), central);
    roiButton_->setCheckable(true);
    roiButton_->setIcon(inspection::regionIcon());
    roiButton_->setToolTip(
        tr("Enfoca la detección en un solo lugar: arrastra un rectángulo sobre el\n"
           "video y el contorno automático solo se buscará ahí — las sombras y\n"
           "objetos fuera de la zona dejan de estorbar. Vuelve a pulsarlo para\n"
           "quitar la zona."));
    cameraLayout->addWidget(roiButton_);

    cameraLayout->addWidget(new QLabel(tr("Unidad:"), central));
    unitCombo_ = new QComboBox(central);
    unitCombo_->addItem(tr("Auto"));  // orden = LengthUnit
    unitCombo_->addItem(tr("mm"));
    unitCombo_->addItem(tr("cm"));
    unitCombo_->addItem(tr("px"));
    unitCombo_->setToolTip(
        tr("Unidad de las medidas. Auto: mm y cambia a cm a partir de 10 cm.\n"
           "Requiere calibración (Calibrar mm…) para mostrar mm/cm."));
    cameraLayout->addWidget(unitCombo_);

    analysisCheck_ = new QCheckBox(tr("Detectar pieza (contorno)"), central);
    analysisCheck_->setChecked(true);
    cameraLayout->addWidget(analysisCheck_);
    rootLayout->addLayout(cameraLayout);

    // --- Fila 2: pieza y flujo ---
    auto* pieceLayout = new QHBoxLayout();
    pieceLayout->addWidget(new QLabel(tr("Pieza:"), central));
    pieceCombo_ = new QComboBox(central);
    pieceCombo_->setMinimumWidth(140);
    pieceLayout->addWidget(pieceCombo_, 1);

    pieceLayout->addWidget(new QLabel(tr("Plantilla:"), central));
    templateCombo_ = new QComboBox(central);
    templateCombo_->setMinimumWidth(110);
    templateCombo_->setToolTip(
        tr("Una pieza puede tener varias plantillas de herramientas; se inspecciona "
           "con la activa."));
    pieceLayout->addWidget(templateCombo_);
    newTemplateButton_ = new QPushButton(tr("+"), central);
    newTemplateButton_->setToolTip(tr("Crear una plantilla nueva para esta pieza"));
    newTemplateButton_->setMaximumWidth(32);
    pieceLayout->addWidget(newTemplateButton_);

    registerLiveButton_ = new QPushButton(tr("Registrar y activar"), central);
    registerLiveButton_->setToolTip(
        tr("Captura automáticamente %1 referencias de la pieza en el video, guarda las "
           "herramientas dibujadas y arranca la auto-inspección")
            .arg(kCaptureTarget));
    pieceLayout->addWidget(registerLiveButton_);

    autoInspectButton_ = new QPushButton(tr("Auto-inspección"), central);
    autoInspectButton_->setCheckable(true);
    autoInspectButton_->setToolTip(
        tr("Inspecciona continuamente el video contra la pieza seleccionada"));
    pieceLayout->addWidget(autoInspectButton_);

    managePiecesButton_ = new QPushButton(tr("Piezas…"), central);
    managePiecesButton_->setToolTip(
        tr("Gestión de piezas: renombrar, eliminar y ajustar su orientación"));
    pieceLayout->addWidget(managePiecesButton_);

    registerWizardButton_ = new QPushButton(tr("Registrar (asistente)…"), central);
    registerWizardButton_->setToolTip(tr("Registro paso a paso; admite imágenes de archivo"));
    pieceLayout->addWidget(registerWizardButton_);

    editorButton_ = new QPushButton(tr("Plantilla…"), central);
    editorButton_->setToolTip(tr("Editor sobre imagen fija: ajustar tolerancias y geometrías"));
    pieceLayout->addWidget(editorButton_);

    inspectButton_ = new QPushButton(tr("Inspeccionar"), central);
    inspectButton_->setToolTip(tr("Inspección única con reporte detallado"));
    pieceLayout->addWidget(inspectButton_);
    rootLayout->addLayout(pieceLayout);

    // --- Fila 3: herramientas para dibujar sobre el video en vivo ---
    auto* toolsLayout = new QHBoxLayout();
    toolsLayout->addWidget(new QLabel(tr("Dibujar:"), central));
    toolModeGroup_ = new QButtonGroup(this);
    toolModeGroup_->setExclusive(true);
    auto addMode = [this, central, toolsLayout](const QString& text, int id) {
        auto* button = new QToolButton(central);
        button->setText(text);
        button->setCheckable(true);
        toolModeGroup_->addButton(button, id);
        toolsLayout->addWidget(button);
        return button;
    };
    auto* selectMode = addMode(tr("Mover/Elegir"), -1);
    selectMode->setChecked(true);
    selectMode->setIcon(inspection::moveModeIcon());
    selectMode->setToolButtonStyle(Qt::ToolButtonIconOnly);
    selectMode->setIconSize(QSize(24, 24));
    selectMode->setToolTip(
        tr("Mover/Elegir — clic para seleccionar; arrastra para mover; arrastra en\n"
           "vacío para un marco de selección múltiple."));
    for (const auto type :
         {inspection::ToolType::Caliper, inspection::ToolType::Circle,
          inspection::ToolType::PointToLine, inspection::ToolType::EdgeFlaw,
          inspection::ToolType::Blob, inspection::ToolType::Ruler}) {
        auto* button = addMode(toolTypeLabel(type), static_cast<int>(type));
        button->setIcon(inspection::toolIcon(type));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(24, 24));
        button->setToolTip(
            QString::fromUtf8(inspection::toolTypeDescription(type)) +
            tr("\n\nAl dibujarla se mide la pieza actual y las tolerancias se "
               "sugieren solas."));
    }
    deleteToolButton_ = new QPushButton(tr("Borrar herramienta"), central);
    deleteToolButton_->setToolTip(tr("Elimina la herramienta seleccionada (Mover/Elegir)"));
    toolsLayout->addWidget(deleteToolButton_);

    anchorButton_ = new QPushButton(tr("Rasgo distintivo"), central);
    anchorButton_->setIcon(inspection::anchorIcon());
    anchorButton_->setCheckable(true);
    anchorButton_->setToolTip(
        tr("Marca un punto visualmente único de la pieza (un agujero, una marca, una\n"
           "esquina oscura). Con él la orientación queda fija aunque la pieza sea\n"
           "simétrica: se detecta igual en cualquier rotación, incluso girada 180°."));
    toolsLayout->addWidget(anchorButton_);

    // Parámetro de muestreo de la herramienta seleccionada, sin abrir el
    // editor: banda del Caliper, rayos del Círculo, escaneos del Borde, área
    // mínima del Blob.
    liveParamLabel_ = new QLabel(tr("Puntos:"), central);
    toolsLayout->addWidget(liveParamLabel_);
    liveParamSpin_ = new QSpinBox(central);
    liveParamSpin_->setRange(1, 1000);
    liveParamSpin_->setEnabled(false);
    liveParamSpin_->setToolTip(
        tr("Cantidad de puntos de muestreo de la herramienta seleccionada:\n"
           "Caliper: grosor de banda (px) · Círculo: rayos · Borde liso: escaneos\n"
           "· Blob: área mínima (px²)"));
    toolsLayout->addWidget(liveParamSpin_);

    toolsLayout->addStretch(1);
    auto* shortcutsButton = new QPushButton(tr("Atajos (F1)"), central);
    shortcutsButton->setToolTip(tr("Guía de atajos de teclado — también puedes cambiarlos"));
    connect(shortcutsButton, &QPushButton::clicked, this, &MainWindow::onShowShortcuts);
    toolsLayout->addWidget(shortcutsButton);
    rootLayout->addLayout(toolsLayout);

    // Banner de veredicto para la auto-inspección.
    verdictBanner_ = new QLabel(central);
    verdictBanner_->setAlignment(Qt::AlignCenter);
    verdictBanner_->setMinimumHeight(36);
    verdictBanner_->setVisible(false);
    rootLayout->addWidget(verdictBanner_);

    // Video (canvas de edición) + panel de comparación registrada vs actual.
    auto* viewLayout = new QHBoxLayout();
    video_ = new inspection::EditorCanvas(central);
    video_->setTools(&liveTools_);
    viewLayout->addWidget(video_, 1);

    auto* compareLayout = new QVBoxLayout();
    auto makeThumb = [central]() {
        auto* label = new QLabel(central);
        label->setFixedSize(170, 170);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(
            QStringLiteral("background:#1a1a1a; color:#888; border:1px solid #444;"));
        label->setText(QStringLiteral("—"));
        return label;
    };
    compareLayout->addWidget(new QLabel(tr("Pieza registrada:"), central));
    refThumbLabel_ = makeThumb();
    compareLayout->addWidget(refThumbLabel_);
    compareLayout->addWidget(new QLabel(tr("Pieza actual:"), central));
    currentThumbLabel_ = makeThumb();
    compareLayout->addWidget(currentThumbLabel_);

    // Rotar la vista de la pieza a gusto del usuario (gira la orientación de
    // la pieza seleccionada; persiste y aplica en registro e inspección).
    auto* rotateLayout = new QHBoxLayout();
    auto* rotateLeft = new QPushButton(QStringLiteral("⟲ 90°"), central);
    auto* rotateRight = new QPushButton(QStringLiteral("⟳ 90°"), central);
    const QString rotateTip =
        tr("Gira cómo se ve la pieza (su recorte normalizado). Con una pieza\n"
           "seleccionada el giro se guarda con ella.");
    rotateLeft->setToolTip(rotateTip);
    rotateRight->setToolTip(rotateTip);
    rotateLayout->addWidget(rotateLeft);
    rotateLayout->addWidget(rotateRight);
    compareLayout->addLayout(rotateLayout);
    connect(rotateLeft, &QPushButton::clicked, this, [this] { rotatePieceView(-90.0); });
    connect(rotateRight, &QPushButton::clicked, this, [this] { rotatePieceView(90.0); });
    similarityLabel_ = new QLabel(central);
    similarityLabel_->setWordWrap(true);
    compareLayout->addWidget(similarityLabel_);
    compareLayout->addStretch(1);
    viewLayout->addLayout(compareLayout);
    rootLayout->addLayout(viewLayout, 1);

    setCentralWidget(central);

    calibLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(calibLabel_);
    statsLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(statsLabel_);

    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::refreshCameras);
    connect(startStopButton_, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    connect(analysisCheck_, &QCheckBox::toggled, video_,
            &inspection::EditorCanvas::setLiveContourVisible);
    connect(&enumerationWatcher_, &QFutureWatcher<std::vector<camera::CameraInfo>>::finished,
            this, &MainWindow::onCamerasEnumerated);
    connect(&analysisWatcher_, &QFutureWatcher<AnalysisOverlay>::finished, this,
            &MainWindow::onAnalysisFinished);

    connect(&controller_, &camera::CameraController::frameReady, this, &MainWindow::onFrame);
    connect(&controller_, &camera::CameraController::statsUpdated, this, &MainWindow::onStats);
    connect(&controller_, &camera::CameraController::cameraError, this,
            &MainWindow::onCameraError);
    connect(&controller_, &camera::CameraController::stopped, this, &MainWindow::onStreamStopped);

    connect(toolModeGroup_, &QButtonGroup::idClicked, this, &MainWindow::onToolModeChanged);
    connect(video_, &inspection::EditorCanvas::toolCreated, this,
            &MainWindow::onLiveToolCreated);
    connect(video_, &inspection::EditorCanvas::toolModified, this,
            &MainWindow::onLiveToolModified);
    connect(video_, &inspection::EditorCanvas::selectionChanged, this,
            &MainWindow::onLiveSelectionChanged);
    connect(liveParamSpin_, &QSpinBox::valueChanged, this, &MainWindow::onLiveParamChanged);
    connect(managePiecesButton_, &QPushButton::clicked, this,
            &MainWindow::onManagePiecesClicked);
    connect(deleteToolButton_, &QPushButton::clicked, this, &MainWindow::onDeleteToolClicked);
    connect(anchorButton_, &QPushButton::toggled, this, &MainWindow::onAnchorButtonToggled);
    connect(video_, &inspection::EditorCanvas::pointPicked, this,
            &MainWindow::onAnchorPicked);
    connect(pieceCombo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onPieceSelectionChanged);
    connect(templateCombo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onTemplateChanged);
    connect(newTemplateButton_, &QPushButton::clicked, this,
            &MainWindow::onNewTemplateClicked);
    connect(unitCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::onUnitChanged);
    connect(video_, &inspection::EditorCanvas::toolRightClicked, this,
            &MainWindow::onToolRightClicked);

    connect(registerLiveButton_, &QPushButton::clicked, this,
            &MainWindow::onRegisterLiveClicked);
    connect(&captureTimer_, &QTimer::timeout, this, &MainWindow::onCaptureTick);
    connect(&captureWatcher_,
            &QFutureWatcher<
                core::Result<engine::RegistrationSession::SampleFeedback>>::finished,
            this, &MainWindow::onCaptureProcessed);
    connect(autoInspectButton_, &QPushButton::toggled, this, &MainWindow::onAutoToggled);
    connect(&autoTimer_, &QTimer::timeout, this, &MainWindow::onAutoTick);

    connect(calibrateButton_, &QPushButton::clicked, this, &MainWindow::onCalibrateClicked);
    connect(detectionButton_, &QPushButton::clicked, this, &MainWindow::onDetectionClicked);
    connect(roiButton_, &QPushButton::toggled, this, &MainWindow::onRoiButtonToggled);
    connect(video_, &inspection::EditorCanvas::regionPicked, this,
            &MainWindow::onRegionPicked);
    connect(registerWizardButton_, &QPushButton::clicked, this,
            &MainWindow::onRegisterWizardClicked);
    connect(editorButton_, &QPushButton::clicked, this, &MainWindow::onOpenEditorClicked);
    connect(inspectButton_, &QPushButton::clicked, this, &MainWindow::onInspectClicked);
    connect(&inspectionWatcher_,
            &QFutureWatcher<core::Result<engine::InspectionEngine::Outcome>>::finished, this,
            &MainWindow::onInspectionFinished);

    captureTimer_.setInterval(350);
    autoTimer_.setInterval(1000);

    // Calibración de escala persistida.
    if (repos_.settings != nullptr) {
        calibration_.mmPerPixel =
            repos_.settings->getDouble("calib_mm_per_px", 0.0).value();
        calibration_.cameraDistanceMm =
            repos_.settings->getDouble("calib_camera_dist_mm", 0.0).value();
        calibration_.horizontalFovDeg =
            repos_.settings->getDouble("calib_fov_deg", 60.0).value();
    }
    updateCalibrationLabel();
    video_->setMmPerPixel(calibration_.mmPerPixel);

    // Ajustes de detección persistidos (umbral, polaridad, kernels y zona).
    if (repos_.settings != nullptr) {
        auto& seg = pipelineConfig_.segmentation;
        seg.manualThreshold = repos_.settings->getInt("det_threshold", -1).value();
        seg.polarity = static_cast<vision::SegmentationPolarity>(
            std::clamp(repos_.settings->getInt("det_polarity", 0).value(), 0, 2));
        seg.blurKernel = repos_.settings->getInt("det_blur", 5).value();
        seg.morphKernel = repos_.settings->getInt("det_morph", 5).value();
        pipelineConfig_.roi = cv::Rect(repos_.settings->getInt("det_roi_x", 0).value(),
                                       repos_.settings->getInt("det_roi_y", 0).value(),
                                       repos_.settings->getInt("det_roi_w", 0).value(),
                                       repos_.settings->getInt("det_roi_h", 0).value());
    }
    updateRoiButton();

    // Unidad de medida elegida por el operador (persistida).
    if (repos_.settings != nullptr) {
        const int unit = std::clamp(repos_.settings->getInt("length_unit", 0).value(), 0, 3);
        QSignalBlocker blocker(unitCombo_);
        unitCombo_->setCurrentIndex(unit);
    }
    video_->setLengthUnit(currentUnit());

    buildShortcuts();

    refreshCameras();
    loadPieceList();
}

inspection::LengthUnit MainWindow::currentUnit() const {
    return static_cast<inspection::LengthUnit>(unitCombo_->currentIndex());
}

std::string MainWindow::activeTemplate() const {
    const QString name = templateCombo_->currentText();
    return name.isEmpty() ? std::string("principal") : name.toStdString();
}

void MainWindow::onUnitChanged(int index) {
    if (repos_.settings != nullptr) {
        repos_.settings->setInt("length_unit", index);
    }
    video_->setLengthUnit(currentUnit());
}

void MainWindow::persistPipelineConfig() {
    if (repos_.settings == nullptr) {
        return;
    }
    const auto& seg = pipelineConfig_.segmentation;
    repos_.settings->setInt("det_threshold", seg.manualThreshold);
    repos_.settings->setInt("det_polarity", static_cast<int>(seg.polarity));
    repos_.settings->setInt("det_blur", seg.blurKernel);
    repos_.settings->setInt("det_morph", seg.morphKernel);
    repos_.settings->setInt("det_roi_x", pipelineConfig_.roi.x);
    repos_.settings->setInt("det_roi_y", pipelineConfig_.roi.y);
    repos_.settings->setInt("det_roi_w", pipelineConfig_.roi.width);
    repos_.settings->setInt("det_roi_h", pipelineConfig_.roi.height);
}

void MainWindow::updateRoiButton() {
    const bool hasRoi = pipelineConfig_.roi.area() > 0;
    video_->setDetectionRegion(hasRoi, pipelineConfig_.roi);
    QSignalBlocker blocker(roiButton_);
    roiButton_->setChecked(false);
    roiButton_->setText(hasRoi ? tr("Quitar zona") : tr("Zona de detección"));
}

void MainWindow::onDetectionClicked() {
    DetectionDialog dialog(pipelineConfig_.segmentation, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    pipelineConfig_.segmentation = dialog.options();
    persistPipelineConfig();
    statusBar()->showMessage(
        tr("Ajustes de detección aplicados: el contorno en vivo ya los usa."));
}

void MainWindow::onRoiButtonToggled(bool enabled) {
    if (!enabled) {
        video_->setRegionPickMode(false);
        return;
    }
    if (pipelineConfig_.roi.area() > 0) {
        // Segundo uso del botón: quitar la zona activa.
        pipelineConfig_.roi = cv::Rect();
        persistPipelineConfig();
        updateRoiButton();
        statusBar()->showMessage(tr("Zona de detección eliminada: se analiza todo el frame."));
        return;
    }
    if (lastFrame_.isNull()) {
        statusBar()->showMessage(tr("Inicia la cámara para dibujar la zona de detección."));
        QSignalBlocker blocker(roiButton_);
        roiButton_->setChecked(false);
        return;
    }
    video_->setRegionPickMode(true);
    statusBar()->showMessage(
        tr("Arrastra un rectángulo sobre el video: la detección se limitará a esa zona."));
}

void MainWindow::onRegionPicked(const cv::Rect& imageRect) {
    pipelineConfig_.roi = imageRect;
    persistPipelineConfig();
    updateRoiButton();
    statusBar()->showMessage(
        tr("Zona de detección activa: el contorno solo se busca dentro del recuadro."));
}

// Acciones con atajo configurable: el valor por defecto puede sobreescribirse
// desde la guía (F1) y persiste en Settings ("key_<id>").
void MainWindow::buildShortcuts() {
    auto addShortcut = [this](const QString& id, const QString& description,
                              const QKeySequence& defaultKey, auto slot) {
        auto* action = new QAction(description, this);
        QKeySequence key = defaultKey;
        if (repos_.settings != nullptr) {
            const auto saved =
                repos_.settings->getString(("key_" + id).toStdString(), std::string());
            if (saved.isOk() && !saved.value().empty()) {
                key = QKeySequence(QString::fromStdString(saved.value()));
            }
        }
        action->setShortcut(key);
        connect(action, &QAction::triggered, this, slot);
        addAction(action);
        shortcuts_.push_back({id, description, defaultKey, action});
    };

    addShortcut("undo", tr("Deshacer (herramientas dibujadas)"), QKeySequence::Undo,
                &MainWindow::onUndo);
    addShortcut("redo", tr("Rehacer"), QKeySequence::Redo, &MainWindow::onRedo);
    addShortcut("delete_tool", tr("Borrar la herramienta seleccionada"),
                QKeySequence(Qt::Key_Delete), &MainWindow::onDeleteToolClicked);
    addShortcut("select_mode", tr("Modo Mover/Elegir (cancela dibujo y rasgo)"),
                QKeySequence(Qt::Key_Escape), [this] {
                    anchorButton_->setChecked(false);
                    if (auto* button = toolModeGroup_->button(-1)) {
                        button->click();
                    }
                });

    const struct {
        const char* id;
        inspection::ToolType type;
        Qt::Key key;
    } toolKeys[] = {
        {"tool_caliper", inspection::ToolType::Caliper, Qt::Key_1},
        {"tool_circle", inspection::ToolType::Circle, Qt::Key_2},
        {"tool_point_line", inspection::ToolType::PointToLine, Qt::Key_3},
        {"tool_edge", inspection::ToolType::EdgeFlaw, Qt::Key_4},
        {"tool_blob", inspection::ToolType::Blob, Qt::Key_5},
    };
    for (const auto& entry : toolKeys) {
        const int id = static_cast<int>(entry.type);
        addShortcut(QString::fromLatin1(entry.id),
                    tr("Dibujar %1").arg(toolTypeLabel(entry.type)), QKeySequence(entry.key),
                    [this, id] {
                        if (auto* button = toolModeGroup_->button(id)) {
                            button->click();
                        }
                    });
    }

    addShortcut("camera_toggle", tr("Iniciar/Detener cámara"), QKeySequence(Qt::Key_V),
                [this] {
                    if (startStopButton_->isEnabled()) {
                        onStartStopClicked();
                    }
                });
    addShortcut("register_live", tr("Registrar y activar"), QKeySequence(Qt::Key_R),
                &MainWindow::onRegisterLiveClicked);
    addShortcut("auto_inspect", tr("Auto-inspección (alternar)"), QKeySequence(Qt::Key_A),
                [this] { autoInspectButton_->toggle(); });
    addShortcut("inspect_once", tr("Inspeccionar una vez"), QKeySequence(Qt::Key_I),
                &MainWindow::onInspectClicked);
    addShortcut("template_editor", tr("Abrir Plantilla…"), QKeySequence(Qt::Key_P),
                &MainWindow::onOpenEditorClicked);
    addShortcut("calibrate", tr("Calibrar mm…"), QKeySequence(Qt::Key_C),
                &MainWindow::onCalibrateClicked);
    addShortcut("anchor", tr("Marcar rasgo distintivo"), QKeySequence(Qt::Key_D),
                [this] { anchorButton_->toggle(); });
    addShortcut("shortcuts_help", tr("Guía de atajos"), QKeySequence(Qt::Key_F1),
                &MainWindow::onShowShortcuts);
}

void MainWindow::onShowShortcuts() {
    ShortcutsDialog dialog(&shortcuts_, repos_.settings, this);
    dialog.exec();
}

// --- Deshacer / rehacer sobre las herramientas dibujadas ---

void MainWindow::commitUndoState() {
    undoStack_.push(stableTools_);
    stableTools_ = liveTools_;
}

void MainWindow::restoreTools(std::vector<inspection::EditedTool> tools) {
    liveTools_ = std::move(tools);
    stableTools_ = liveTools_;
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
    video_->update();
}

void MainWindow::onUndo() {
    if (auto previous = undoStack_.undo(liveTools_)) {
        restoreTools(std::move(*previous));
        statusBar()->showMessage(tr("Deshecho."));
    }
}

void MainWindow::onRedo() {
    if (auto next = undoStack_.redo(liveTools_)) {
        restoreTools(std::move(*next));
        statusBar()->showMessage(tr("Rehecho."));
    }
}

void MainWindow::updateCalibrationLabel() {
    calibLabel_->setText(
        calibration_.valid()
            ? tr("Escala: %1 mm/px · cámara ~%2 mm")
                  .arg(calibration_.mmPerPixel, 0, 'f', 4)
                  .arg(calibration_.cameraDistanceMm, 0, 'f', 0)
            : tr("Sin calibrar (medidas en px)"));
}

void MainWindow::onCalibrateClicked() {
    const QImage snapshot = frameOrFile();
    if (snapshot.isNull()) {
        return;
    }
    CalibrationDialog dialog(snapshot, calibration_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    calibration_ = dialog.calibration();
    updateCalibrationLabel();
    video_->setMmPerPixel(calibration_.mmPerPixel);
    if (repos_.settings != nullptr) {
        repos_.settings->setDouble("calib_mm_per_px", calibration_.mmPerPixel);
        repos_.settings->setDouble("calib_camera_dist_mm", calibration_.cameraDistanceMm);
        repos_.settings->setDouble("calib_fov_deg", calibration_.horizontalFovDeg);
    }
    statusBar()->showMessage(
        tr("Escala calibrada: las medidas ahora se muestran también en mm."));
}

MainWindow::~MainWindow() {
    autoTimer_.stop();
    captureTimer_.stop();
    controller_.stop();
    enumerationWatcher_.waitForFinished();
    analysisWatcher_.waitForFinished();
    inspectionWatcher_.waitForFinished();
    captureWatcher_.waitForFinished();
}

// --- Cámara y análisis -----------------------------------------------------

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
    controller_.start(cameras_[comboIndex]);
}

void MainWindow::onFrame(const QImage& frame) {
    video_->setFrame(frame);
    lastFrame_ = frame;
    if (streaming_) {
        // El análisis corre siempre: da el fixture que ancla el dibujo en vivo.
        pendingAnalysisFrame_ = frame;
        maybeStartAnalysis();
    }
}

void MainWindow::onAnalysisFinished() {
    const AnalysisOverlay overlay = analysisWatcher_.result();
    if (streaming_) {
        const QString status = overlay.valid
                                   ? tr("Pieza: %1°").arg(overlay.angleDeg, 0, 'f', 1)
                                   : overlay.error;
        if (overlay.valid) {
            // El fixture llega ya estabilizado desde el worker (banda muerta,
            // suavizado y continuidad anti-giro de 180°).
            liveFixture_ = vision::Fixture{{static_cast<float>(overlay.centroid.x()),
                                            static_cast<float>(overlay.centroid.y())},
                                           overlay.angleDeg};
            video_->setLivePiece(true, overlay.contour, overlay.centroid,
                                 overlay.angleDeg, status);
            currentThumbLabel_->setPixmap(QPixmap::fromImage(overlay.normalized)
                                              .scaled(currentThumbLabel_->size(),
                                                      Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
        } else {
            liveFixture_.reset();
            video_->setLivePiece(false, overlay.contour, overlay.centroid,
                                 overlay.angleDeg, status);
        }
        // Medidas en vivo de las herramientas dibujadas (px o mm calibrados).
        video_->setResults(overlay.toolResults);
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
    const auto anchor = currentAnchor_;

    // Copia de las herramientas dibujadas para medirlas sobre este frame.
    std::vector<inspection::ToolConfig> configs;
    configs.reserve(liveTools_.size());
    for (const auto& tool : liveTools_) {
        auto config = tool.config;
        config.geometryJson = inspection::toJson(tool.geometry);
        configs.push_back(std::move(config));
    }

    analysisWatcher_.setFuture(QtConcurrent::run(
        [frame, anchor, offset = currentOrientationOffset_, configs = std::move(configs),
         pipeline = pipelineConfig_, previous = liveFixture_,
         mm = calibration_.mmPerPixel, unit = currentUnit()] {
            return buildOverlay(frame, anchor, offset, configs, pipeline, previous, mm, unit);
        }));
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
    autoInspectButton_->setChecked(false);
    stopLiveCapture();
    startStopButton_->setText(tr("Iniciar"));
    startStopButton_->setEnabled(!cameras_.empty());
    cameraCombo_->setEnabled(true);
    refreshButton_->setEnabled(true);
    statsLabel_->clear();
    pendingAnalysisFrame_ = QImage();
    lastFrame_ = QImage();
    video_->clearLive();
}

void MainWindow::setControlsEnabled(bool enabled) {
    cameraCombo_->setEnabled(enabled);
    refreshButton_->setEnabled(enabled);
    startStopButton_->setEnabled(enabled && !cameras_.empty());
}

// --- Herramientas dibujadas sobre el video ---------------------------------

void MainWindow::onToolModeChanged(int id) {
    if (id < 0) {
        video_->setCreateType(std::nullopt);
        statusBar()->showMessage(tr("Modo mover: clic para seleccionar, arrastra para mover."));
        return;
    }
    const auto type = static_cast<inspection::ToolType>(id);
    video_->setCreateType(type);
    // La primera línea de la descripción como guía inmediata.
    const QString description = QString::fromUtf8(inspection::toolTypeDescription(type));
    statusBar()->showMessage(description.section(QLatin1Char('\n'), 0, 1));
}

void MainWindow::onLiveToolCreated(const inspection::ToolGeometry& geometry) {
    inspection::EditedTool tool;
    tool.geometry = geometry;
    tool.config.type = inspection::typeOf(geometry);
    ++toolNameCounter_;
    tool.config.name = (toolTypeLabel(tool.config.type) +
                        QStringLiteral(" %1").arg(toolNameCounter_))
                           .toStdString();
    tool.config.geometryJson = inspection::toJson(geometry);
    tool.config.toleranceMin = 0.0;
    tool.config.toleranceMax = 100000.0;

    // Medir la pieza actual de inmediato y sugerir tolerancias alrededor de
    // ese valor: la pieza buena define su propio rango de aceptación.
    QString hint;
    if (liveFixture_.has_value() && !lastFrame_.isNull()) {
        const auto result =
            inspection::runTool(camera::qImageToMat(lastFrame_), *liveFixture_, tool.config,
                                calibration_.mmPerPixel);
        if (result.isOk() && !result.value().detail.empty() &&
            (result.value().ok || result.value().measured > 0.0)) {
            inspection::suggestTolerances(tool.config.type, result.value().measured,
                                          tool.config.toleranceMin,
                                          tool.config.toleranceMax);
            const QString measure =
                tool.config.type == inspection::ToolType::Blob
                    ? QString::number(result.value().measured, 'f', 0)
                    : QString::fromStdString(
                          calibration_.formatLength(result.value().measured));
            hint = tr("%1 — midió %2; tolerancias sugeridas [%3, %4] px")
                       .arg(QString::fromStdString(tool.config.name), measure)
                       .arg(tool.config.toleranceMin, 0, 'f', 1)
                       .arg(tool.config.toleranceMax, 0, 'f', 1);
        } else {
            hint = tr("%1 creada, pero no midió en este frame (%2) — ajusta su posición")
                       .arg(QString::fromStdString(tool.config.name),
                            QString::fromStdString(result.isOk() ? result.value().detail
                                                                 : result.error().message));
        }
    }
    liveTools_.push_back(std::move(tool));
    commitUndoState();

    video_->clearResults();
    const int newIndex = static_cast<int>(liveTools_.size()) - 1;
    video_->setSelectedIndex(newIndex);
    onLiveSelectionChanged(newIndex);  // sincroniza el spin de "Puntos"
    statusBar()->showMessage(hint.isEmpty()
                                 ? tr("%1 creada").arg(QString::fromStdString(
                                       liveTools_.back().config.name))
                                 : hint);
}

// Un movimiento terminó (arrastre en el canvas): estado nuevo, undoable.
void MainWindow::onLiveToolModified() {
    video_->clearResults();
    commitUndoState();
}

void MainWindow::onDeleteToolClicked() {
    auto indices = video_->selectedIndices();
    if (indices.empty()) {
        return;
    }
    // De mayor a menor para que los índices no se corran al borrar.
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    for (const int index : indices) {
        if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
            continue;
        }
        const auto& tool = liveTools_[static_cast<std::size_t>(index)];
        if (tool.config.id >= 0 && repos_.tools != nullptr) {
            // Si se deshace el borrado, el guardado reinsertará la fila.
            if (auto removed = repos_.tools->remove(tool.config.id); !removed.isOk()) {
                statusBar()->showMessage(QString::fromStdString(removed.error().message));
                continue;
            }
        }
        liveTools_.erase(liveTools_.begin() + index);
    }
    commitUndoState();
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
}

// Marcar el rasgo distintivo: el siguiente clic sobre la pieza en el video
// define el punto; se guarda de inmediato si hay una pieza seleccionada.
void MainWindow::onAnchorButtonToggled(bool enabled) {
    if (!enabled) {
        video_->setPickMode(false);
        return;
    }

    // Si la pieza ya tiene rasgo, ofrecer quitarlo o reemplazarlo.
    if (currentAnchor_.has_value()) {
        QMessageBox box(QMessageBox::Question, tr("Rasgo distintivo"),
                        tr("Esta pieza ya tiene un rasgo distintivo. ¿Qué quieres hacer?"),
                        QMessageBox::NoButton, this);
        auto* removeBtn = box.addButton(tr("Quitar rasgo"), QMessageBox::DestructiveRole);
        auto* replaceBtn = box.addButton(tr("Marcar otro"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        anchorButton_->setChecked(false);
        if (box.clickedButton() == removeBtn) {
            currentAnchor_.reset();
            video_->setAnchorMarker(false);
            if (const std::int64_t pieceId = selectedPieceId();
                pieceId >= 0 && repos_.pieces != nullptr) {
                repos_.pieces->clearAnchor(pieceId);
            }
            statusBar()->showMessage(tr("Rasgo distintivo eliminado."));
            return;
        }
        if (box.clickedButton() != replaceBtn) {
            return;
        }
        // Reemplazar: continúa al modo de selección abajo.
    }

    if (!streaming_ || !liveFixture_.has_value()) {
        statusBar()->showMessage(
            tr("Para marcar el rasgo necesitas video en vivo con la pieza detectada."));
        anchorButton_->setChecked(false);
        return;
    }
    video_->setPickMode(true);
    statusBar()->showMessage(
        tr("Haz clic sobre un punto único de la pieza (agujero, marca, esquina oscura)…"));
}

void MainWindow::onAnchorPicked(const cv::Point2f& imagePoint) {
    anchorButton_->setChecked(false);
    if (!liveFixture_.has_value() || lastFrame_.isNull()) {
        return;
    }

    vision::OrientationAnchor anchor;
    anchor.piecePoint = vision::toPieceCoords(*liveFixture_, imagePoint);
    anchor.intensity = vision::sampleIntensity(camera::qImageToMat(lastFrame_), imagePoint);
    currentAnchor_ = anchor;
    video_->setAnchorMarker(true, anchor.piecePoint);

    const std::int64_t pieceId = selectedPieceId();
    if (pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto saved = repos_.pieces->saveAnchor(pieceId, anchor); saved.isOk()) {
            statusBar()->showMessage(
                tr("Rasgo distintivo guardado: la pieza se detectará en cualquier rotación."));
        } else {
            statusBar()->showMessage(QString::fromStdString(saved.error().message));
        }
    } else {
        statusBar()->showMessage(
            tr("Rasgo marcado — se guardará con la pieza al registrar."));
    }
}

// Sincroniza el spin de "Puntos" con la herramienta seleccionada en el video.
void MainWindow::onLiveSelectionChanged(int index) {
    liveParamSpin_->setEnabled(false);
    liveParamLabel_->setText(tr("Puntos:"));
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    QSignalBlocker blocker(liveParamSpin_);
    std::visit(
        [this](const auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, inspection::CaliperGeometry>) {
                liveParamLabel_->setText(tr("Banda (px):"));
                liveParamSpin_->setValue(static_cast<int>(g.bandWidth));
                liveParamSpin_->setEnabled(true);
            } else if constexpr (std::is_same_v<T, inspection::CircleGeometry>) {
                liveParamLabel_->setText(tr("Rayos:"));
                liveParamSpin_->setValue(g.rayCount);
                liveParamSpin_->setEnabled(true);
            } else if constexpr (std::is_same_v<T, inspection::EdgeFlawGeometry>) {
                liveParamLabel_->setText(tr("Escaneos:"));
                liveParamSpin_->setValue(g.scanCount);
                liveParamSpin_->setEnabled(true);
            } else if constexpr (std::is_same_v<T, inspection::BlobGeometry>) {
                liveParamLabel_->setText(tr("Área mín:"));
                liveParamSpin_->setValue(static_cast<int>(g.minArea));
                liveParamSpin_->setEnabled(true);
            }
            // Punto-Línea no tiene parámetro de muestreo editable.
        },
        liveTools_[static_cast<std::size_t>(index)].geometry);
}

void MainWindow::onLiveParamChanged(int value) {
    const int index = video_->selectedIndex();
    if (!liveParamSpin_->isEnabled() || index < 0 ||
        index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    std::visit(
        [value](auto& g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, inspection::CaliperGeometry>) {
                g.bandWidth = static_cast<float>(value);
            } else if constexpr (std::is_same_v<T, inspection::CircleGeometry>) {
                g.rayCount = value;
            } else if constexpr (std::is_same_v<T, inspection::EdgeFlawGeometry>) {
                g.scanCount = value;
            } else if constexpr (std::is_same_v<T, inspection::BlobGeometry>) {
                g.minArea = static_cast<float>(value);
            }
        },
        liveTools_[static_cast<std::size_t>(index)].geometry);
    commitUndoState();
    video_->update();
}

void MainWindow::rotatePieceView(double deltaDeg) {
    currentOrientationOffset_ = wrapAngleDeg(currentOrientationOffset_ + deltaDeg);
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto saved =
                repos_.pieces->saveOrientationOffset(pieceId, currentOrientationOffset_);
            !saved.isOk()) {
            statusBar()->showMessage(QString::fromStdString(saved.error().message));
            return;
        }
        statusBar()->showMessage(
            tr("Orientación de la pieza girada a %1° (guardada).")
                .arg(currentOrientationOffset_, 0, 'f', 0));
    } else {
        statusBar()->showMessage(
            tr("Vista girada a %1° — se guardará con la pieza al registrar.")
                .arg(currentOrientationOffset_, 0, 'f', 0));
    }
}

void MainWindow::onManagePiecesClicked() {
    if (repos_.pieces == nullptr) {
        QMessageBox::warning(this, tr("BD no disponible"),
                             tr("La gestión de piezas necesita la base de datos."));
        return;
    }
    const std::int64_t previous = selectedPieceId();
    PieceManagerDialog dialog(repos_.pieces, repos_.tools, this);
    dialog.exec();
    if (dialog.changed()) {
        autoInspectButton_->setChecked(false);
        loadPieceList(previous);
        onPieceSelectionChanged(pieceCombo_->currentIndex());
    }
}

// Repuebla el combo de plantillas de la pieza actual. Siempre incluye
// "principal" aunque aún no tenga herramientas.
void MainWindow::loadTemplateList(const QString& selectName) {
    QSignalBlocker blocker(templateCombo_);
    const QString previous = selectName.isEmpty() ? templateCombo_->currentText()
                                                  : selectName;
    templateCombo_->clear();

    std::vector<std::string> names{"principal"};
    if (const std::int64_t pieceId = selectedPieceId();
        pieceId >= 0 && repos_.tools != nullptr) {
        if (auto listed = repos_.tools->listTemplates(pieceId); listed.isOk()) {
            for (const auto& name : listed.value()) {
                if (std::find(names.begin(), names.end(), name) == names.end()) {
                    names.push_back(name);
                }
            }
        }
    }
    for (const auto& name : names) {
        templateCombo_->addItem(QString::fromStdString(name));
    }
    const int idx = templateCombo_->findText(previous);
    templateCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
}

void MainWindow::onTemplateChanged(int index) {
    Q_UNUSED(index);
    autoInspectButton_->setChecked(false);
    loadToolsForSelectedPiece();
}

void MainWindow::onNewTemplateClicked() {
    if (selectedPieceId() < 0) {
        QMessageBox::information(this, tr("Sin pieza"),
                                 tr("Selecciona o registra una pieza primero."));
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Nueva plantilla"),
                              tr("Nombre de la plantilla:"), QLineEdit::Normal,
                              tr("plantilla %1").arg(templateCombo_->count() + 1), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (templateCombo_->findText(name) >= 0) {
        QMessageBox::warning(this, tr("Ya existe"),
                             tr("Ya hay una plantilla con ese nombre."));
        return;
    }
    // La plantilla nace vacía; se materializa al guardar su primera herramienta.
    templateCombo_->addItem(name);
    templateCombo_->setCurrentText(name);  // dispara onTemplateChanged
    statusBar()->showMessage(
        tr("Plantilla '%1' activa: dibuja herramientas y se guardarán en ella.").arg(name));
}

void MainWindow::onToolRightClicked(int index) {
    deleteToolAt(index);
}

void MainWindow::deleteToolAt(int index) {
    if (index < 0 || index >= static_cast<int>(liveTools_.size())) {
        return;
    }
    const auto& tool = liveTools_[static_cast<std::size_t>(index)];
    if (tool.config.id >= 0 && repos_.tools != nullptr) {
        if (auto removed = repos_.tools->remove(tool.config.id); !removed.isOk()) {
            statusBar()->showMessage(QString::fromStdString(removed.error().message));
            return;
        }
    }
    liveTools_.erase(liveTools_.begin() + index);
    commitUndoState();
    video_->setSelectedIndex(-1);
    onLiveSelectionChanged(-1);
    video_->clearResults();
    statusBar()->showMessage(tr("Herramienta eliminada."));
}

void MainWindow::onPieceSelectionChanged(int index) {
    Q_UNUSED(index);
    autoInspectButton_->setChecked(false);
    loadTemplateList();       // repuebla plantillas de la pieza
    loadToolsForSelectedPiece();

    // Rasgo distintivo y ajuste de orientación de la pieza seleccionada.
    currentAnchor_.reset();
    currentOrientationOffset_ = 0.0;
    video_->setAnchorMarker(false);
    if (const std::int64_t pieceId = selectedPieceId();
        pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto anchor = repos_.pieces->loadAnchor(pieceId);
            anchor.isOk() && anchor.value().has_value()) {
            currentAnchor_ = anchor.value();
            video_->setAnchorMarker(true, currentAnchor_->piecePoint);
        }
        if (auto offset = repos_.pieces->loadOrientationOffset(pieceId); offset.isOk()) {
            currentOrientationOffset_ = offset.value();
        }
    }

    // Miniatura de la pieza registrada para el panel de comparación.
    referenceThumb_ = QImage();
    refThumbLabel_->setPixmap(QPixmap());
    refThumbLabel_->setText(QStringLiteral("—"));
    similarityLabel_->clear();
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId >= 0 && repos_.pieces != nullptr) {
        if (auto thumb = repos_.pieces->loadThumbnail(pieceId);
            thumb.isOk() && !thumb.value().empty()) {
            referenceThumb_ = QImage::fromData(thumb.value().data(),
                                               static_cast<int>(thumb.value().size()));
            if (!referenceThumb_.isNull()) {
                refThumbLabel_->setPixmap(QPixmap::fromImage(referenceThumb_)
                                              .scaled(refThumbLabel_->size(),
                                                      Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
            }
        } else {
            refThumbLabel_->setText(tr("Sin miniatura\n(regístrala de nuevo\npara generarla)"));
        }
    }
}

void MainWindow::loadToolsForSelectedPiece() {
    liveTools_.clear();
    video_->setSelectedIndex(-1);
    video_->clearResults();

    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 || repos_.tools == nullptr) {
        video_->update();
        return;
    }
    auto listed = repos_.tools->listForPiece(pieceId, activeTemplate());
    if (!listed.isOk()) {
        core::logWarning("No se pudieron cargar las herramientas: " + listed.error().message);
        return;
    }
    for (auto& config : listed.value()) {
        auto geometry = inspection::geometryFromJson(config.type, config.geometryJson);
        if (!geometry.isOk()) {
            core::logWarning("Herramienta '" + config.name +
                             "' con geometría corrupta: " + geometry.error().message);
            continue;
        }
        inspection::EditedTool tool;
        tool.config = std::move(config);
        tool.geometry = std::move(geometry.value());
        liveTools_.push_back(std::move(tool));
    }
    // Cambiar de pieza reinicia el historial de deshacer.
    undoStack_.clear();
    stableTools_ = liveTools_;
    video_->update();
}

// --- Registro en vivo -------------------------------------------------------

void MainWindow::onRegisterLiveClicked() {
    if (!streaming_ || lastFrame_.isNull()) {
        QMessageBox::information(this, tr("Sin video"),
                                 tr("Inicia la cámara primero (o usa el asistente para "
                                    "registrar desde imágenes)."));
        return;
    }
    if (repos_.pieces == nullptr || !repos_.embedFn) {
        QMessageBox::warning(this, tr("No disponible"),
                             tr("El registro necesita la base de datos y el modelo de "
                                "embeddings (ejecuta run.ps1)."));
        return;
    }

    // Pedir el nombre validando duplicados ANTES de capturar nada: si ya
    // existe se ofrece guardar como nueva versión de esa pieza o renombrar.
    pendingPieceId_ = -1;
    QString name;
    while (true) {
        name = QInputDialog::getText(this, tr("Registrar pieza"), tr("Nombre de la pieza:"),
                                     QLineEdit::Normal, name)
                   .trimmed();
        if (name.isEmpty()) {
            return;
        }
        const auto exists = repos_.pieces->nameExists(name.toStdString());
        if (!exists.isOk()) {
            QMessageBox::warning(this, tr("Error"),
                                 QString::fromStdString(exists.error().message));
            return;
        }
        if (!exists.value()) {
            break;  // nombre libre
        }

        QMessageBox question(QMessageBox::Question, tr("La pieza ya existe"),
                             tr("Ya existe una pieza llamada '%1'.\n\n¿Qué quieres hacer?")
                                 .arg(name),
                             QMessageBox::NoButton, this);
        auto* newVersion =
            question.addButton(tr("Guardar como nueva versión"), QMessageBox::AcceptRole);
        question.addButton(tr("Elegir otro nombre"), QMessageBox::ActionRole);
        auto* cancel = question.addButton(QMessageBox::Cancel);
        question.exec();
        if (question.clickedButton() == cancel) {
            return;
        }
        if (question.clickedButton() == newVersion) {
            if (auto pieces = repos_.pieces->listPieces(); pieces.isOk()) {
                for (const auto& piece : pieces.value()) {
                    if (piece.name == name.toStdString()) {
                        pendingPieceId_ = piece.id;
                        break;
                    }
                }
            }
            break;
        }
        // "Elegir otro nombre": vuelve a preguntar conservando el texto.
    }
    pendingPieceName_ = name;

    // El rasgo distintivo marcado (si hay) fija la orientación de las 30
    // capturas de referencia y se guarda con la pieza.
    liveSession_ = std::make_shared<engine::RegistrationSession>(
        repos_.embedFn, kCaptureTarget, kCaptureMinimum, currentAnchor_, pipelineConfig_,
        currentOrientationOffset_);
    captureProgress_ = new QProgressDialog(
        tr("Capturando referencias de '%1'…\nMantén la pieza a la vista.")
            .arg(pendingPieceName_),
        tr("Cancelar"), 0, kCaptureTarget, this);
    captureProgress_->setWindowModality(Qt::WindowModal);
    captureProgress_->setMinimumDuration(0);
    captureProgress_->setValue(0);
    connect(captureProgress_, &QProgressDialog::canceled, this,
            &MainWindow::onCaptureCanceled);

    captureTimer_.start();
}

void MainWindow::onCaptureTick() {
    if (captureWatcher_.isRunning() || lastFrame_.isNull() || liveSession_ == nullptr) {
        return;
    }
    // shared_ptr capturado: la sesión sobrevive aunque el usuario cancele
    // mientras un frame sigue procesándose en el pool.
    auto session = liveSession_;
    const QImage frame = lastFrame_;
    captureWatcher_.setFuture(QtConcurrent::run([session, frame] {
        using ResultT = core::Result<engine::RegistrationSession::SampleFeedback>;
        try {
            return session->addFrame(camera::qImageToMat(frame));
        } catch (const std::exception& e) {
            return ResultT::err(std::string("Error interno de captura: ") + e.what());
        } catch (...) {
            return ResultT::err("Error interno de captura");
        }
    }));
}

void MainWindow::onCaptureProcessed() {
    if (liveSession_ == nullptr || captureProgress_ == nullptr) {
        return;  // registro cancelado mientras se procesaba un frame
    }
    const auto result = captureWatcher_.result();
    if (!result.isOk()) {
        stopLiveCapture();
        QMessageBox::warning(this, tr("Registro fallido"),
                             QString::fromStdString(result.error().message));
        return;
    }

    const auto& feedback = result.value();
    captureProgress_->setValue(feedback.count);
    if (!feedback.accepted) {
        captureProgress_->setLabelText(
            tr("Capturando referencias de '%1'…\nRechazada: %2")
                .arg(pendingPieceName_, QString::fromStdString(feedback.reason)));
    } else {
        captureProgress_->setLabelText(tr("Capturando referencias de '%1'…\n%2 de %3")
                                           .arg(pendingPieceName_)
                                           .arg(feedback.count)
                                           .arg(kCaptureTarget));
    }

    if (feedback.count >= kCaptureTarget) {
        finishLiveRegistration();
    }
}

void MainWindow::onCaptureCanceled() {
    stopLiveCapture();
    statusBar()->showMessage(tr("Registro cancelado."));
}

void MainWindow::stopLiveCapture() {
    captureTimer_.stop();
    liveSession_.reset();
    if (captureProgress_ != nullptr) {
        captureProgress_->deleteLater();
        captureProgress_ = nullptr;
    }
}

void MainWindow::finishLiveRegistration() {
    captureTimer_.stop();
    auto session = liveSession_;

    auto reference = session->finish();
    if (!reference.isOk()) {
        stopLiveCapture();
        QMessageBox::warning(this, tr("Registro incompleto"),
                             QString::fromStdString(reference.error().message));
        return;
    }

    // Pieza nueva, o nueva versión de una existente (elegido al pedir nombre).
    std::int64_t pieceId = pendingPieceId_;
    if (pieceId < 0) {
        auto created = repos_.pieces->createPiece(pendingPieceName_.toStdString());
        if (!created.isOk()) {
            stopLiveCapture();
            QMessageBox::warning(this, tr("No se pudo crear la pieza"),
                                 QString::fromStdString(created.error().message));
            return;
        }
        pieceId = created.value();
    }

    const auto savedVersion = repos_.pieces->saveReference(pieceId, reference.value());
    if (!savedVersion.isOk()) {
        stopLiveCapture();
        QMessageBox::warning(this, tr("No se pudo guardar la referencia"),
                             QString::fromStdString(savedVersion.error().message));
        return;
    }

    // Miniatura del recorte normalizado: alimenta el panel "Pieza registrada".
    const auto thumbnail = engine::encodeThumbnailJpeg(session->firstNormalized(), 256);
    if (!thumbnail.empty()) {
        if (auto saved = repos_.pieces->saveThumbnail(pieceId, thumbnail); !saved.isOk()) {
            core::logWarning("No se pudo guardar la miniatura: " + saved.error().message);
        }
    }

    // Rasgo distintivo elegido durante la sesión.
    if (currentAnchor_.has_value()) {
        if (auto saved = repos_.pieces->saveAnchor(pieceId, *currentAnchor_);
            !saved.isOk()) {
            core::logWarning("No se pudo guardar el rasgo distintivo: " +
                             saved.error().message);
        }
    }

    // Persistir las herramientas dibujadas sobre el video (en la plantilla
    // activa: una pieza puede tener varias plantillas).
    const std::string tmpl = activeTemplate();
    int toolErrors = 0;
    if (repos_.tools != nullptr) {
        for (auto& tool : liveTools_) {
            tool.config.geometryJson = inspection::toJson(tool.geometry);
            if (auto saved = repos_.tools->save(pieceId, tool.config, tmpl); saved.isOk()) {
                tool.config.id = saved.value();
            } else {
                ++toolErrors;
                core::logError(saved.error().message);
            }
        }
    }

    stopLiveCapture();
    // Seleccionar la pieza sin recargar las herramientas recién guardadas,
    // pero sí refrescar la miniatura de referencia del panel.
    {
        QSignalBlocker blocker(pieceCombo_);
        loadPieceList(pieceId);
    }
    referenceThumb_ = QImage::fromData(thumbnail.data(), static_cast<int>(thumbnail.size()));
    if (!referenceThumb_.isNull()) {
        refThumbLabel_->setPixmap(QPixmap::fromImage(referenceThumb_)
                                      .scaled(refThumbLabel_->size(), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
    }

    statusBar()->showMessage(
        toolErrors == 0
            ? tr("'%1' registrada (referencia v%2) con %3 herramienta(s). "
                 "Auto-inspección activa.")
                  .arg(pendingPieceName_)
                  .arg(savedVersion.value())
                  .arg(liveTools_.size())
            : tr("'%1' registrada, pero %2 herramienta(s) no se guardaron (ver log).")
                  .arg(pendingPieceName_)
                  .arg(toolErrors));

    autoInspectButton_->setChecked(true);
}

// --- Auto-inspección ---------------------------------------------------------

void MainWindow::onAutoToggled(bool enabled) {
    if (enabled) {
        if (repos_.engine == nullptr || selectedPieceId() < 0 || !streaming_) {
            QMessageBox::information(
                this, tr("Auto-inspección"),
                tr("Necesitas video en vivo y una pieza registrada seleccionada."));
            autoInspectButton_->setChecked(false);
            return;
        }
        autoInspecting_ = true;
        // En inspección el operador solo lee piezas: se bloquea la edición y
        // se desactivan las herramientas de dibujo.
        video_->setEditingLocked(true);
        video_->setCreateType(std::nullopt);
        if (auto* selectBtn = toolModeGroup_->button(-1)) {
            selectBtn->setChecked(true);
        }
        for (auto* button : toolModeGroup_->buttons()) {
            button->setEnabled(false);
        }
        deleteToolButton_->setEnabled(false);
        verdictBanner_->setStyleSheet(
            QStringLiteral("background:#444; color:white; font-size:16px; font-weight:bold;"));
        verdictBanner_->setText(tr("Auto-inspección en marcha…"));
        verdictBanner_->setVisible(true);
        autoTimer_.start();
    } else {
        autoInspecting_ = false;
        autoTimer_.stop();
        video_->setEditingLocked(false);
        for (auto* button : toolModeGroup_->buttons()) {
            button->setEnabled(true);
        }
        deleteToolButton_->setEnabled(true);
        verdictBanner_->setVisible(false);
        video_->clearResults();
    }
}

void MainWindow::onAutoTick() {
    if (inspectionWatcher_.isRunning() || lastFrame_.isNull()) {
        return;
    }
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0) {
        autoInspectButton_->setChecked(false);
        return;
    }
    inspectedFrame_ = lastFrame_;
    auto* engine = repos_.engine;
    engine->setPipelineConfig(pipelineConfig_);
    engine->setMmPerPixel(calibration_.mmPerPixel);
    engine->setUnit(currentUnit());
    engine->setTemplateName(activeTemplate());
    const QImage frame = inspectedFrame_;
    inspectionWatcher_.setFuture(QtConcurrent::run([engine, frame, pieceId] {
        using ResultT = core::Result<engine::InspectionEngine::Outcome>;
        try {
            return engine->inspect(camera::qImageToMat(frame), pieceId);
        } catch (const std::exception& e) {
            return ResultT::err(std::string("Error interno de inspección: ") + e.what());
        } catch (...) {
            return ResultT::err("Error interno de inspección");
        }
    }));
}

void MainWindow::showLiveVerdict(const engine::InspectionEngine::Outcome& outcome) {
    verdictBanner_->setStyleSheet(
        outcome.verdict.ok
            ? QStringLiteral(
                  "background:#1e6f2f; color:white; font-size:16px; font-weight:bold;")
            : QStringLiteral(
                  "background:#8f1f1f; color:white; font-size:16px; font-weight:bold;"));
    verdictBanner_->setText(QString::fromStdString(outcome.verdict.summary));
    // Los overlays de herramientas ya los pinta la medición en vivo de cada
    // frame; aquí solo el veredicto y la similitud.

    if (outcome.verdict.embedding.evaluated) {
        similarityLabel_->setText(tr("Similitud: %1\nUmbral: %2")
                                      .arg(outcome.verdict.embedding.similarity, 0, 'f', 4)
                                      .arg(outcome.verdict.embedding.threshold, 0, 'f', 4));
    } else {
        similarityLabel_->setText(
            QString::fromStdString(outcome.verdict.embedding.note));
    }
}

// --- Flujos con diálogo -------------------------------------------------------

// Último frame de la cámara o imagen elegida por el usuario (los flujos
// completos deben poder probarse en equipos sin cámara).
QImage MainWindow::frameOrFile() {
    if (!lastFrame_.isNull()) {
        return lastFrame_;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Elegir imagen"), QString(), tr("Imágenes (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty()) {
        return {};
    }
    QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Imagen inválida"), tr("No se pudo cargar la imagen."));
        return {};
    }
    return image.convertToFormat(QImage::Format_BGR888);
}

void MainWindow::loadPieceList(std::int64_t selectId) {
    pieceCombo_->clear();
    if (repos_.pieces == nullptr) {
        pieceCombo_->addItem(tr("BD no disponible"));
        pieceCombo_->setEnabled(false);
        return;
    }
    auto pieces = repos_.pieces->listPieces();
    if (!pieces.isOk()) {
        core::logWarning("No se pudieron listar las piezas: " + pieces.error().message);
        return;
    }
    for (const auto& piece : pieces.value()) {
        pieceCombo_->addItem(QString::fromStdString(piece.name),
                             QVariant::fromValue<qlonglong>(piece.id));
        if (piece.id == selectId) {
            pieceCombo_->setCurrentIndex(pieceCombo_->count() - 1);
        }
    }
    if (pieceCombo_->count() == 0) {
        pieceCombo_->addItem(tr("(sin piezas registradas)"));
    }
}

std::int64_t MainWindow::selectedPieceId() const {
    const QVariant data = pieceCombo_->currentData();
    return data.isValid() ? data.toLongLong() : -1;
}

void MainWindow::onRegisterWizardClicked() {
    if (repos_.pieces == nullptr) {
        QMessageBox::warning(this, tr("BD no disponible"),
                             tr("No se puede registrar sin base de datos."));
        return;
    }
    if (!repos_.embedFn) {
        QMessageBox::warning(
            this, tr("Modelo no disponible"),
            tr("El registro necesita el modelo de embeddings. Ejecuta run.ps1 para "
               "descargarlo y prepararlo."));
        return;
    }

    RegistrationWizard wizard(&controller_, repos_.embedFn, repos_.pieces, this);
    if (wizard.exec() == QDialog::Accepted) {
        loadPieceList(wizard.createdPieceId());
    }
}

void MainWindow::onOpenEditorClicked() {
    const QImage reference = frameOrFile();
    if (reference.isNull()) {
        return;
    }

    const auto analysis = vision::analyzeFrame(camera::qImageToMat(reference));
    if (!analysis.isOk()) {
        QMessageBox::warning(this, tr("Sin pieza detectada"),
                             tr("No se pudo analizar la imagen: %1")
                                 .arg(QString::fromStdString(analysis.error().message)));
        return;
    }

    // Con pieza seleccionada se edita su plantilla; sin piezas, una "demo".
    std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0 && repos_.pieces != nullptr) {
        if (auto created = repos_.pieces->createPiece("demo"); created.isOk()) {
            pieceId = created.value();
            loadPieceList(pieceId);
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
                                    pieceId >= 0 ? repos_.tools : nullptr, calibration_,
                                    activeTemplate(), this);
    editor.exec();
    // Reflejar en el video los cambios hechos en el editor.
    loadToolsForSelectedPiece();
}

void MainWindow::onInspectClicked() {
    if (repos_.engine == nullptr) {
        QMessageBox::warning(this, tr("Motor no disponible"),
                             tr("La inspección necesita la base de datos."));
        return;
    }
    const std::int64_t pieceId = selectedPieceId();
    if (pieceId < 0) {
        QMessageBox::information(this, tr("Sin pieza"),
                                 tr("Registra o selecciona una pieza primero."));
        return;
    }
    if (inspectionWatcher_.isRunning()) {
        return;
    }

    const QImage frame = frameOrFile();
    if (frame.isNull()) {
        return;
    }

    inspectedFrame_ = frame;
    inspectButton_->setEnabled(false);
    statusBar()->showMessage(tr("Inspeccionando…"));
    auto* engine = repos_.engine;
    engine->setPipelineConfig(pipelineConfig_);
    engine->setMmPerPixel(calibration_.mmPerPixel);
    engine->setUnit(currentUnit());
    engine->setTemplateName(activeTemplate());
    inspectionWatcher_.setFuture(QtConcurrent::run([engine, frame, pieceId] {
        using ResultT = core::Result<engine::InspectionEngine::Outcome>;
        try {
            return engine->inspect(camera::qImageToMat(frame), pieceId);
        } catch (const std::exception& e) {
            return ResultT::err(std::string("Error interno de inspección: ") + e.what());
        } catch (...) {
            return ResultT::err("Error interno de inspección");
        }
    }));
}

void MainWindow::onInspectionFinished() {
    inspectButton_->setEnabled(true);
    const auto result = inspectionWatcher_.result();
    const bool autoMode = autoInspectButton_->isChecked();

    if (!result.isOk()) {
        if (autoMode) {
            verdictBanner_->setStyleSheet(QStringLiteral(
                "background:#444; color:#ffb066; font-size:16px; font-weight:bold;"));
            verdictBanner_->setText(QString::fromStdString(result.error().message));
        } else {
            statusBar()->showMessage(tr("Inspección fallida"));
            QMessageBox::warning(this, tr("Inspección fallida"),
                                 QString::fromStdString(result.error().message));
        }
        return;
    }

    const std::int64_t pieceId = selectedPieceId();
    if (repos_.inspections != nullptr) {
        if (auto stats = repos_.inspections->todayStats(pieceId); stats.isOk()) {
            statusBar()->showMessage(tr("Hoy: %1 inspecciones — %2 OK / %3 NG")
                                         .arg(stats.value().total)
                                         .arg(stats.value().okCount)
                                         .arg(stats.value().ngCount));
        }
    }

    if (autoMode) {
        showLiveVerdict(result.value());
        return;
    }

    InspectionResultDialog dialog(inspectedFrame_, result.value(), repos_.engine, pieceId,
                                  referenceThumb_, calibration_, this);
    dialog.exec();
}

}  // namespace pci::ui
