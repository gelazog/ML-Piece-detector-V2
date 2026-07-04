#pragma once

#include <QFutureWatcher>
#include <QMainWindow>

#include <vector>

#include "camera/camera_controller.h"
#include "camera/camera_info.h"
#include "engine/inspection_engine.h"
#include "ui/analysis_overlay.h"
#include "ui/app_repositories.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace pci::ui {

class VideoWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // Los repositorios pueden venir vacíos: la app funciona sin persistencia
    // si la BD no pudo abrirse (error ya loggeado por quien la abrió).
    explicit MainWindow(AppRepositories repositories = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void refreshCameras();
    void onCamerasEnumerated();
    void onStartStopClicked();
    void onFrame(const QImage& frame);
    void onStats(double fps, int width, int height);
    void onCameraError(const QString& message);
    void onStreamStopped();
    void onAnalysisToggled(bool enabled);
    void onAnalysisFinished();
    void onOpenEditorClicked();
    void onRegisterClicked();
    void onInspectClicked();
    void onInspectionFinished();

private:
    void setControlsEnabled(bool enabled);
    void maybeStartAnalysis();
    void loadPieceList(std::int64_t selectId = -1);
    [[nodiscard]] std::int64_t selectedPieceId() const;
    [[nodiscard]] QImage frameOrFile();

    QComboBox* cameraCombo_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* startStopButton_ = nullptr;
    QComboBox* pieceCombo_ = nullptr;
    QPushButton* registerButton_ = nullptr;
    QPushButton* editorButton_ = nullptr;
    QPushButton* inspectButton_ = nullptr;
    QCheckBox* analysisCheck_ = nullptr;
    VideoWidget* video_ = nullptr;
    QLabel* statsLabel_ = nullptr;

    AppRepositories repos_;
    QImage lastFrame_;
    QImage inspectedFrame_;
    camera::CameraController controller_;
    QFutureWatcher<std::vector<camera::CameraInfo>> enumerationWatcher_;
    QFutureWatcher<AnalysisOverlay> analysisWatcher_;
    QFutureWatcher<core::Result<engine::InspectionEngine::Outcome>> inspectionWatcher_;
    QImage pendingAnalysisFrame_;
    std::vector<camera::CameraInfo> cameras_;
    bool streaming_ = false;
};

}  // namespace pci::ui
