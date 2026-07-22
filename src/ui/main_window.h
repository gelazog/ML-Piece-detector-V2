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

class QAction;
class QActionGroup;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressDialog;
class QPushButton;
class QSpinBox;

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
    void onDuplicateToolClicked();
    void onSaveTemplateClicked();  // guarda liveTools_ en la plantilla activa (P1)
    void onUndo();
    void onRedo();
    void onShowShortcuts();
    void onAnchorButtonToggled(bool enabled);
    void onAnchorPicked(const cv::Point2f& imagePoint);
    void onPieceSelectionChanged(int index);
    void onManagePiecesClicked();
    void onLiveSelectionChanged(int index);
    void onLiveParamChanged(int value);
    void onCalibrateFromToolClicked();
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
    void onDetectionClicked();
    void onRoiButtonToggled(bool enabled);
    void onRegionPicked(const cv::Rect& imageRect);
    void onUnitChanged();
    void onTemplateChanged(int index);
    void onNewTemplateClicked();
    void onToolRightClicked(int index);

private:
    void setControlsEnabled(bool enabled);
    void maybeStartAnalysis();
    [[nodiscard]] bool analysisNeeded() const;
    void updateCalibrationLabel();
    void persistCalibration();  // sella resolución/cámara y guarda en Settings
    void buildMenuBar();
    void buildShortcuts();
    void commitUndoState();
    void restoreTools(std::vector<inspection::EditedTool> tools);
    void persistPipelineConfig();
    void updateRoiButton();
    void rotatePieceView(double deltaDeg);
    void loadPieceList(std::int64_t selectId = -1);
    void loadTemplateList(const QString& selectName = QString());
    void loadToolsForSelectedPiece();
    void deleteToolAt(int index);
    [[nodiscard]] inspection::LengthUnit currentUnit() const;
    [[nodiscard]] std::string activeTemplate() const;
    void finishLiveRegistration();
    void stopLiveCapture();
    void showLiveVerdict(const engine::InspectionEngine::Outcome& outcome);
    [[nodiscard]] std::int64_t selectedPieceId() const;
    [[nodiscard]] QImage frameOrFile();

    // Menú y acciones de baja frecuencia (antes botones sueltos).
    QAction* refreshAction_ = nullptr;
    QAction* calibrateAction_ = nullptr;
    QAction* detectionAction_ = nullptr;
    QAction* registerWizardAction_ = nullptr;
    QAction* managePiecesAction_ = nullptr;
    QAction* editorAction_ = nullptr;
    QAction* showContourAction_ = nullptr;   // Ver > Mostrar contorno (checkable)
    QAction* trackRotationAction_ = nullptr;  // Ver > Seguir rotación (checkable)
    QActionGroup* unitGroup_ = nullptr;      // Ver > Unidad (Auto/mm/cm/px)
    // Fila 1: cámara (controles de uso constante).
    QComboBox* cameraCombo_ = nullptr;
    QPushButton* startStopButton_ = nullptr;
    QPushButton* roiButton_ = nullptr;
    QLabel* calibLabel_ = nullptr;  // estado de la escala en la barra inferior
    // Fila 2: pieza y flujo.
    QComboBox* pieceCombo_ = nullptr;
    QComboBox* templateCombo_ = nullptr;   // plantillas de la pieza
    QPushButton* newTemplateButton_ = nullptr;
    QPushButton* registerLiveButton_ = nullptr;
    QPushButton* autoInspectButton_ = nullptr;
    QPushButton* inspectButton_ = nullptr;
    // Fila 3: herramientas para dibujar sobre el video.
    QButtonGroup* toolModeGroup_ = nullptr;
    QPushButton* deleteToolButton_ = nullptr;
    QPushButton* anchorButton_ = nullptr;  // marcar el rasgo distintivo
    QLabel* liveParamLabel_ = nullptr;     // "Puntos" de la herramienta elegida
    QSpinBox* liveParamSpin_ = nullptr;
    QPushButton* calibrateFromToolButton_ = nullptr;  // fijar escala con la medida
    QPushButton* saveTemplateButton_ = nullptr;       // guardar herramientas en vivo (P1)
    QPushButton* managePiecesButton_ = nullptr;

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
    double currentOrientationOffset_ = 0.0;
    domain::ScaleCalibration calibration_;
    // Identidad de la cámara con la que se calibró vs. la que transmite ahora,
    // para avisar si la escala quedó obsoleta al cambiar de cámara (D1).
    QString calibratedCameraKey_;
    QString currentCameraKey_;
    vision::PipelineConfig pipelineConfig_;
    QImage referenceThumb_;
    int toolNameCounter_ = 0;
    bool streaming_ = false;
    bool autoInspecting_ = false;
    bool arucoLiveScale_ = false;   // escala por marcador ArUco en vivo
    double markerSizeMm_ = 30.0;    // lado real del marcador impreso
};

}  // namespace pci::ui
