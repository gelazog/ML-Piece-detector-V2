#pragma once

#include <QDialog>

#include <functional>
#include <vector>

#include "inspection_editor/auto_measure.h"
#include "inspection_editor/measure_recipe.h"

class QCheckBox;
class QComboBox;
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
    // `mmPerPixel` es la escala con la que rotular los valores. Sin ella, la
    // tabla da píxeles y lo dice — que es lo correcto: inventar milímetros
    // sin calibrar sería la peor de las salidas.
    // QUÉ CLASES DE COTA PROPONER, elegidas por el operador.
    //
    // Sale de una petición de uso: poder elegir qué herramientas entran en la
    // medición automática. El proponedor ofrece hasta doce cotas de siete
    // clases, y quien solo mide diámetros acaba desmarcando nueve cada vez.
    //
    // Se pasa una función que VUELVE A PROPONER en vez de filtrar la lista que
    // ya hay, y la razón es el tope: el recorte a doce se aplica DESPUÉS de
    // filtrar. Escondiendo filas se quedaría con «tres diámetros», porque los
    // otros nueve huecos se los habrían comido cotas que el operador no quería.
    // Volviendo a proponer, los doce son de lo que pidió.
    //
    // Si no se pasa, el diálogo funciona como siempre y no enseña el filtro:
    // así el llamante que no puede reproponer no ofrece un control que mentiría.
    //
    // LO QUE SE LE PASA ES UNA RECETA ENTERA, no la lista de clases.
    //
    // Las dos cosas que el operador toca aquí —elegir una receta y marcar
    // casillas— son la MISMA: una receta no es más que un juego de casillas con
    // nombre. Con dos caminos distintos, elegir «Arandela» y marcar sus cuatro
    // casillas a mano darían resultados distintos el día que una receta llevara
    // algo más, y nadie sabría cuál manda.
    //
    // Y hace falta que la respuesta traiga el «no aplica»: la receta de la
    // tuerca sobre una arandela no devuelve cero cotas, devuelve un motivo. Sin
    // eso, el diálogo enseñaría una tabla vacía y el operador probaría recetas a
    // ver cuál entra.
    using Reproposer = std::function<RecipeResult(const MeasureRecipe&)>;

    AutoMeasureDialog(std::vector<AutoProposal> proposals, double mmPerPixel = 0.0,
                      QWidget* parent = nullptr, Reproposer reproposer = {});

    // La receta elegida ahora mismo, con las casillas que el operador haya
    // tocado. Es lo que hay que guardar en la pieza para que la próxima vez
    // salga ya puesta.
    [[nodiscard]] MeasureRecipe chosenRecipe() const;

    // Las propuestas que el operador dejó marcadas, en el orden en que se
    // mostraron. Vacío si canceló.
    [[nodiscard]] std::vector<AutoProposal> accepted() const;

private:
    void updateAcceptLabel();
    void fillTable();
    // Las clases marcadas ahora mismo. Vacío = todas, igual que en
    // `ProposeOptions`, para que las dos capas signifiquen lo mismo.
    [[nodiscard]] std::vector<ToolType> chosenTypes() const;
    // Vuelve a proponer con la receta que haya ahora y refleja el resultado:
    // la tabla, el motivo cuando no aplica, y el botón de aceptar.
    void reproposeWithCurrentRecipe();
    // Pone las casillas como diga la receta, sin disparar el reproponer: si
    // cada casilla reproprusiera al ponerla, elegir una receta lanzaría siete
    // barridos del contorno y el último ganaría.
    void applyRecipeToBoxes(const MeasureRecipe& recipe);

    std::vector<AutoProposal> proposals_;
    double mmPerPixel_ = 0.0;
    QTableWidget* table_ = nullptr;
    QPushButton* acceptButton_ = nullptr;
    QComboBox* recipeBox_ = nullptr;
    QLabel* recipeWhat_ = nullptr;
    QLabel* noticeLabel_ = nullptr;
    Reproposer reproposer_;
    std::vector<QCheckBox*> typeBoxes_;
    std::vector<ToolType> boxTypes_;
    // La receta base elegida en el desplegable. Las casillas la ajustan, y por
    // eso se guarda aparte: `chosenRecipe()` es ESTA con las casillas de ahora.
    MeasureRecipe base_;
};

}  // namespace pci::inspection
