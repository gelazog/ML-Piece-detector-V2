#include "ui/calibration_dialog.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>

#include "inspection_editor/canvas/editor_canvas.h"

namespace pci::ui {

CalibrationDialog::CalibrationDialog(const QImage& snapshot,
                                     domain::ScaleCalibration current, QWidget* parent)
    : QDialog(parent), snapshot_(snapshot), result_(current) {
    setWindowTitle(tr("Calibración de escala (px → mm)"));
    resize(1000, 640);

    auto* rootLayout = new QHBoxLayout(this);

    canvas_ = new inspection::EditorCanvas(this);
    canvas_->setScene(snapshot_, vision::Fixture{{0.0F, 0.0F}, 0.0});
    canvas_->setPickMode(true);
    rootLayout->addWidget(canvas_, 1);

    auto* sideLayout = new QVBoxLayout();

    // Estado actual bien visible: esta es la sección dedicada a la escala.
    auto* stateLabel = new QLabel(this);
    stateLabel->setWordWrap(true);
    stateLabel->setStyleSheet(
        QStringLiteral("font-weight:bold; padding:6px; background:#22333a; color:#cfe;"));
    stateLabel->setText(current.valid()
                            ? tr("Escala actual: %1 mm/px").arg(current.mmPerPixel, 0, 'f', 4)
                            : tr("Sin calibrar — las medidas están en píxeles."));
    sideLayout->addWidget(stateLabel);

    auto* methodA = new QGroupBox(tr("Método A: objeto de referencia (recomendado)"), this);
    auto* formA = new QFormLayout(methodA);
    auto* help = new QLabel(
        tr("Coloca una regla u objeto de tamaño conocido sobre la superficie y haz "
           "DOS CLICS sobre los extremos de una distancia conocida."),
        methodA);
    help->setWordWrap(true);
    formA->addRow(help);
    measuredLabel_ = new QLabel(tr("Distancia marcada: —"), methodA);
    formA->addRow(measuredLabel_);
    knownMm_ = new QDoubleSpinBox(methodA);
    knownMm_->setRange(0.1, 10000.0);
    knownMm_->setValue(100.0);
    knownMm_->setSuffix(QStringLiteral(" mm"));
    formA->addRow(tr("Longitud real:"), knownMm_);
    sideLayout->addWidget(methodA);

    auto* methodB = new QGroupBox(tr("Método B: distancia de cámara"), this);
    auto* formB = new QFormLayout(methodB);
    cameraDistMm_ = new QDoubleSpinBox(methodB);
    cameraDistMm_->setRange(10.0, 100000.0);
    cameraDistMm_->setValue(result_.cameraDistanceMm > 0.0 ? result_.cameraDistanceMm
                                                           : 300.0);
    cameraDistMm_->setSuffix(QStringLiteral(" mm"));
    formB->addRow(tr("Cámara → superficie:"), cameraDistMm_);
    auto* useDistance = new QPushButton(tr("Calcular escala con la distancia"), methodB);
    formB->addRow(useDistance);
    sideLayout->addWidget(methodB);

    auto* fovForm = new QFormLayout();
    fovDeg_ = new QDoubleSpinBox(this);
    fovDeg_->setRange(20.0, 120.0);
    fovDeg_->setValue(result_.horizontalFovDeg > 0.0 ? result_.horizontalFovDeg : 60.0);
    fovDeg_->setSuffix(QStringLiteral(" °"));
    fovDeg_->setToolTip(
        tr("Campo de visión horizontal de la cámara (webcams típicas: 55–70°).\n"
           "Solo afecta la distancia estimada (método A) o la escala (método B)."));
    fovForm->addRow(tr("FOV horizontal:"), fovDeg_);
    sideLayout->addLayout(fovForm);

    resultLabel_ = new QLabel(this);
    resultLabel_->setWordWrap(true);
    resultLabel_->setStyleSheet(QStringLiteral("font-weight:bold;"));
    sideLayout->addWidget(resultLabel_);
    sideLayout->addStretch(1);

    auto* buttonsLayout = new QHBoxLayout();
    applyButton_ = new QPushButton(tr("Aplicar calibración"), this);
    applyButton_->setEnabled(result_.valid());
    buttonsLayout->addWidget(applyButton_);
    auto* resetButton = new QPushButton(tr("Quitar calibración"), this);
    resetButton->setToolTip(tr("Vuelve a medir en píxeles."));
    buttonsLayout->addWidget(resetButton);
    auto* cancel = new QPushButton(tr("Cancelar"), this);
    buttonsLayout->addWidget(cancel);
    sideLayout->addLayout(buttonsLayout);

    rootLayout->addLayout(sideLayout);

    connect(canvas_, &inspection::EditorCanvas::pointPicked, this,
            &CalibrationDialog::onPointPicked);
    connect(knownMm_, &QDoubleSpinBox::valueChanged, this,
            &CalibrationDialog::onKnownLengthChanged);
    connect(fovDeg_, &QDoubleSpinBox::valueChanged, this,
            &CalibrationDialog::onKnownLengthChanged);
    connect(useDistance, &QPushButton::clicked, this,
            &CalibrationDialog::onUseCameraDistance);
    connect(applyButton_, &QPushButton::clicked, this, &CalibrationDialog::onApply);
    connect(resetButton, &QPushButton::clicked, this, [this] {
        result_ = domain::ScaleCalibration{};  // sin escala
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    showResult();
}

void CalibrationDialog::onPointPicked(const cv::Point2f& imagePoint) {
    if (!pointA_.has_value() || pointB_.has_value()) {
        pointA_ = imagePoint;  // primer clic (o reinicio tras una pareja)
        pointB_.reset();
        measuredPx_ = 0.0;
        measuredLabel_->setText(tr("Distancia marcada: primer punto listo, falta el segundo"));
    } else {
        pointB_ = imagePoint;
        measuredPx_ = cv::norm(*pointB_ - *pointA_);
        measuredLabel_->setText(tr("Distancia marcada: %1 px").arg(measuredPx_, 0, 'f', 1));
        updateFromKnownLength();
    }

    // Visualizar los puntos/segmento con el overlay de resultados del canvas.
    inspection::ToolRunResult marker;
    marker.ok = true;
    if (pointA_.has_value()) {
        marker.overlayPoints.push_back(*pointA_);
    }
    if (pointB_.has_value()) {
        marker.overlayPoints.push_back(*pointB_);
        marker.overlaySegments.push_back(std::array<cv::Point2f, 2>{*pointA_, *pointB_});
    }
    canvas_->setResults({marker});
    canvas_->setPickMode(true);  // siempre listo para el siguiente clic
}

void CalibrationDialog::onKnownLengthChanged() {
    updateFromKnownLength();
}

void CalibrationDialog::updateFromKnownLength() {
    if (measuredPx_ <= 0.0) {
        return;
    }
    result_ = domain::calibrationFromKnownLength(measuredPx_, knownMm_->value(),
                                                 snapshot_.width(), fovDeg_->value());
    showResult();
}

void CalibrationDialog::onUseCameraDistance() {
    result_ = domain::calibrationFromCameraDistance(cameraDistMm_->value(),
                                                    fovDeg_->value(), snapshot_.width());
    showResult();
}

void CalibrationDialog::showResult() {
    applyButton_->setEnabled(result_.valid());
    if (!result_.valid()) {
        resultLabel_->setText(tr("Sin calibrar: las medidas seguirán en píxeles."));
        return;
    }
    resultLabel_->setText(tr("Escala: %1 mm/px\nDistancia de cámara: ~%2 mm\n"
                             "Ejemplo: 100 px = %3 mm")
                              .arg(result_.mmPerPixel, 0, 'f', 4)
                              .arg(result_.cameraDistanceMm, 0, 'f', 0)
                              .arg(result_.toMm(100.0), 0, 'f', 2));
}

void CalibrationDialog::onApply() {
    if (result_.valid()) {
        accept();
    }
}

}  // namespace pci::ui
