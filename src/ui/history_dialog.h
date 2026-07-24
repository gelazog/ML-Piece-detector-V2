#pragma once

#include <QDialog>

#include <cstdint>

class QComboBox;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace pci::repositories {
class InspectionRepository;
class PieceRepository;
}

namespace pci::ui {

// Pantalla de historial de inspecciones (S1): tabla de las inspecciones
// recientes de una pieza (fecha, veredicto, similitud, versión de referencia),
// con selector de pieza, cantidad a mostrar y exportación a CSV. Solo lee.
class HistoryDialog : public QDialog {
    Q_OBJECT

public:
    HistoryDialog(repositories::InspectionRepository* inspections,
                  repositories::PieceRepository* pieces, std::int64_t initialPieceId,
                  QWidget* parent = nullptr);

private:
    void reloadPieces(std::int64_t select);
    void reload();
    void exportCsv();
    [[nodiscard]] std::int64_t currentPieceId() const;

    repositories::InspectionRepository* inspections_ = nullptr;
    repositories::PieceRepository* pieces_ = nullptr;
    QComboBox* pieceCombo_ = nullptr;
    QSpinBox* limitSpin_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
};

}  // namespace pci::ui
