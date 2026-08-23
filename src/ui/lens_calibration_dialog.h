#pragma once

#include <QDialog>
#include <QImage>

#include <optional>
#include <vector>

#include "vision/lens_calibration.h"

class QLabel;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;

namespace pci::ui {

// Asistente para calibrar la lente con un tablero de ajedrez impreso.
//
// LO QUE ESTE ASISTENTE TIENE QUE CONSEGUIR, y no es «recoger unas fotos»: que
// las tomas lleguen a las ESQUINAS del encuadre.
//
// Está medido y es tajante (`tests/test_lens_distortion.cpp`). Con doce tomas
// apiñadas alrededor del centro, la calibración sale con un aspecto estupendo
// —error de reproyección 0,404 px, indistinguible del de una buena— y deja las
// medidas del borde un 34,88 % desviadas: dos veces y media PEOR que no corregir
// nada, que son 14,01 %. Con esas mismas doce tomas repartidas por los rincones,
// el residuo baja al 0,11 %.
//
// O sea que la diferencia entre una calibración que arregla las medidas y una
// que las estropea no está en el algoritmo: está en dónde puso el operador el
// tablero. Por eso la mitad de esta ventana es una rejilla que dice qué zonas
// faltan, y por eso `calibrateLens` se niega a devolver un modelo mal cubierto
// en vez de devolverlo con un aviso.
class LensCalibrationDialog : public QDialog {
    Q_OBJECT

public:
    explicit LensCalibrationDialog(QWidget* parent = nullptr);

    // Alimentar el asistente con lo que ve la cámara. Devuelve si se encontró el
    // tablero entero en este fotograma.
    //
    // Es también la puerta por la que entran las pruebas: sin ella, este flujo
    // solo se podría comprobar con una cámara y un tablero impreso delante, o
    // sea nunca.
    bool offerFrame(const QImage& frame);

    // Guardar la última toma ofrecida. Falso si no había tablero que guardar.
    bool captureCurrent();

    [[nodiscard]] int viewCount() const { return static_cast<int>(views_.size()); }
    [[nodiscard]] vision::BoardCoverage coverage() const;
    [[nodiscard]] const std::optional<vision::LensCalibration>& result() const {
        return result_;
    }
    // Intenta calibrar con lo recogido. Devuelve el error si no se puede, para
    // que las pruebas puedan comprobar que se dice el motivo.
    [[nodiscard]] QString tryCalibrate();
    [[nodiscard]] vision::BoardSpec boardSpec() const;

private:
    void refreshState();
    void showPreview(const QImage& frame, bool found);

    std::vector<vision::BoardView> views_;
    std::optional<vision::BoardView> pending_;
    QImage pendingFrame_;
    std::optional<vision::LensCalibration> result_;

    QLabel* preview_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* advice_ = nullptr;
    QLabel* cells_[9] = {};
    QSpinBox* innerCols_ = nullptr;
    QSpinBox* innerRows_ = nullptr;
    QDoubleSpinBox* squareMm_ = nullptr;
    QPushButton* capture_ = nullptr;
    QPushButton* calibrate_ = nullptr;
    QPushButton* forget_ = nullptr;
};

}  // namespace pci::ui
