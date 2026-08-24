#pragma once

#include <QDialog>
#include <QImage>

#include <opencv2/core.hpp>

#include <optional>

#include "domain/calibration.h"
#include "inspection_editor/execution/tool_executor.h"

class QComboBox;
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
// LO QUE SE ESCRIBIÓ LA VEZ ANTERIOR.
//
// Va fuera de la clase por un detalle del lenguaje: un inicializador de miembro
// por defecto no se puede usar como argumento por defecto dentro de la misma
// clase, y aquí el diálogo quiere poder abrirse sin pasar nada.
struct ScaleEntry {
    double knownLength = 100.0;
    int unitIndex = 0;  // índice en la lista de unidades del diálogo
};

class CalibrationDialog : public QDialog {
    Q_OBJECT

public:
    // LO QUE SE ESCRIBIÓ LA VEZ ANTERIOR.
    //
    // La longitud de referencia es lo Único que hay que teclear cada vez que se
    // calibra —y era lo Único que no se recordaba: el campo volvía a 100 mm
    // aunque la regla del puesto midiera 6 pulgadas. La distancia de cámara y
    // el FOV sí se recuperaban, lo que lo hacía aún más raro.
    //
    // No va dentro de `ScaleCalibration` porque no es parte de la calibración:
    // es la comodidad de quien la hace. Vive en los ajustes de la máquina.
    // `unit` es la unidad que el operador tiene elegida en Medida ▸ Unidad de
    // medida. El diálogo enseña sus resultados EN ESA, y no siempre en
    // milímetros como hacía antes: elegir centímetros y que la ventana de
    // calibrar te hable en milímetros es la incoherencia que se vino a quitar.
    CalibrationDialog(const QImage& snapshot, domain::ScaleCalibration current,
                      ScaleEntry last = {},
                      inspection::LengthUnit unit = inspection::LengthUnit::Auto,
                      QWidget* parent = nullptr);

    [[nodiscard]] const domain::ScaleCalibration& calibration() const { return result_; }
    // Para que la ventana lo guarde y la próxima vez el campo venga puesto.
    [[nodiscard]] ScaleEntry lastEntry() const;

    // Cuántos milímetros mide una unidad de la lista. Expuesto para poder
    // comprobarlo sin abrir el diálogo.
    [[nodiscard]] static double millimetresPerUnit(int unitIndex);
    [[nodiscard]] static int unitCount();

    // LA LONGITUD ESCRITA, EN MILÍMETROS — que es lo que entiende el cálculo.
    //
    // Es pública para poder comprobarla: que exista una tabla de conversión no
    // demuestra que el cálculo la use, y ese es justo el fallo que importa. Una
    // pulgada tomada como un milímetro no da un error visible: da cotas bien
    // formateadas y veinticinco veces equivocadas.
    [[nodiscard]] double knownLengthMm() const;

private slots:
    void onPointPicked(const cv::Point2f& imagePoint);
    void onKnownLengthChanged();
    void onUseCameraDistance();
    void onApply();

private:
    void updateFromKnownLength();
    void showResult();
    // La escala rotulada en la unidad elegida. Por dentro sigue siendo mm/px.
    [[nodiscard]] QString perPixelText(double mmPerPixel) const;

    inspection::EditorCanvas* canvas_ = nullptr;
    QLabel* measuredLabel_ = nullptr;
    QDoubleSpinBox* knownLength_ = nullptr;
    QComboBox* knownUnit_ = nullptr;
    QDoubleSpinBox* fovDeg_ = nullptr;
    QDoubleSpinBox* cameraDistMm_ = nullptr;
    QLabel* resultLabel_ = nullptr;
    QPushButton* applyButton_ = nullptr;

    QImage snapshot_;
    std::optional<cv::Point2f> pointA_;
    std::optional<cv::Point2f> pointB_;
    double measuredPx_ = 0.0;
    domain::ScaleCalibration result_;
    inspection::LengthUnit unit_ = inspection::LengthUnit::Auto;
};

}  // namespace pci::ui
