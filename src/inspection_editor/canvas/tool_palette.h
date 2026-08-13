#pragma once

#include <QWidget>

#include <optional>
#include <vector>

#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"

class QGridLayout;
class QLabel;
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
        // Panel acoplable estilo Paint: franja de familias arriba, nombre de la
        // activa, y TODAS sus herramientas en rejilla debajo.
        //
        // Las dos formas anteriores comparten el mismo defecto de fondo: las
        // herramientas no se ven. Con `Compact` hay que abrir un menú —que tapa
        // el vídeo justo cuando quieres mirar dónde vas a dibujar— y con
        // `Accordion` cada herramienta gasta una fila entera de alto. Aquí se ve
        // la familia completa de un vistazo y se elige en un clic, no en dos.
        Panel,
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

    // Cuántas columnas y cuánto alto pide una rejilla de `toolCount` botones a
    // un ancho dado. Son la aritmética del panel expuesta a propósito: la
    // pregunta que hay que poder responder es «¿cabrá la familia más grande
    // cuando tenga el doble de herramientas?», y esperar a tenerlas para
    // averiguarlo es tarde.
    [[nodiscard]] static int gridColumnsFor(int width);
    [[nodiscard]] static int gridHeightFor(int toolCount, int width);

signals:
    // Herramienta elegida por el operador; `type` vacío = Mover/Elegir.
    void toolChosen(std::optional<pci::inspection::ToolType> type);

protected:
    // La rejilla no tiene layout de flujo en Qt, así que se recoloca a mano
    // cuando cambia el ancho. Para ≤10 botones por familia, un `QGridLayout`
    // que se rehace es mucho menos máquina que un FlowLayout.
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildCompact();
    void buildAccordion();
    void buildPanel();
    // Rellena la rejilla con las herramientas de la familia activa. Solo se
    // instancian las de esa familia: con 32 herramientas, crear los 32 botones
    // para enseñar 8 sería trabajo tirado en cada cambio de familia.
    void rebuildGrid();
    void relayoutGrid();
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
    std::vector<std::pair<ToolType, QToolButton*>> toolButtons_;  // Accordion y Panel

    // Solo en Panel.
    QLabel* familyTitle_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QWidget* gridHost_ = nullptr;
    int gridColumns_ = 0;
};

}  // namespace pci::inspection
