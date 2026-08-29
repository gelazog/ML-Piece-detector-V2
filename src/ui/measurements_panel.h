#pragma once

#include <QWidget>

#include <string>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"

class QLabel;
class QTableWidget;

namespace pci::ui {

// LAS MEDIDAS EN VIVO, EN UNA TABLA QUE SE PUEDE LEER.
//
// Petición de uso: «falta la parte en donde te resume las medidas, la
// ventana/pestaña para verlas».
//
// Los números existían y se pintaban sobre el vídeo, encima de cada
// herramienta. Eso funciona con tres cotas y se rompe con catorce: las
// etiquetas se pisan —el lienzo tiene un buscador de huecos justamente por
// eso— y, con varias piezas, **solo se escriben los números de UNA**, porque
// catorce etiquetas por seis piezas serían ochenta y cuatro encima del vídeo.
// La decisión de pintar una sola es correcta para el vídeo, y deja al operador
// sin poder leer las demás en ningún sitio.
//
// Aquí se leen todas: qué mide cada herramienta de cada pieza, dentro de qué
// banda, y si cumple.
//
// Y CUANDO UNA NO MIDE, SE VE EL MOTIVO EN SU FILA. Es la otra mitad de la
// misma queja —«varias herramientas no muestran medidas»—: medido sobre las 32,
// ninguna se queda callada, todas explican por qué no dan número. Lo que
// faltaba era un sitio donde leer esa explicación sin tener que abrir la
// herramienta una por una.
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

    // Cuántas filas hay ahora mismo, para poder comprobarlo.
    [[nodiscard]] int rowCount() const;

private:
    QTableWidget* table_ = nullptr;
    QLabel* summary_ = nullptr;
};

}  // namespace pci::ui
