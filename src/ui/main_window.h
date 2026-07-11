#pragma once

#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "camera/camera_controller.h"
#include "camera/camera_info.h"
#include "domain/calibration.h"
#include "engine/inspection_engine.h"
#include "inspection_editor/tools/undo_stack.h"
#include "ui/shortcuts_dialog.h"
#include "engine/registration_session.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "ui/analysis_overlay.h"
#include "ui/app_repositories.h"
#include "vision/orientation_anchor.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressDialog;
class QPushButton;

namespace pci::ui {

// Ventana principal: video en vivo sobre el que se dibujan las herramientas
// en tiempo real (ancladas a la pieza), registro con captura automática de
// referencias y auto-inspección continua con veredicto OK/NG en vivo.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // Los servicios pueden venir vacíos: la app funciona sin persistencia
    // si la BD no pudo abrirse (error ya loggeado por quien la abrió).
    explicit MainWindow(AppRepositories repositories = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    // Cámara y análisis en vivo.
    void refreshCameras();
    void onCamerasEnumerated();
    void onStartStopClicked();
    void onFrame(const QImage& frame);
    void onStats(double fps, int width, int height);
    void onCameraError(const QString& message);
    void onStreamStopped();
    void onAnalysisFinished();
    // Herramientas dibujadas sobre el video.
    void onToolModeChanged(int id);
    void onLiveToolCreated(const pci::inspection::ToolGeometry& geometry);
    void onLiveToolModified();
    void onDeleteToolClicked();
    void onUndo();
    void onRedo();
    void onShowShortcuts();
    void onAnchorButtonToggled(bool enabled);
    void onAnchorPicked(const cv::Point2f& imagePoint);
    void onPieceSelectionChanged(int index);
    // Registro en vivo y auto-inspección.
    void onRegisterLiveClicked();
    void onCaptureTick();
    void onCaptureProcessed();
    void onCaptureCanceled();
    void onAutoToggled(bool enabled);
    void onAutoTick();
    // Flujos con diálogo (siguen disponibles, p. ej. sin cámara).
    void onRegisterWizardClicked();
    void onOpenEditorClicked();
    void onInspectClicked();
    void onInspectionFinished();
    void onCalibrateClicked();

private:
    void setControlsEnabled(bool enabled);
    void maybeStartAnalysis();
    void updateCalibrationLabel();
    void buildShortcuts();
    void commitUndoState();
    void restoreTools(std::vector<inspection::EditedTool> tools);
    void loadPieceList(std::int64_t selectId = -1);
    void loadToolsForSelectedPiece();
    void finishLiveRegistration();
    void stopLiveCapture();
    void showLiveVerdict(const engine::InspectionEngine::Outcome& outcome);
    [[nodiscard]] std::int64_t selectedPieceId() const;
    [[nodiscard]] QImage frameOrFile();

    // Fila 1: cámara.
    QComboBox* cameraCombo_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* startStopButton_ = nullptr;
    QPushButton* calibrateButton_ = nullptr;
    QCheckBox* analysisCheck_ = nullptr;
    QLabel* calibLabel_ = nullptr;  // estado de la escala en la barra inferior
    // Fila 2: pieza y flujo.
    QComboBox* pieceCombo_ = nullptr;
    QPushButton* registerLiveButton_ = nullptr;
    QPushButton* autoInspectButton_ = nullptr;
    QPushButton* registerWizardButton_ = nullptr;
    QPushButton* editorButton_ = nullptr;
    QPushButton* inspectButton_ = nullptr;
    // Fila 3: herramientas para dibujar sobre el video.
    QButtonGroup* toolModeGroup_ = nullptr;
    QPushButton* deleteToolButton_ = nullptr;
    QPushButton* anchorButton_ = nullptr;  // marcar el rasgo distintivo

    QLabel* verdictBanner_ = nullptr;
    inspection::EditorCanvas* video_ = nullptr;
    QLabel* statsLabel_ = nullptr;
    // Panel de comparación: pieza registrada vs pieza actual.
    QLabel* refThumbLabel_ = nullptr;
    QLabel* currentThumbLabel_ = nullptr;
    QLabel* similarityLabel_ = nullptr;

    AppRepositories repos_;
    QImage lastFrame_;
    QImage inspectedFrame_;
    camera::CameraController controller_;
    QFutureWatcher<std::vector<camera::CameraInfo>> enumerationWatcher_;
    QFutureWatcher<AnalysisOverlay> analysisWatcher_;
    QFutureWatcher<core::Result<engine::InspectionEngine::Outcome>> inspectionWatcher_;
    QFutureWatcher<core::Result<engine::RegistrationSession::SampleFeedback>> captureWatcher_;
    QImage pendingAnalysisFrame_;
    std::vector<camera::CameraInfo> cameras_;
    std::vector<inspection::EditedTool> liveTools_;
    std::vector<inspection::EditedTool> stableTools_;  // estado previo a la mutación en curso
    inspection::UndoStack<std::vector<inspection::EditedTool>> undoStack_;
    std::vector<ShortcutSpec> shortcuts_;
    std::shared_ptr<engine::RegistrationSession> liveSession_;
    QProgressDialog* captureProgress_ = nullptr;
    QTimer captureTimer_;
    QTimer autoTimer_;
    QString pendingPieceName_;
    std::int64_t pendingPieceId_ = -1;  // >= 0: nueva versión de pieza existente
    std::optional<vision::Fixture> liveFixture_;
    std::optional<vision::OrientationAnchor> currentAnchor_;
    domain::ScaleCalibration calibration_;
    QImage referenceThumb_;
    int toolNameCounter_ = 0;
    bool streaming_ = false;
};

}  // namespace pci::ui
