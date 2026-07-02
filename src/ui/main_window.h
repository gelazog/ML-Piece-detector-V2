#pragma once

#include <QFutureWatcher>
#include <QMainWindow>

#include <vector>

#include "camera/camera_controller.h"
#include "camera/camera_info.h"
#include "ui/analysis_overlay.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace pci::ui {

class VideoWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
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

private:
    void setControlsEnabled(bool enabled);
    void maybeStartAnalysis();

    QComboBox* cameraCombo_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* startStopButton_ = nullptr;
    QCheckBox* analysisCheck_ = nullptr;
    VideoWidget* video_ = nullptr;
    QLabel* statsLabel_ = nullptr;

    camera::CameraController controller_;
    QFutureWatcher<std::vector<camera::CameraInfo>> enumerationWatcher_;
    QFutureWatcher<AnalysisOverlay> analysisWatcher_;
    QImage pendingAnalysisFrame_;
    std::vector<camera::CameraInfo> cameras_;
    bool streaming_ = false;
};

}  // namespace pci::ui
