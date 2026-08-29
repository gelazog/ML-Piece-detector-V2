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
// Segunda tanda de peticiones, ya con el panel delante:
//
//   - «si presiona alguna medida, que la remarque más» → pulsar una fila
//     selecciona esa herramienta en la imagen;
//   - «un ojo para hacer visible o invisible las medidas, para saturar menos» →
//     cada fila decide si su cota se dibuja encima de la pieza;
//   - «que puedas borrar si quieres la medida, por si se satura de más»;
//   - «si hay más piezas, la opción de supervisar por piezas y que sea un
//     selectbox» → un desplegable elige la pieza, y esa elección es LA MISMA que
//     usan las flechas y el mosaico, no un estado aparte;
//   - «¿qué es OK a secas?» → la columna dice «Cumple» o «No cumple» **con el
//     margen que queda**, que es lo que contesta de verdad la pregunta.
//
// Este panel no toca nada: enseña y AVISA. Quien manda sobre las herramientas es
// la ventana, igual que con el interruptor del informe de pieza — un panel que
// borrara por su cuenta se saltaría el deshacer que ya existe.
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

    // Cuántas filas hay ahora mismo, para poder comprobarlo.
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
    void pieceChosen(int pieceIndex);

private:
    void rebuild();

    QComboBox* pieceBox_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* summary_ = nullptr;

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
