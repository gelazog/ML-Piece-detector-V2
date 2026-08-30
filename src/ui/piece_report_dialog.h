#pragma once

#include <QDialog>

#include <optional>
#include <utility>

#include "inspection_editor/piece_report.h"

class QCheckBox;
class QLabel;
class QTableWidget;
class QTreeWidget;

namespace pci::repositories {
class SettingsRepository;
}

namespace pci::ui {

// «¿Cuánto mide esto?», contestado entero y de una vez.
//
// Es la pregunta que más se hace delante de una pieza, y hasta ahora la
// aplicación obligaba a un rodeo para responderla: abrir el editor de plantilla,
// pulsar «Medir automáticamente», revisar una lista de propuestas y aceptarlas
// como herramientas. Eso está bien para PREPARAR la vigilancia de una pieza en
// producción y es absurdo para mirar una pieza y querer sus cotas.
//
// La tabla va en dos bloques y la separación no es decorativa: arriba los HECHOS
// del contorno —perímetro, área, envolvente, agujeros— que no llevan tolerancia
// porque nadie ha declarado ninguna, y debajo las COTAS que sí pueden pasar a
// vigilarse. Mezclarlos invita a buscarle banda a un área.
class PieceReportDialog : public QDialog {
    Q_OBJECT

public:
    // LO QUE MIDEN LAS HERRAMIENTAS QUE EL OPERADOR TIENE DIBUJADAS.
    //
    // Hasta ahora este diálogo no las enseñaba: dibujabas cinco cotas, pulsabas
    // «Medir pieza» y veías hechos del contorno y propuestas automáticas, pero
    // ninguna de las tuyas. Para verlas había que inspeccionar, que además
    // guarda en el historial — dos cosas distintas mezcladas en un botón.
    struct DrawnTool {
        inspection::ToolConfig config;
        inspection::ToolRunResult result;
        // La medida YA ROTULADA con su unidad. Se formatea fuera porque quien
        // conoce la escala y la unidad elegida es la ventana; el diálogo no
        // tiene por qué aprenderse esa conversión para enseñar una tabla.
        std::string text;

        // TODO LO DEMÁS QUE ESTA MISMA FIGURA PUEDE MEDIR.
        //
        // Cinco clases de herramienta eligen una medida al dibujarse: la Región
        // entre seis (área, perímetro, solidez, circularidad, relación de
        // aspecto, agujeros), la Ranura entre tres, el Chaflán entre tres, el
        // Acuerdo y los Extremos entre dos. El operador escoge UNA y las otras
        // quedan invisibles, aunque salen de la misma figura y no cuestan un
        // trazo más: para ver el perímetro de la región que ya dibujaste había
        // que dibujar una segunda región encima.
        //
        // Petición de uso: «que hubiera como dos partes en lo de herramientas,
        // una de la herramienta en general y otra de todas las secciones-medidas
        // de esa herramienta». Esto es la segunda parte.
        struct OtherMeasure {
            std::string label;     // «Perímetro», «Circularidad»…
            int value = 0;         // el valor del enum, para `setMeasureChoice`
            std::string text;      // lo que da, ya con su unidad
            bool isTheOneItMeasures = false;  // la que la herramienta mide hoy
        };
        std::vector<OtherMeasure> alsoMeasures;
    };

    // Una medida hermana que el operador quiere pasar a vigilar: la MISMA figura
    // de `fromTool`, midiendo `measureValue` en vez de lo suyo.
    //
    // Se devuelve la elección y no una herramienta ya hecha porque construirla
    // pide la geometría y el sitio donde vive, y de eso sabe la ventana.
    struct MeasureToAdd {
        int fromTool = -1;  // índice en el `drawn` que se le pasó
        int measureValue = 0;
        std::string label;
    };

    PieceReportDialog(inspection::PieceReport report, const QString& sourceLabel,
                      repositories::SettingsRepository* settings = nullptr,
                      QWidget* parent = nullptr, std::vector<DrawnTool> drawn = {});

    // Las cotas que el operador quiere convertir en herramientas vigiladas.
    // Vacío si no pulsó ese botón: medir y vigilar son dos decisiones, y unirlas
    // llenaría la plantilla de herramientas a cada consulta.
    [[nodiscard]] std::vector<inspection::AutoProposal> toWatch() const { return toWatch_; }

    // Las herramientas cuyo interruptor cambió, para que la ventana las guarde.
    //
    // Se devuelven en vez de guardarlas aquí porque este diálogo no tiene —ni
    // debe tener— acceso al repositorio: enseña y pregunta, y quien manda sobre
    // los datos es la ventana.
    [[nodiscard]] std::vector<inspection::ToolConfig> toolsWithChangedState() const;

    // LA CLASE QUE EL OPERADOR QUIERE DIBUJAR A MANO, si pulsó «Dibujarla».
    //
    // Para las cotas que el programa no sabe colocar solo —una Rectitud, un
    // Chaflán: exigen señalar QUÉ tramo se mide— el camino honrado no es fingir
    // que sí puede, sino llevar al operador a trazarla. El diálogo no dibuja
    // nada: dice cuál, y la ventana la deja elegida en la paleta.
    [[nodiscard]] std::optional<inspection::ToolType> toolToDrawByHand() const {
        return toDrawByHand_;
    }

    // Las medidas hermanas marcadas para vigilar. Vacío si no marcó ninguna.
    [[nodiscard]] std::vector<MeasureToAdd> measuresToAdd() const;

private slots:
    void onCopyClicked();
    void onExportClicked();
    void onWatchClicked();

private:
    // La pestaña nueva: las herramientas del operador, con su lectura, su
    // veredicto y su interruptor.
    [[nodiscard]] QWidget* buildToolsTab();
    // EL CATÁLOGO: las 32 herramientas, y qué se puede hacer con cada una
    // SOBRE ESTA PIEZA.
    //
    // Petición de uso: «en medir pieza debería de haber otra sección con todas
    // las herramientas para usar/medir, aparte de la sección que te dice qué
    // herramientas se usaron y cuántas, y que se intenten agregar
    // automáticamente».
    //
    // Son tres preguntas distintas sobre la misma lista y por eso son tres
    // pestañas: qué mide la pieza sola, qué mido YO con lo que he dibujado, y
    // qué MÁS podría medir. La tercera no existía en ningún sitio: la paleta
    // enseña los iconos mientras dibujas, pero no dice cuáles de ellas sirven
    // para la pieza que tienes delante.
    [[nodiscard]] QWidget* buildCatalogueTab();

    inspection::PieceReport report_;
    std::vector<inspection::AutoProposal> toWatch_;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    std::vector<DrawnTool> drawn_;
    // Las casillas del catálogo, CON SU CLASE al lado. Emparejar por posición
    // ya se rompió una vez en este mismo diálogo —al agrupar por clase, apagar
    // «Ø» apagaba «alto»— así que aquí la pareja va explícita.
    std::vector<std::pair<inspection::ToolType, QCheckBox*>> useBoxes_;
    std::optional<inspection::ToolType> toDrawByHand_;
    // EL INTERRUPTOR, CON LA HERRAMIENTA A LA QUE PERTENECE.
    //
    // Antes esto era un vector de casillas y se emparejaba con `drawn_` POR
    // POSICION. Funciono mientras la pestana era una lista plana; al agrupar por
    // clase, las casillas se crean en otro orden y apagar una cota guardaba OTRA
    // —«Ø» apagaba «alto»— sin decir nada. Un emparejamiento implicito que
    // depende del orden de pintado es una bomba de relojeria.
    struct ToolSwitch {
        QCheckBox* box = nullptr;
        std::size_t tool = 0;  // indice en `drawn_`
    };
    std::vector<ToolSwitch> toolSwitches_;
    // Las casillas de las medidas hermanas, con a qué herramienta y a qué valor
    // pertenece cada una.
    struct SiblingBox {
        QCheckBox* box = nullptr;
        MeasureToAdd what;
    };
    std::vector<SiblingBox> siblingBoxes_;
    // El estado con el que llegaron, para saber cuáles cambió el operador.
    std::vector<bool> switchesAtStart_;
};

}  // namespace pci::ui
