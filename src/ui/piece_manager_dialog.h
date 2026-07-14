#pragma once

#include <QDialog>

#include <cstdint>

class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace pci::repositories {
class PieceRepository;
class ToolRepository;
}  // namespace pci::repositories

namespace pci::ui {

// Gestión de piezas registradas: renombrar, eliminar (con todo su historial),
// y ajustar manualmente la orientación del sistema de coordenadas (girarla
// hasta dejar el eje donde el usuario quiera; aplica en vivo, registro e
// inspección).
class PieceManagerDialog : public QDialog {
    Q_OBJECT

public:
    PieceManagerDialog(repositories::PieceRepository* pieces,
                       repositories::ToolRepository* tools, QWidget* parent = nullptr);

    // true si hubo cambios (la ventana principal debe recargar su lista).
    [[nodiscard]] bool changed() const { return changed_; }

private slots:
    void onSelectionChanged();
    void onRenameClicked();
    void onDeleteClicked();
    void onOffsetEdited();
    void onRotate90Clicked();

private:
    void reloadList(std::int64_t selectId = -1);
    [[nodiscard]] std::int64_t selectedPieceId() const;

    repositories::PieceRepository* pieces_;
    repositories::ToolRepository* tools_;
    QListWidget* list_ = nullptr;
    QLabel* thumbLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QPushButton* renameButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QDoubleSpinBox* offsetSpin_ = nullptr;
    QPushButton* rotateButton_ = nullptr;
    bool changed_ = false;
    bool syncing_ = false;
};

}  // namespace pci::ui
