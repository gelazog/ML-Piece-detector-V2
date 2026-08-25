#pragma once

#include <QDialog>

#include "inspection_editor/piece_report.h"

class QCheckBox;
class QLabel;
class QTableWidget;

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

private slots:
    void onCopyClicked();
    void onExportClicked();
    void onWatchClicked();

private:
    // La pestaña de siempre: qué mide la pieza, medido solo.
    [[nodiscard]] QWidget* buildMeasurementsTab();
    // La pestaña nueva: las herramientas del operador, con su lectura, su
    // veredicto y su interruptor.
    [[nodiscard]] QWidget* buildToolsTab();

    inspection::PieceReport report_;
    std::vector<inspection::AutoProposal> toWatch_;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    std::vector<DrawnTool> drawn_;
    std::vector<QCheckBox*> toolSwitches_;
    // El estado con el que llegaron, para saber cuáles cambió el operador.
    std::vector<bool> switchesAtStart_;
};

}  // namespace pci::ui
