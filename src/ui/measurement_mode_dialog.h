#pragma once

#include <QDialog>

#include "repositories/piece_repository.h"

class QCheckBox;
class QLabel;
class QRadioButton;

namespace pci::ui {

// Modo de medición de la pieza y, con él, el tablero de referencia con el que
// se mide (M2). Se usa en dos sitios: al registrar una pieza y desde
// Pieza ▸ Modo de medición…, para poder cambiarlo después sin volver a
// registrar.
class MeasurementModeDialog : public QDialog {
    Q_OBJECT

public:
    MeasurementModeDialog(const repositories::PieceMeasurement& current,
                          const QString& pieceName, QWidget* parent = nullptr);

    [[nodiscard]] repositories::PieceMeasurement measurement() const;

private:
    void syncBoardEnabled();  // el tablero solo se configura en modo Especial

    QRadioButton* realRadio_ = nullptr;
    QRadioButton* specialRadio_ = nullptr;
    QRadioButton* originPiece_ = nullptr;
    QRadioButton* originImage_ = nullptr;
    QRadioButton* originFixed_ = nullptr;
    QCheckBox* followAngle_ = nullptr;
    QLabel* warning_ = nullptr;  // aviso de la combinación que no mide posición
    // El punto fijado no se teclea aquí: se marca con un clic sobre la imagen
    // (Ver ▸ Origen del tablero ▸ Punto fijado a mano…). Se conserva tal cual.
    cv::Point2f fixedPoint_{0.0F, 0.0F};
};

}  // namespace pci::ui
