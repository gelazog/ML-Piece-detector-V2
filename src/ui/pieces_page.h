#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QSpinBox;

namespace pci::ui {

// Página «Piezas» del panel Configurar (C5): cuántas piezas se esperan ver en
// la imagen.
//
// Hasta ahora la aplicación se quedaba con la pieza mayor y borraba el resto en
// silencio, así que una bandeja con cinco tornillos y otra con seis daban
// exactamente el mismo resultado. Con el número declarado, que falte una **es
// un NG por sí solo**, sin necesidad de tener ninguna herramienta dibujada.
//
// El número se guarda **con la pieza**, no en los ajustes de la máquina: "seis
// tornillos en bandeja" es una propiedad del trabajo.
class PiecesPage : public QWidget {
    Q_OBJECT

public:
    PiecesPage(int expectedPieces, QWidget* parent = nullptr);

    [[nodiscard]] int expectedPieces() const;

    // Cuántas se están viendo ahora mismo, para el botón «usar lo que veo» y
    // para que el operador compruebe de un vistazo si el número que puso es el
    // correcto antes de que salte un NG en producción.
    void setDetectedCount(int found);

signals:
    // El operador pidió rellenar el número con lo que se ve ahora.
    void useDetectedRequested();

private:
    void refreshStatus();

    QSpinBox* expected_ = nullptr;
    QPushButton* useDetected_ = nullptr;
    QLabel* status_ = nullptr;
    int detected_ = -1;  // -1 = todavía no se ha analizado nada
};

}  // namespace pci::ui
