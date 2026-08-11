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
#include "vision/auto_roi.h"
#include "ui/app_repositories.h"
#include "repositories/piece_repository.h"
#include "vision/board_frame.h"
#include "vision/orientation_anchor.h"

class QAction;
class QActionGroup;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QProgressDialog;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace pci::ui {

class CameraImagePage;
class ConfigureDialog;
class DetectionPage;
class PerformancePage;
class PiecesPage;
class PreferencesPage;

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
    void onRoiButtonToggled(bool enabled);
    void onRegionPicked(const cv::Rect& imageRect);
    void onUnitChanged();
    void onTemplateChanged(int index);
    void onNewTemplateClicked();
    void onManageTemplatesClicked();  // gestor de plantillas (M1)
    void onShowHistoryClicked();      // pantalla de historial (S1)
    void onConfigureClicked();        // panel Configurar, un solo sitio (C1)
    void onExportConfigClicked();     // exportar configuración (O4)
    void onImportConfigClicked();
    void onControlsProbed(const std::vector<pci::camera::CameraControlState>& controls);
    void onResolutionsProbed(const std::vector<pci::camera::CameraResolution>& available,
                             const pci::camera::CameraResolution& current);
    // Reajusta lo que vive en píxeles cuando cambia la resolución del frame.
    void rescalePixelSettings(const QSize& from, const QSize& to);
    void onMeasurementModeClicked();  // modo de medición de la pieza (M2)
    void onToolRightClicked(int index);

protected:
    void closeEvent(QCloseEvent* event) override;  // aviso de cambios sin guardar (P2)

private:
    void setControlsEnabled(bool enabled);
    void maybeStartAnalysis();
    [[nodiscard]] bool analysisNeeded() const;
    void updateCalibrationLabel();
    void persistCalibration();     // sella resolución/cámara y guarda en Settings
    void updateStatusIndicators();  // pone al día los iconos de estado (S4)
    void updateZoomIndicator();     // porcentaje y botones de la barra de zoom (Z3)
    void onBoardOriginChanged(QAction* action);  // origen del tablero (T2)
    void updateBoardReadout();  // lectura en vivo de desviacion y giro (T3)
    void persistBoardConfig();
    // Aplica el modo + tablero de una pieza a toda la UI (canvas, motor,
    // visibilidad del tablero y menús). Es el único punto que cambia el estado
    // de medición, para que no se desincronicen.
    void applyMeasurement(const repositories::PieceMeasurement& measurement);
    void loadMeasurementForSelectedPiece();
    void loadDetectionProfileForSelectedPiece();  // perfil de detección (O3)
    void updateModeChip();  // etiqueta del modo activo (M3)
    [[nodiscard]] int positionToolCount() const;
    // Avisa si cambiar el cero deja las herramientas de Posición midiendo otra
    // cosa (sus tolerancias se sugirieron respecto al origen anterior).
    void warnIfPositionToolsAffected(vision::BoardOrigin previousOrigin);
    void buildMenuBar();
    void buildShortcuts();
    void commitUndoState();
    void restoreTools(std::vector<inspection::EditedTool> tools);
    void persistPipelineConfig();
    // Páginas del panel Configurar (C1). Las de formulario se vuelcan al pulsar
    // Aplicar; la de cámara aplica sola y aquí solo se persiste lo que deja.
    void applyDetectionPage(DetectionPage* page);
    // Piezas esperadas (C5): va con la pieza seleccionada, no con la máquina.
    void applyPiecesPage(PiecesPage* page);
    // Zona de trabajo (C3): elige con qué recorte se analiza el próximo frame.
    // El recorte automático NO pisa la zona manual del operador: son dos
    // cosas distintas y el modo decide cuál manda.
    [[nodiscard]] cv::Rect effectiveWorkingZone() const;
    void setWorkingZoneMode(pci::vision::WorkingZoneMode mode);
    void updateWorkingZoneOverlay();
    void applyPreferencesPage(PreferencesPage* page);
    void wireCameraPage(CameraImagePage* page);
    void updateRoiButton();
    void rotatePieceView(double deltaDeg);
    void loadPieceList(std::int64_t selectId = -1);
    void loadTemplateList(const QString& selectName = QString());
    void loadToolsForSelectedPiece();
    void deleteToolAt(int index);
    // Guardado de plantilla (P1/P2). saveTemplate resuelve la pieza (creándola
    // si hace falta) y persiste; devuelve false si el usuario cancela. persist
    // hace el upsert propiamente dicho. confirmSaveBeforeLeaving muestra el
    // aviso Guardar/Descartar/Cancelar si hay cambios sin guardar.
    bool saveTemplate(std::int64_t pieceId);
    void persistTemplateTools(std::int64_t pieceId);
    [[nodiscard]] bool confirmSaveBeforeLeaving();
    void selectPieceById(std::int64_t pieceId);  // fija el combo sin disparar señales
    [[nodiscard]] inspection::LengthUnit currentUnit() const;
    [[nodiscard]] std::string activeTemplate() const;
    void finishLiveRegistration();
    void stopLiveCapture();
    void showLiveVerdict(const engine::InspectionEngine::Outcome& outcome);
    [[nodiscard]] std::int64_t selectedPieceId() const;
    [[nodiscard]] QImage frameOrFile();
    [[nodiscard]] QImage openImageFile();  // siempre abre el diálogo de archivo

    // Menú y acciones de baja frecuencia (antes botones sueltos).
    QAction* refreshAction_ = nullptr;
    QAction* calibrateAction_ = nullptr;
    QAction* configureAction_ = nullptr;  // Cámara > Configurar… (C1)
    // Panel Configurar abierto (no modal, uno solo) y su última pestaña.
    ConfigureDialog* configureDialog_ = nullptr;
    int configureTab_ = 0;
    // Zona de trabajo automática (C3).
    vision::WorkingZoneMode zoneMode_ = vision::WorkingZoneMode::Off;
    vision::AutoRoiTracker autoRoi_;
    int expectedPieces_ = 1;  // de la pieza seleccionada (C5)
    int lastPieceCount_ = -1;  // piezas vistas en el último análisis
    QAction* registerWizardAction_ = nullptr;
    QAction* managePiecesAction_ = nullptr;
    QAction* editorAction_ = nullptr;
    QAction* showContourAction_ = nullptr;   // Ver > Mostrar contorno (checkable)
    QAction* trackRotationAction_ = nullptr;  // Ver > Seguir rotación (checkable)
    QActionGroup* unitGroup_ = nullptr;      // Ver > Unidad (Auto/mm/cm/px)
    // Tablero de referencia centrado (T2).
    QAction* boardAction_ = nullptr;          // Ver > Tablero de referencia
    QAction* boardFollowAction_ = nullptr;    // ejes girados con la pieza
    QAction* rulerAction_ = nullptr;          // Ver > Regla graduada
    bool rulerVisible_ = false;
    QActionGroup* boardOriginGroup_ = nullptr;
    bool boardVisible_ = false;
    bool boardPointPick_ = false;  // el próximo clic fija el cero del tablero
    // Avisos que solo tienen sentido una vez por sesión (repetirlos molesta y
    // hace que se dejen de leer).
    bool positionWarningShown_ = false;
    bool toolsOnlyAccepted_ = false;
    bool boardReadoutAlarm_ = false;  // estado pintado de la banda de lectura
    vision::BoardConfig boardConfig_;
    std::int64_t currentProfileId_ = 0;  // perfil de detección de la pieza (O3)
    // Modo de medición de la pieza activa (M1/M2). Sin pieza seleccionada actúa
    // como valor por defecto de la sesión.
    domain::MeasurementMode measurementMode_ = domain::MeasurementMode::Real;
    // Controles de la fuente (O2): lo que la cámara reportó al abrirse y los
    // valores elegidos por el operador, que se reaplican en cada arranque.
    std::vector<camera::CameraControlState> cameraControls_;
    std::vector<camera::CameraControlValue> savedCameraControls_;
    camera::CameraResolution savedResolution_;   // resolución elegida (O2)
    camera::CameraResolution currentResolution_;  // la que está dando la cámara
    std::vector<camera::CameraResolution> knownResolutions_;  // sondeadas y cacheadas
    [[nodiscard]] std::string resolutionCacheKey() const;
    void loadCachedResolutions();
    // Reglas del modo Especial de la pieza activa (M4); 0 = no vigilar.
    double maxOffsetPx_ = 0.0;
    double maxAngleDeg_ = 0.0;
    // Modo elegido durante un registro en curso, para guardarlo al terminar.
    repositories::PieceMeasurement pendingMeasurement_;
    // Fila 1: cámara (controles de uso constante).
    QComboBox* cameraCombo_ = nullptr;
    QPushButton* startStopButton_ = nullptr;
    QPushButton* roiButton_ = nullptr;
    QLabel* calibLabel_ = nullptr;  // estado de la escala en la barra inferior
    // Fila 2: pieza y flujo.
    QComboBox* pieceCombo_ = nullptr;
    QLabel* modeChip_ = nullptr;           // modo de medición activo (M3)
    QComboBox* templateCombo_ = nullptr;   // plantillas de la pieza
    QPushButton* newTemplateButton_ = nullptr;
    QPushButton* manageTemplatesButton_ = nullptr;  // abre el gestor de plantillas
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
    QDockWidget* compareDock_ = nullptr;              // panel comparación reubicable (S3)
    QPushButton* managePiecesButton_ = nullptr;

    QLabel* verdictBanner_ = nullptr;
    QLabel* boardReadoutLabel_ = nullptr;  // dx/dy/radio/giro respecto al tablero (T3)
    inspection::EditorCanvas* video_ = nullptr;
    QLabel* statsLabel_ = nullptr;
    // Indicadores de estado (S4): cámara / base de datos / modelo ONNX.
    QLabel* camIndicator_ = nullptr;
    QLabel* dbIndicator_ = nullptr;
    QLabel* modelIndicator_ = nullptr;
    // Controles de vista (Z3): mínimo / − / % / + / máximo.
    QLabel* zoomLabel_ = nullptr;
    QToolButton* zoomInButton_ = nullptr;
    QToolButton* zoomOutButton_ = nullptr;
    QToolButton* zoomMinButton_ = nullptr;
    QToolButton* zoomMaxButton_ = nullptr;
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
    // Sincronización tiempo real ↔ plantilla (P2): flag de cambios sin guardar y
    // la pieza/plantilla a la que pertenecen las herramientas en vivo, para poder
    // guardar en la correcta y restaurar el combo si el operador cancela.
    bool templateDirty_ = false;
    std::int64_t loadedPieceId_ = -1;
    QString loadedTemplate_;
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
    // Preferencias configurables (O1), persistidas en Settings.
    int autoIntervalMs_ = 1000;     // intervalo de auto-inspección
    double kSigma_ = 3.0;           // sensibilidad de anomalía de apariencia
};

}  // namespace pci::ui
