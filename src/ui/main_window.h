#pragma once

#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "ui/rate_readout.h"
#include "ui/setup_guide.h"
#include "ui/setup_guide.h"
#include "ui/station_status.h"
#include "vision/stage_stats.h"
#include "camera/camera_controller.h"
#include "camera/camera_info.h"
#include "camera/frame_source.h"
#include "domain/calibration.h"
#include "engine/inspection_engine.h"
#include "inspection_editor/tools/undo_stack.h"
#include "ui/shortcuts_dialog.h"
#include "engine/registration_session.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "inspection_editor/canvas/tool_palette.h"
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
    void updateRateReadout();
    void onCameraError(const QString& message);
    void onStreamStopped();
    void onAnalysisFinished();
    // Herramientas dibujadas sobre el video.
    void onToolModeChanged(std::optional<pci::inspection::ToolType> type);
    void onLiveToolCreated(const pci::inspection::ToolGeometry& geometry);
    void onLiveToolModified();
    void onDeleteToolClicked();
    // Borrar TODAS. Va aparte y no como variante de la anterior porque necesita
    // lo que la otra no: preguntar antes, y decir cuántas se lleva por delante.
    void onDeleteAllToolsClicked();
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
    // «¿Cuánto mide esto?»: mide la pieza entera a partir de su contorno y
    // enseña el informe. Es la pregunta que más se hace delante de una pieza,
    // y hasta ahora había que dar un rodeo por el editor de plantilla para
    // contestarla.
    void onMeasurePieceClicked();
    void onRoiButtonToggled(bool enabled);
    void onRegionPicked(const cv::Rect& imageRect);
    void onFreeZoneButtonToggled(bool enabled);
    void onFreeZonePicked(const std::vector<cv::Point>& imagePolygon);
    void onFreeZoneCancelled();
    void onClearZoneClicked();
    void onUnitChanged();
    void onTemplateChanged(int index);
    void onNewTemplateClicked();
    void onManageTemplatesClicked();  // gestor de plantillas (M1)
    void onShowHistoryClicked();      // pantalla de historial (S1)
    void onConfigureClicked();        // panel Configurar, un solo sitio (C1)
    void onExportConfigClicked();     // exportar configuración (O4)
    // Volver a los valores de fábrica. Es irreversible, así que pregunta
    // antes y dice exactamente qué se lleva y qué no toca.
    void onResetConfigClicked();
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
    // Mover o cambiar de tamaño la ventana arma el guardado diferido de abajo.
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void changeEvent(QEvent* event) override;  // maximizar/restaurar

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
    // El tablero por defecto guardado en `Settings`, que es lo que la regla
    // de `persistBoardConfig` llama «la plantilla para piezas nuevas».
    [[nodiscard]] vision::BoardConfig defaultBoardConfig() const;
    // Y quien la cumple: una pieza recién creada hereda ese tablero.
    //
    // Sin esto, la regla estaba escrita y no se cumplía: la pieza nueva se
    // quedaba con los valores por defecto del ESQUEMA («bounds», sin giro,
    // sin desfase), así que configurar el tablero sin ninguna pieza no servía
    // para nada — que es justo el único momento en que ese ajuste global se
    // puede tocar.
    void seedMeasurementForNewPiece(std::int64_t pieceId);
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
    // Tamaño, posición, pantalla, maximizada y disposición de paneles.
    //
    // No basta con guardarlo al cerrar: el estado de los paneles ya se hacía
    // así, y un cierre que no pase por `closeEvent` —un corte de luz en la
    // línea, un apagado a lo bruto— se llevaba por delante justo lo que el
    // operador coloca una vez y espera no volver a tocar. Se guarda además con
    // un retardo después de mover o redimensionar, para no escribir en la base
    // de datos en cada píxel del arrastre.
    void persistWindowLayout();
    void restoreWindowLayout();
    void scheduleWindowLayoutSave();
    // Con qué se estaba trabajando: pieza, plantilla y fuente. Solo se
    // RECUERDA la elección; abrir la fuente sigue siendo un gesto del
    // operador, igual que con la cámara, que también se preselecciona sin
    // arrancarse sola.
    void persistLastSession();
    // Páginas del panel Configurar (C1). Las de formulario se vuelcan al pulsar
    // Aplicar; la de cámara aplica sola y aquí solo se persiste lo que deja.
    void applyDetectionPage(DetectionPage* page);
    // Piezas esperadas (C5): va con la pieza seleccionada, no con la máquina.
    void applyPiecesPage(PiecesPage* page);
    // Abrir una imagen o un vídeo como fuente. False si el operador canceló el
    // diálogo de fichero, que no es un error y no debe dejar la ventana a
    // medio arrancar.
    bool startFileSource(pci::camera::SourceKind kind);
    // Congelar el frame actual y trabajar sobre él, o soltarlo y volver al
    // vídeo. La cámara NO se cierra al congelar: se deja de escuchar y se
    // vuelve a escuchar, para que volver cueste cero y no haya que resondear
    // controles ni relanzar el perfil de exposición.
    void toggleFrozenPhoto();
    // Cómo se llama, para el operador, la imagen que hay ahora mismo. Existe
    // porque varios diálogos ofrecían «Frame actual de la cámara» aunque la
    // fuente fuera una foto, un fichero o un vídeo: nombrar una fuente que no es
    // la que hay le dice al operador que va a usar algo distinto de lo que ve.
    [[nodiscard]] QString currentSourceLabel() const;
    // ¿Va alguien a leer CUÁNTAS piezas se ven? La pieza espera más de una, o
    // el panel Configurar está abierto. Se pregunta en dos sitios —al montar el
    // análisis y al elegir la zona— y tienen que responder lo mismo, así que la
    // condición se escribe una vez.
    [[nodiscard]] bool countingPieces() const;
    // Zona de trabajo (C3): elige con qué recorte se analiza el próximo frame.
    // El recorte automático NO pisa la zona manual del operador: son dos
    // cosas distintas y el modo decide cuál manda.
    [[nodiscard]] cv::Rect effectiveWorkingZone() const;
    void setWorkingZoneMode(pci::vision::WorkingZoneMode mode);
    void updateWorkingZoneOverlay();
    // La configuración con la que INSPECCIONAR. La zona libre solo recorta si
    // su modo está activo: una guardada de otro día, con el modo apagado, no
    // puede seguir tapando media imagen sin que nadie lo haya pedido.
    [[nodiscard]] vision::PipelineConfig inspectionConfig() const;
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
    // Automática de fábrica: es la que no puede cambiar una respuesta.
    vision::WorkingZoneMode zoneMode_ = vision::WorkingZoneMode::Automatic;
    vision::AutoRoiTracker autoRoi_;
    int expectedPieces_ = 1;  // de la pieza seleccionada (C5)
    int lastPieceCount_ = -1;  // piezas vistas en el último análisis
    QAction* registerWizardAction_ = nullptr;
    QAction* managePiecesAction_ = nullptr;
    QAction* editorAction_ = nullptr;
    // Espejo en el menú del botón de auto-inspección de la barra: una acción
    // que solo existe en la barra no la encuentra quien navega con el teclado.
    QAction* autoInspectAction_ = nullptr;
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
    // UN control para la zona, con menú. Antes eran dos botones que además
    // cambiaban de texto según el estado, así que en la barra se leía «Zona de
    // detección | Quitar zona libre»: un botón diciendo lo que dibuja junto a
    // otro diciendo lo que borra.
    QToolButton* zoneButton_ = nullptr;
    QAction* rectZoneAction_ = nullptr;
    QAction* freeZoneAction_ = nullptr;
    QAction* clearZoneAction_ = nullptr;
    QPushButton* measurePieceButton_ = nullptr;
    // Ruta del fichero abierto como fuente, para recordarlo entre sesiones.
    QString lastSourcePath_;
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
    inspection::ToolPalette* toolPalette_ = nullptr;
    QPushButton* anchorButton_ = nullptr;  // marcar el rasgo distintivo
    QLabel* liveParamLabel_ = nullptr;     // "Puntos" de la herramienta elegida
    QSpinBox* liveParamSpin_ = nullptr;
    QPushButton* calibrateFromToolButton_ = nullptr;  // fijar escala con la medida
    QPushButton* saveTemplateButton_ = nullptr;       // guardar herramientas en vivo (P1)
    QDockWidget* compareDock_ = nullptr;              // panel comparación reubicable (S3)
    QPushButton* managePiecesButton_ = nullptr;

    // Guía del primer arranque (I3). No es un asistente: es una línea que
    // señala la tira de estado y se quita cuando el operador la ha leído.
    QWidget* setupBanner_ = nullptr;
    QLabel* setupHintLabel_ = nullptr;
    bool setupGuided_ = false;
    void updateSetupGuide();
    void dismissSetupGuide();

    // Dock de herramientas (P5). La fila 3 se quedaba sin ancho, y lo que
    // ACTÚA sobre la herramienta seleccionada se va con la paleta.
    QDockWidget* toolsDock_ = nullptr;

    QLabel* verdictBanner_ = nullptr;
    QLabel* boardReadoutLabel_ = nullptr;  // dx/dy/radio/giro respecto al tablero (T3)
    inspection::EditorCanvas* video_ = nullptr;
    // Si cada automático de la cámara está encendido AHORA. No se lee de la
    // cámara: se lleva la cuenta de lo que se le ha hecho, porque preguntárselo
    // no sirve — `get(CAP_PROP_AUTO_EXPOSURE)` devuelve −1 pase lo que pase, y
    // sobre esa mentira ya se perdieron dos diseños en C1.
    bool autoExposureOn_ = false;
    bool autoFocusOn_ = false;

    // Ritmo del ANÁLISIS, que es el que decide si se mide o no se mide, y los
    // frames que se quedan por el camino. Los de captura los cuenta el hilo de
    // la cámara; estos solo se pueden contar aquí, que es donde se descartan.
    FrameAccounting frames_;
    // Desglose de tiempos por etapa (R2). Apagado por defecto: corre en el
    // camino más caliente del programa y nadie lo mira si no ha abierto la
    // pestaña de Rendimiento a propósito.
    bool measureStages_ = false;
    vision::StageStats stageStats_;
    double lastCaptureFps_ = 0.0;

    // Tira de estado de la estación (I1): los cuatro datos que deciden si una
    // medida vale, sin abrir nada.
    std::vector<QPushButton*> stationLights_;
    void updateStationStatus();

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
    // La fuente de archivo, viva solo cuando la fuente elegida es una imagen o
    // un vídeo. La cámara y los ficheros no comparten clase a propósito: la
    // cámara tiene controles, resolución y perfil de exposición que un fichero
    // no puede prometer, y una interfaz común obligaría a rellenar esos huecos
    // con métodos vacíos. Lo que sí comparten —y es lo único que la ventana
    // necesita— es que llega un frame.
    std::unique_ptr<camera::FrameSource> fileSource_;
    camera::SourceKind sourceKind_ = camera::SourceKind::Camera;
    // La escucha de la cámara, guardada para poder cortarla mientras se mira
    // una foto y reanudarla al soltarla.
    QMetaObject::Connection cameraFrames_;
    QPushButton* freezeButton_ = nullptr;
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
    // Guardado diferido de la geometría: un arrastre de ventana emite
    // decenas de eventos y no hacen falta decenas de escrituras.
    QTimer layoutSaveTimer_;
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
