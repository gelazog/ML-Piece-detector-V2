#pragma once

#include <QDialog>
#include <QImage>

#include <cstdint>
#include <string>
#include <vector>

#include "domain/calibration.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "inspection_editor/tools/undo_stack.h"
#include "vision/types.h"

class QButtonGroup;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace pci::repositories {
class ToolRepository;
}

namespace pci::inspection {

// Editor de plantilla de inspección (estilo VisionMaster): dibuja herramientas
// sobre la imagen de referencia, ajusta tolerancias, prueba sobre la propia
// imagen y guarda en la BD ancladas al fixture de la pieza.
class EditorWindow : public QDialog {
    Q_OBJECT

public:
    // repo puede ser nullptr (BD no disponible): el editor funciona pero el
    // botón Guardar queda deshabilitado. calibration inválida = medidas en px.
    EditorWindow(const QImage& reference, const vision::Fixture& fixture,
                 std::int64_t pieceId, repositories::ToolRepository* repo,
                 domain::ScaleCalibration calibration = {},
                 const std::string& templateName = "principal", QWidget* parent = nullptr);

private slots:
    void onToolCreated(const pci::inspection::ToolGeometry& geometry);
    void onCanvasSelection(int index);
    void onListRowChanged(int row);
    void onPanelEdited();
    void onDeleteClicked();
    void onTestClicked();
    void onSaveClicked();

private:
    void loadExistingTools();
    // Añade una herramienta a partir de config+geometría (nombre nuevo,
    // desplazada `offset` en coords de pieza) y la selecciona. Base común de
    // duplicar (Ctrl+D) y pegar (Ctrl+V).
    void addToolCopy(const ToolConfig& config, const ToolGeometry& geometry,
                     const cv::Point2f& offset);
    void duplicateSelected();  // Ctrl+D
    void copySelected();       // Ctrl+C
    void pasteClipboard();     // Ctrl+V
    void refreshList();
    void syncPanelFromSelection();
    void commitUndoState();
    void applyUndoRedo(bool redo);
    [[nodiscard]] int listRowToToolIndex(int row) const;
    [[nodiscard]] std::vector<ToolConfig> activeConfigs() const;

    EditorCanvas* canvas_ = nullptr;
    QButtonGroup* modeGroup_ = nullptr;
    QListWidget* list_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QDoubleSpinBox* tolMin_ = nullptr;
    QDoubleSpinBox* tolMax_ = nullptr;
    QLabel* paramLabel_ = nullptr;   // parámetro de muestreo según el tipo
    QSpinBox* paramSpin_ = nullptr;  // (banda, rayos, escaneos, área mínima)
    QLabel* tolMmLabel_ = nullptr;   // equivalente en mm de las tolerancias
    QPushButton* deleteButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QImage reference_;
    vision::Fixture fixture_;
    std::int64_t pieceId_ = -1;
    repositories::ToolRepository* repo_ = nullptr;
    domain::ScaleCalibration calibration_;
    std::string templateName_ = "principal";

    std::vector<EditedTool> tools_;
    std::vector<EditedTool> stableTools_;
    UndoStack<std::vector<EditedTool>> undoStack_;
    int nameCounter_ = 0;
    bool syncing_ = false;  // evita bucles señal<->panel
};

}  // namespace pci::inspection
