#pragma once

#include <QWidget>

#include <optional>
#include <vector>

#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"

class QGridLayout;
class QLabel;
class QToolButton;

namespace pci::inspection {

// Paleta de herramientas agrupada por familias, compartida por las dos
// superficies que la necesitan: el dock de la ventana principal y la columna
// del editor.
//
// Hubo tres formas —una fila con menús, un acordeón y este panel— y ahora hay
// una. Mantener tres divergen: ya pasó con los botones antes de que la paleta
// se compartiera, y no hay razón para volver a pagarlo ahora que el panel cabe
// en los dos sitios.
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
    explicit ToolPalette(QWidget* parent = nullptr);

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
    // Cómo se activa una herramienta con el teclado, en texto («Ctrl+2, luego
    // 1»). Se calcula de las mismas listas que generan los atajos, así que no
    // puede quedarse contando otra cosa que la que hace la tecla.
    [[nodiscard]] static QString shortcutHint(ToolType type);

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
    // Los botones de la rejilla no tienen texto: el nombre y la explicación
    // viven en la línea de ayuda, y para saber cuál se está señalando hace
    // falta ver entrar y salir el ratón.
    bool eventFilter(QObject* watched, QEvent* event) override;

    // La rejilla no tiene layout de flujo en Qt, así que se recoloca a mano
    // cuando cambia el ancho. Para ≤10 botones por familia, un `QGridLayout`
    // que se rehace es mucho menos máquina que un FlowLayout.
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildPanel();
    // Rellena la rejilla con las herramientas de la familia activa. Solo se
    // instancian las de esa familia: con 32 herramientas, crear los 32 botones
    // para enseñar 8 sería trabajo tirado en cada cambio de familia.
    void rebuildGrid();
    void relayoutGrid();
    void refreshButtons();
    // Qué dice la línea de ayuda ahora mismo: lo señalado con el ratón si hay
    // algo, si no lo seleccionado, y si no hay nada, por dónde empezar.
    void updateHelpLine();

    std::optional<ToolType> current_;
    ToolCategory currentCategory_ = ToolCategory::InLine;
    QToolButton* selectButton_ = nullptr;
    struct FamilyEntry {
        ToolCategory category;
        QToolButton* button = nullptr;
    };
    std::vector<FamilyEntry> families_;
    std::vector<std::pair<ToolType, QToolButton*>> toolButtons_;
    QLabel* familyTitle_ = nullptr;
    QLabel* helpName_ = nullptr;
    QLabel* helpShortcut_ = nullptr;
    QLabel* helpText_ = nullptr;
    std::optional<ToolType> hovered_;
    QGridLayout* grid_ = nullptr;
    QWidget* gridHost_ = nullptr;
    int gridColumns_ = 0;
};

}  // namespace pci::inspection
