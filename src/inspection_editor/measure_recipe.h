#pragma once

#include <string>
#include <vector>

#include "inspection_editor/auto_measure.h"
#include "vision/shape_class.h"

namespace pci::inspection {

// RECETAS DE MEDICIÓN: qué medir en un engranaje, en una arandela, en una pieza
// rectangular.
//
// Petición de uso: «un conjunto personalizado de reglas para algunas piezas
// específicas —engranajes, círculos, piezas cuadradas, rectangulares— para
// tomar de mejor manera las medidas».
//
// El proponedor YA mira la figura: a un disco le ofrece diámetro y redondez, a
// un polígono sus lados y sus ángulos, y no le ofrece redondeos a una pieza de
// esquinas vivas. Lo que faltaba es lo de arriba: poder decir «para MIS
// engranajes quiero esto y solo esto», guardarlo con un nombre y no volver a
// elegirlo pieza por pieza. El operador podía marcar las clases de cota en el
// diálogo, pero esa elección se perdía en cuanto lo cerraba.
//
// UNA RECETA NO INVENTA COTAS, y esto es lo que la separa de un simple filtro.
// Elegir «Engranaje» sobre una arandela no puede sacar un módulo de una pieza
// sin dientes: la receta dice a qué familia se aplica y, cuando no encaja, se
// dice en vez de proponer. Es la misma regla que ya gobierna el proponedor —lo
// que se propone tiene que existir en la pieza— y la razón por la que aquí no
// hay un modo «forzar».
//
// Las recetas son PLANTILLAS, no ajustes escondidos: lo único que hacen es
// rellenar `ProposeOptions`. Cualquier cosa que una receta pueda hacer, se
// puede hacer a mano marcando casillas; lo que aporta es que se recuerda y que
// tiene nombre.

// A qué clase de pieza se le puede aplicar una receta.
//
// No es la lista de `ShapeKind`: es lo que el operador reconoce en su taller.
// «Cuadrada o rectangular» son la misma familia para esto —cuatro caras y
// cuatro esquinas, y la receta es idéntica— y una tuerca hexagonal es su propia
// familia aunque para el clasificador sea un polígono más.
enum class PieceFamily {
    Any,        // cualquiera: la receta de siempre
    Round,      // disco macizo
    Ring,       // arandela: exterior, interior y concentricidad
    FourSided,  // cuadrada o rectangular
    Hexagonal,  // tuerca: entrecaras y recuento de caras
    Gear,       // rueda dentada
};

[[nodiscard]] const char* familyName(PieceFamily family);

// La familia como CLAVE ESTABLE, para guardarla.
//
// No se guarda el número del enum: reordenar `PieceFamily` —añadir «tuerca
// cuadrada» en medio, por ejemplo— convertiría en silencio todas las recetas
// guardadas de una familia en recetas de otra, y el operador se encontraría su
// receta de arandelas negándose a aplicarse a una arandela. Una clave de texto
// no se puede reordenar.
[[nodiscard]] const char* familyKey(PieceFamily family);
[[nodiscard]] PieceFamily familyFromKey(const std::string& key);

// Las clases de cota de una receta, como texto y de vuelta.
//
// Se usan los NOMBRES de `toolTypeName`, que son los mismos con los que se
// guarda una herramienta en la base: una clase que se renombre rompe las dos
// cosas a la vez y se entera todo el mundo, en vez de dejar recetas apuntando a
// clases que ya no existen mientras las herramientas siguen bien.
//
// Un nombre desconocido se SALTA en vez de tirar la receta entera: si una
// versión futura quita una clase, la receta guardada sigue sirviendo para las
// demás. Devolver un error dejaría al operador sin su receta por una clase que
// ya no le importa.
[[nodiscard]] std::string typesToText(const std::vector<ToolType>& types);
[[nodiscard]] std::vector<ToolType> typesFromText(const std::string& text);

struct MeasureRecipe {
    std::string name;                    // «Engranaje», «Arandela»…
    std::string what;                    // qué trae y por qué esas cotas
    PieceFamily family = PieceFamily::Any;
    ProposeOptions options;
};

// Las recetas que vienen de fábrica, en el orden en que conviene enseñarlas.
//
// Viven aquí, al lado de quien las aplica, por lo mismo que `proposableTypes()`:
// para que añadir una receta y olvidarse de la interfaz no sea posible.
[[nodiscard]] const std::vector<MeasureRecipe>& factoryRecipes();

// La receta con ese nombre, o `nullptr`. El nombre es la clave con la que se
// guarda en la pieza, así que renombrar una receta de fábrica rompería las
// piezas que la usaban — por eso los nombres son parte del contrato.
[[nodiscard]] const MeasureRecipe* recipeNamed(const std::string& name);

// La familia a la que pertenece una figura reconocida.
//
// El engranaje NO sale de aquí: para el clasificador una rueda dentada es
// «irregular» —115 lados rectos en `engranaje-1.png`—, así que quien decide si
// una pieza es un engranaje es la herramienta de Engranaje consiguiendo medirla,
// no la clase. Devolver `Gear` por adivinanza sería fingir una capacidad que no
// existe.
[[nodiscard]] PieceFamily familyOf(const vision::ShapeClass& shape);

struct RecipeResult {
    // Si la receta se puede aplicar a esta pieza. Falso NO es un error: es la
    // respuesta a «¿qué mido en esto con esta receta?» cuando la respuesta
    // honrada es «esta receta no es para esta pieza».
    bool applies = false;
    // Qué se reconoció y, si no aplica, por qué. Va en una frase porque acaba
    // en pantalla: «la receta Tuerca hexagonal pide una pieza de seis caras y
    // esta se ha reconocido como arandela».
    std::string why;
    std::vector<AutoProposal> proposals;
    // Cuántas se quedaron fuera por el tope de la receta. Descartarlas en
    // silencio deja al operador creyendo que la pieza no tenía más.
    int dropped = 0;
};

// Propone según una receta. Es `proposeTools` con dos cosas encima: comprueba
// que la receta va con la pieza y explica el resultado con palabras.
[[nodiscard]] RecipeResult proposeWithRecipe(const cv::Mat& gray, const cv::Mat& mask,
                                             const vision::Fixture& fixture,
                                             const MeasureRecipe& recipe, double mmPerPixel);

}  // namespace pci::inspection
