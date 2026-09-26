#pragma once

#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"

class QComboBox;
class QLabel;
class QTableWidget;

namespace pci::ui {

// LAS MEDIDAS EN VIVO, EN UNA TABLA QUE SE PUEDE LEER Y TOCAR.
//
// Primera petición: «falta la parte en donde te resume las medidas, la
// ventana/pestaña para verlas». Los números existían y se pintaban sobre el
// vídeo; eso funciona con tres cotas y se rompe con catorce —las etiquetas se
// pisan— y con varias piezas se rompe del todo, porque el lienzo escribe los
// números de UNA sola pieza.
//
// SEGUNDA QUEJA, ya con la tabla delante: «el panel de mediciones está
// confuso». Con «Todas» la misma cota salía repetida una vez por pieza —15
// filas para 5 cotas y 3 piezas—, el ojo y la ✕ vivían en cada una de esas
// filas aunque actuaran sobre la HERRAMIENTA entera (borrar desde la fila de
// la pieza 3 borraba la cota en las tres), la banda ocupaba media fila con la
// unidad repetida tres veces, y el resumen de abajo no decía qué pieza fallaba
// ni por qué cota. Este fichero es el rediseño: UNA fila por cota, nunca por
// pieza×cota, con el ojo y la ✕ una sola vez, y un veredicto arriba que nombra
// la pieza y la cota que falla en vez de contar OK/NG a secas.
//
// La API pública no cambia —`toolChosen`, `overlayVisibilityChanged`,
// `deleteRequested`, `pieceChosen`, `setResults`, `setChosenPiece`,
// `rowCount`— porque la ventana ya la usa y tocarla sería un cambio aparte del
// que se pidió. Lo que cambia es la forma de la tabla, no quién manda sobre
// las herramientas: este panel sigue sin borrar ni ocultar por su cuenta,
// avisa y deja que la ventana lo haga, que es la que tiene el deshacer.
class MeasurementsPanel : public QWidget {
    Q_OBJECT

public:
    explicit MeasurementsPanel(QWidget* parent = nullptr);

    // Las medidas de este análisis. `mmPerPixel` y `unit` se pasan para que la
    // tabla rotule EXACTAMENTE igual que las etiquetas del vídeo: las dos usan
    // `formatMeasure`, que es el único sitio donde se decide cómo se escribe una
    // medida. Cuatro pantallas tuvieron su propia regla una vez y las cuatro se
    // equivocaban igual.
    void setResults(const std::vector<inspection::ToolRunResult>& results,
                    const std::vector<inspection::ToolConfig>& configs, double mmPerPixel,
                    inspection::LengthUnit unit);

    // Qué pieza se está mirando (0 = la primera). Se pone desde fuera para que
    // el desplegable siga a las flechas y al mosaico en vez de competir con
    // ellos: es una sola elección con tres mandos.
    void setChosenPiece(int pieceIndex);

    // Cuántas filas hay ahora mismo: una por COTA (herramienta), no una por
    // pieza×cota. Con tres piezas y cinco cotas esto vale 5, no 15 — es
    // precisamente lo que este rediseño vino a arreglar.
    [[nodiscard]] int rowCount() const;

signals:
    // El operador ha pulsado una fila: esa herramienta pasa a estar seleccionada
    // —y remarcada— sobre la imagen.
    void toolChosen(std::int64_t toolId);
    // El ojo de una fila. `visible` falso = esa cota deja de dibujarse encima de
    // la pieza, pero se sigue midiendo y sigue en la tabla: es una decisión de
    // vista, no de medición.
    void overlayVisibilityChanged(std::int64_t toolId, bool visible);
    // Borrar esa herramienta. El panel no la borra: lo pide.
    void deleteRequested(std::int64_t toolId);
    // Elegir qué pieza se supervisa, con el mismo significado que las flechas.
    // En modo «Todas» también se dispara al pulsar la celda de una pieza
    // concreta dentro de una fila.
    void pieceChosen(int pieceIndex);

private:
    void rebuild();

    QComboBox* pieceBox_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* verdict_ = nullptr;

    std::vector<inspection::ToolRunResult> results_;
    std::vector<inspection::ToolConfig> configs_;
    double mmPerPixel_ = 0.0;
    inspection::LengthUnit unit_ = inspection::LengthUnit::Auto;
    // Qué cotas están ocultas en la imagen, por id de herramienta. Vive aquí
    // porque es una decisión de esta vista; la ventana la recibe por señal y la
    // aplica al dibujar.
    std::vector<std::int64_t> hidden_;
    // -1 = TODAS, y ese es el valor de partida: con una sola pieza da igual, y
    // con seis es lo que se quiere ver al abrir. Empezar en la pieza 1 escondía
    // las otras cinco sin que nadie lo hubiera pedido.
    int chosenPiece_ = -1;
};

}  // namespace pci::ui
