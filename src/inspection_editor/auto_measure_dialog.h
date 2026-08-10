#pragma once

#include <QDialog>

#include <vector>

#include "inspection_editor/auto_measure.h"

class QTableWidget;
class QLabel;
class QPushButton;

namespace pci::inspection {

// Revisión de las medidas propuestas automáticamente.
//
// Existe en vez de insertar las propuestas directamente porque insertar sin
// preguntar deja al operador borrando lo que no pidió: revisar una lista corta
// con su porqué y su medida cuesta menos que limpiar la lista de herramientas.
// Por eso cada fila muestra QUÉ mide, CUÁNTO da sobre esta pieza y POR QUÉ se
// propone — sin esos tres datos la revisión se convierte en aceptar todo.
class AutoMeasureDialog : public QDialog {
    Q_OBJECT

public:
    AutoMeasureDialog(std::vector<AutoProposal> proposals, QWidget* parent = nullptr);

    // Las propuestas que el operador dejó marcadas, en el orden en que se
    // mostraron. Vacío si canceló.
    [[nodiscard]] std::vector<AutoProposal> accepted() const;

private:
    void updateAcceptLabel();

    std::vector<AutoProposal> proposals_;
    QTableWidget* table_ = nullptr;
    QPushButton* acceptButton_ = nullptr;
};

}  // namespace pci::inspection
