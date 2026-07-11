#pragma once

#include <QDialog>
#include <QImage>

#include <opencv2/core.hpp>

#include <optional>

#include "domain/calibration.h"

class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace pci::inspection {
class EditorCanvas;
}

namespace pci::ui {

// Calibración de escala px->mm sobre una foto del plano de trabajo.
// Método A: dos clics sobre una distancia real conocida (regla, moneda) +
// longitud en mm. Método B: distancia cámara->superficie + FOV horizontal.
// Ambos producen mm/px; el A además estima la distancia de la cámara.
class CalibrationDialog : public QDialog {
    Q_OBJECT

public:
    CalibrationDialog(const QImage& snapshot, domain::ScaleCalibration current,
                      QWidget* parent = nullptr);

    [[nodiscard]] const domain::ScaleCalibration& calibration() const { return result_; }

private slots:
    void onPointPicked(const cv::Point2f& imagePoint);
    void onKnownLengthChanged();
    void onUseCameraDistance();
    void onApply();

private:
    void updateFromKnownLength();
    void showResult();

    inspection::EditorCanvas* canvas_ = nullptr;
    QLabel* measuredLabel_ = nullptr;
    QDoubleSpinBox* knownMm_ = nullptr;
    QDoubleSpinBox* fovDeg_ = nullptr;
    QDoubleSpinBox* cameraDistMm_ = nullptr;
    QLabel* resultLabel_ = nullptr;
    QPushButton* applyButton_ = nullptr;

    QImage snapshot_;
    std::optional<cv::Point2f> pointA_;
    std::optional<cv::Point2f> pointB_;
    double measuredPx_ = 0.0;
    domain::ScaleCalibration result_;
};

}  // namespace pci::ui
