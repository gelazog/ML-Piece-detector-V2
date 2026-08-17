#pragma once

#include <QDialog>

#include "inspection_editor/piece_report.h"

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
    PieceReportDialog(inspection::PieceReport report, const QString& sourceLabel,
                      repositories::SettingsRepository* settings = nullptr,
                      QWidget* parent = nullptr);

    // Las cotas que el operador quiere convertir en herramientas vigiladas.
    // Vacío si no pulsó ese botón: medir y vigilar son dos decisiones, y unirlas
    // llenaría la plantilla de herramientas a cada consulta.
    [[nodiscard]] std::vector<inspection::AutoProposal> toWatch() const { return toWatch_; }

private slots:
    void onCopyClicked();
    void onExportClicked();
    void onWatchClicked();

private:
    inspection::PieceReport report_;
    std::vector<inspection::AutoProposal> toWatch_;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
};

}  // namespace pci::ui
