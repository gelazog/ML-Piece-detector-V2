#include "ui/calibration_dialog.h"

#include <iterator>

#include <QComboBox>
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

namespace {

// LAS UNIDADES EN LAS QUE SE PUEDE DAR LA REFERENCIA.
//
// El campo solo aceptaba milímetros, y quien tiene delante una regla en
// pulgadas o una marca de 5 cm tenía que convertir a mano — con lo que un
// error de conversión se convierte en un error de escala en TODAS las medidas
// de la pieza, y encima uno silencioso: las cotas salen, solo que mal.
//
// Se guarda la conversión a milímetros porque es lo que entiende el cálculo; el
// resto de la aplicación sigue trabajando igual.
struct KnownUnit {
    const char* label;
    double millimetres;
};

const KnownUnit kKnownUnits[] = {
    {"mm", 1.0},
    {"cm", 10.0},
    {"m", 1000.0},
    {"pulgadas", 25.4},
};

}  // namespace

CalibrationDialog::CalibrationDialog(const QImage& snapshot,
                                     domain::ScaleCalibration current, ScaleEntry last,
                                     inspection::LengthUnit unit, QWidget* parent)
    : QDialog(parent), snapshot_(snapshot), result_(current), unit_(unit) {
    setWindowTitle(tr("Calibrar la escala: cuántos milímetros mide un píxel"));
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
    // EN LA UNIDAD QUE TIENE ELEGIDA, no siempre en milímetros.
    //
    // Sale de una queja directa: «si está relacionado la opción de ver las
    // medidas en cm, y entras y lo ves en mm». Lo estaba y no lo estaba: la
    // escala se guarda en mm/px por dentro —eso no cambia— pero enseñársela en
    // milímetros a quien ha pedido centímetros le obliga a convertir de cabeza
    // justo en la pantalla donde una conversión mal hecha estropea TODAS las
    // cotas de la pieza.
    stateLabel->setText(current.valid()
                            ? tr("Escala actual: %1").arg(perPixelText(current.mmPerPixel))
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
    knownLength_ = new QDoubleSpinBox(methodA);
    knownLength_->setRange(0.001, 100000.0);
    knownLength_->setDecimals(3);
    knownLength_->setValue(last.knownLength > 0.0 ? last.knownLength : 100.0);
    knownLength_->setToolTip(
        tr("Lo que mide DE VERDAD la distancia que acabas de marcar con los dos\n"
           "clics. Se recuerda para la próxima vez: si el puesto tiene una regla\n"
           "fija, se escribe una sola vez."));
    knownUnit_ = new QComboBox(methodA);
    for (const auto& unit : kKnownUnits) {
        knownUnit_->addItem(QString::fromUtf8(unit.label));
    }
    knownUnit_->setCurrentIndex(
        last.unitIndex >= 0 && last.unitIndex < unitCount() ? last.unitIndex : 0);
    knownUnit_->setToolTip(
        tr("En qué unidad estás dando la longitud. El programa la pasa a\n"
           "milímetros por dentro; las medidas se siguen mostrando en la unidad\n"
           "que elijas en Medida ▸ Unidad de medida.\n\n"
           "Está aquí para que nadie tenga que convertir a mano: un error de\n"
           "conversión aquí sale como un error en TODAS las cotas de la pieza, y\n"
           "encima silencioso — las medidas salen, solo que mal."));
    auto* knownRow = new QHBoxLayout();
    knownRow->addWidget(knownLength_, 1);
    knownRow->addWidget(knownUnit_);
    formA->addRow(tr("Longitud real:"), knownRow);
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
    connect(knownUnit_, &QComboBox::currentIndexChanged, this,
            [this](int) { onKnownLengthChanged(); });
    connect(knownLength_, &QDoubleSpinBox::valueChanged, this,
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
    result_ = domain::calibrationFromKnownLength(measuredPx_, knownLengthMm(),
                                                 snapshot_.width(), fovDeg_->value());
    showResult();
}

void CalibrationDialog::onUseCameraDistance() {
    result_ = domain::calibrationFromCameraDistance(cameraDistMm_->value(),
                                                    fovDeg_->value(), snapshot_.width());
    showResult();
}

// La escala, rotulada en la unidad que el operador tiene elegida.
//
// Por dentro sigue siendo mm/px, que es lo que guarda la calibración y lo que
// usa todo lo demás; esto es solo cómo se lee. Se dan más decimales cuanto más
// pequeña es la unidad, porque el número también se hace más pequeño: en cm/px
// una escala típica es 0,0123 y con cuatro decimales se quedaría sin cifras
// significativas.
QString CalibrationDialog::perPixelText(double mmPerPixel) const {
    const inspection::UnitPick pick = inspection::pickLength(mmPerPixel, unit_);
    return QStringLiteral("%1 %2/px")
        .arg(pick.value, 0, 'f', pick.decimals + 4)
        .arg(QString::fromUtf8(pick.suffix));
}

void CalibrationDialog::showResult() {
    applyButton_->setEnabled(result_.valid());
    if (!result_.valid()) {
        resultLabel_->setText(tr("Sin calibrar: las medidas seguirán en píxeles."));
        return;
    }
    const inspection::UnitPick distance =
        inspection::pickLength(result_.cameraDistanceMm, unit_);
    const inspection::UnitPick example = inspection::pickLength(result_.toMm(100.0), unit_);
    resultLabel_->setText(tr("Escala: %1\nDistancia de cámara: ~%2 %3\n"
                             "Ejemplo: 100 px = %4 %5")
                              .arg(perPixelText(result_.mmPerPixel))
                              .arg(distance.value, 0, 'f', distance.decimals)
                              .arg(QString::fromUtf8(distance.suffix))
                              .arg(example.value, 0, 'f', example.decimals)
                              .arg(QString::fromUtf8(example.suffix)));
}

void CalibrationDialog::onApply() {
    if (result_.valid()) {
        accept();
    }
}

double CalibrationDialog::millimetresPerUnit(int unitIndex) {
    if (unitIndex < 0 || unitIndex >= unitCount()) {
        return 1.0;
    }
    return kKnownUnits[static_cast<std::size_t>(unitIndex)].millimetres;
}

int CalibrationDialog::unitCount() {
    return static_cast<int>(std::size(kKnownUnits));
}

double CalibrationDialog::knownLengthMm() const {
    if (knownLength_ == nullptr) {
        return 0.0;
    }
    const int index = knownUnit_ != nullptr ? knownUnit_->currentIndex() : 0;
    return knownLength_->value() * millimetresPerUnit(index);
}

ScaleEntry CalibrationDialog::lastEntry() const {
    ScaleEntry last;
    if (knownLength_ != nullptr) {
        last.knownLength = knownLength_->value();
    }
    if (knownUnit_ != nullptr) {
        last.unitIndex = knownUnit_->currentIndex();
    }
    return last;
}

}  // namespace pci::ui
