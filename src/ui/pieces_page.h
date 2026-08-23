#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QRadioButton;
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

    // AUTOMÁTICA O MANUAL, dicho con todas las letras.
    //
    // Antes esto era un número con un cero mágico: 0 significaba «no vigilar el
    // recuento» y cualquier otro valor, «deben ser exactamente estas». Funcionaba
    // y no se entendía — un campo numérico cuyo primer valor significa otra cosa
    // hay que descubrirlo probando.
    //
    // Y hacía falta separarlo por algo más que claridad: con una sola pieza en la
    // mesa, la aplicación se empeñaba en enumerar, y cualquier sombra o reflejo
    // que pasara el filtro de área salía contado como una segunda pieza. Diciendo
    // «manual, una pieza» el programa deja de enumerar y mide la mayor, que es lo
    // que el operador quería decir.
    enum class CountMode { Automatic, Manual };
    [[nodiscard]] CountMode countMode() const;

    // Cuántas piezas se exigen. 0 = no vigilar (modo automático), que es como se
    // guardaba antes y se sigue guardando: el modo es una forma de enseñarlo, no
    // un dato nuevo que pueda desincronizarse del que ya había.
    [[nodiscard]] int expectedPieces() const;

    // Cuántas se están viendo ahora mismo, para el botón «usar lo que veo» y
    // para que el operador compruebe de un vistazo si el número que puso es el
    // correcto antes de que salte un NG en producción.
    void setDetectedCount(int found);

    // Rellena el campo y pasa a manual.
    //
    // No existía, y por eso el botón «Usar lo que se ve ahora» no hacía nada: su
    // manejador llamaba a `setDetectedCount`, que solo refresca el texto de
    // estado. El botón prometía poner el número en el campo y el campo no se
    // movía.
    void setExpectedPieces(int expected);

signals:
    // El operador pidió rellenar el número con lo que se ve ahora.
    void useDetectedRequested();

private:
    void refreshStatus();

    QRadioButton* automatic_ = nullptr;
    QRadioButton* manual_ = nullptr;
    QSpinBox* expected_ = nullptr;
    QPushButton* useDetected_ = nullptr;
    QLabel* status_ = nullptr;
    int detected_ = -1;  // -1 = todavía no se ha analizado nada
};

}  // namespace pci::ui
