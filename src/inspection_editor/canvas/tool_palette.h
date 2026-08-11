#pragma once

#include <QWidget>

#include <optional>
#include <vector>

#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"

class QToolBox;
class QToolButton;

namespace pci::inspection {

// Paleta de herramientas agrupada por familias, compartida por las dos
// superficies que la necesitan.
//
// Existe por una medida concreta: la fila plana de la vista en vivo pedía
// ~1400 px de ancho mínimo en una ventana que arranca a 1100, y la columna del
// editor gastaba 440 px de alto. Con las veinte herramientas que quedan por
// añadir, las dos reventaban. Agrupar no es estética aquí: es la condición para
// poder seguir.
//
// Vive en `pci_editor`, del que ya depende `pci_ui`, así que la dependencia
// baja. Las dos ventanas comparten el widget y por tanto el orden, los iconos y
// las descripciones: escrito dos veces, acabarían divergiendo.
class ToolPalette : public QWidget {
    Q_OBJECT

public:
    enum class Shape {
        // Fila horizontal: un botón por familia con menú desplegable. Es lo que
        // cabe en la barra de la vista en vivo.
        Compact,
        // Acordeón vertical: una sección por familia. Es lo que cabe en la
        // columna del editor, donde hay alto pero no ancho.
        Accordion,
    };

    explicit ToolPalette(Shape shape, QWidget* parent = nullptr);

    // Herramienta activa; vacío = modo Mover/Elegir.
    [[nodiscard]] std::optional<ToolType> currentTool() const { return current_; }
    // Selecciona sin emitir la señal (para sincronizar desde fuera).
    void showSelection(std::optional<ToolType> type);
    // Selecciona Y avisa, como si el operador hubiera pulsado. Lo usan los
    // atajos de teclado.
    void activate(std::optional<ToolType> type);
    // Elige la primera herramienta de una familia. Es la mitad del atajo
    // "familia + dígito".
    void activateCategory(ToolCategory category);
    // La n-ésima herramienta de la familia activa (0 = la primera). Devuelve
    // false si esa familia no tiene tantas.
    bool activateInCurrentCategory(int index);
    [[nodiscard]] ToolCategory currentCategory() const { return currentCategory_; }

signals:
    // Herramienta elegida por el operador; `type` vacío = Mover/Elegir.
    void toolChosen(std::optional<pci::inspection::ToolType> type);

private:
    void buildCompact();
    void buildAccordion();
    void refreshButtons();

    Shape shape_;
    std::optional<ToolType> current_;
    ToolCategory currentCategory_ = ToolCategory::InLine;
    QToolButton* selectButton_ = nullptr;
    QToolBox* accordion_ = nullptr;
    struct FamilyEntry {
        ToolCategory category;
        QToolButton* button = nullptr;  // solo en Compact
        int accordionPage = -1;         // solo en Accordion
    };
    std::vector<FamilyEntry> families_;
    std::vector<std::pair<ToolType, QToolButton*>> toolButtons_;  // solo Accordion
};

}  // namespace pci::inspection
