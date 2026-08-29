#include "inspection_editor/measure_recipe.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace pci::inspection {
namespace {

// Las clases de cota de cada receta.
//
// Lo que NO lleva una receta importa tanto como lo que lleva: una arandela con
// «Ángulo» marcado acaba recibiendo los ángulos de un octógono inventado sobre
// su borde, y un engranaje con «Polígono» recibe un recuento de lados que no
// son lados sino dientes leídos como tales. Por eso cada lista se escribe
// entera y no como «todas menos una».
// Y SE GUARDAN EN EL ORDEN DE `proposableTypes()`, no en el que se escriban
// aquí. El diálogo reconstruye la lista recorriendo sus casillas, o sea en ese
// orden; si la receta las guardara en otro, la receta elegida y la receta
// devuelta serían dos objetos distintos con el mismo significado — y cualquier
// comparación entre ellas (guardarla en la pieza, saber si el operador la ha
// ajustado) diría que cambió cuando no cambió nada.
ProposeOptions optionsWith(const std::vector<ToolType>& types) {
    ProposeOptions options;
    for (const ToolType type : proposableTypes()) {
        if (std::find(types.begin(), types.end(), type) != types.end()) {
            options.allowedTypes.push_back(type);
        }
    }
    return options;
}

}  // namespace

const char* familyName(PieceFamily family) {
    switch (family) {
        case PieceFamily::Any: return "cualquier pieza";
        case PieceFamily::Round: return "pieza redonda";
        case PieceFamily::Ring: return "arandela";
        case PieceFamily::FourSided: return "pieza de cuatro caras";
        case PieceFamily::Hexagonal: return "tuerca hexagonal";
        case PieceFamily::Gear: return "rueda dentada";
    }
    return "pieza";
}

const std::vector<MeasureRecipe>& factoryRecipes() {
    static const std::vector<MeasureRecipe> kRecipes = [] {
        std::vector<MeasureRecipe> recipes;

        // La de siempre, la primera: sin receta el proponedor ofrece de todo, y
        // esa sigue siendo la opción por defecto. Una receta es para acotar,
        // nunca para que el que no la use reciba menos.
        recipes.push_back({"Todas las cotas",
                           "Lo que la aplicación sabe proponer sobre cualquier pieza, sin "
                           "acotar. Es lo que hay sin receta.",
                           PieceFamily::Any, ProposeOptions{}});

        // REDONDA. Diámetro y redondez, y el área/perímetro que vigilan la
        // pieza entera. Sin ángulos ni lados: un disco no los tiene, y lo que
        // salga de ahí es el ajuste poligonal de una curva.
        recipes.push_back({"Pieza redonda",
                           "Diámetro, redondez y el área con su perímetro. Sin lados ni "
                           "ángulos: en una pieza redonda esos números salen de partir una "
                           "curva en tramos, y cambian con el encuadre.",
                           PieceFamily::Round,
                           optionsWith({ToolType::Circle, ToolType::Roundness,
                                        ToolType::Arc, ToolType::Region})});

        // ARANDELA. Lo mismo más el agujero, que es la cota que la define.
        recipes.push_back({"Arandela",
                           "Diámetro exterior e interior, redondez y área. El agujero es la "
                           "cota que la define: una arandela con el interior fuera de banda "
                           "no entra en el tornillo aunque el exterior esté perfecto.",
                           PieceFamily::Ring,
                           optionsWith({ToolType::Circle, ToolType::Roundness,
                                        ToolType::Arc, ToolType::Region})});

        // CUADRADA O RECTANGULAR. Largo, ancho, las cuatro esquinas y el área.
        // El calibre entra porque en una pieza de caras paralelas es la cota
        // que mide el espesor entre las dos, que es lo que pide un plano.
        recipes.push_back({"Cuadrada o rectangular",
                           "Largo, ancho, el espesor entre caras enfrentadas y los cuatro "
                           "ángulos —que son los que dicen si la pieza está a escuadra—, "
                           "más el área.",
                           PieceFamily::FourSided,
                           optionsWith({ToolType::Ruler, ToolType::Caliper, ToolType::Angle,
                                        ToolType::Polygon, ToolType::Region})});

        // TUERCA HEXAGONAL. El entrecaras es la cota del plano; el recuento de
        // caras vigila que no falte ni sobre una, que es otra avería distinta.
        recipes.push_back({"Tuerca hexagonal",
                           "El entrecaras, el recuento de seis caras, sus ángulos y el "
                           "agujero. El recuento vigila una avería distinta de las "
                           "longitudes: que no falte ni sobre una cara.",
                           PieceFamily::Hexagonal,
                           optionsWith({ToolType::Caliper, ToolType::Polygon,
                                        ToolType::Angle, ToolType::Circle,
                                        ToolType::Region})});

        // ENGRANAJE. Lo propio de la rueda y nada de lados: sus «lados» son
        // dientes, y contarlos con la herramienta de polígono da un número que
        // cambia con la tolerancia del ajuste.
        recipes.push_back({"Engranaje",
                           "Módulo, número de dientes y diámetro primitivo, más la "
                           "redondez de la rueda y su área. Sin cotas de lado: los «lados» "
                           "de un engranaje son sus dientes, y contarlos partiendo el "
                           "contorno da un número que cambia con la tolerancia.",
                           PieceFamily::Gear,
                           optionsWith({ToolType::Gear, ToolType::Circle,
                                        ToolType::Roundness, ToolType::Region})});
        return recipes;
    }();
    return kRecipes;
}

const MeasureRecipe* recipeNamed(const std::string& name) {
    for (const auto& recipe : factoryRecipes()) {
        if (recipe.name == name) {
            return &recipe;
        }
    }
    return nullptr;
}

PieceFamily familyOf(const vision::ShapeClass& shape) {
    switch (shape.kind) {
        case vision::ShapeKind::Circle: return PieceFamily::Round;
        case vision::ShapeKind::Ring: return PieceFamily::Ring;
        case vision::ShapeKind::Polygon:
        case vision::ShapeKind::Rounded:
            if (shape.sides == 4) {
                return PieceFamily::FourSided;
            }
            if (shape.sides == 6) {
                return PieceFamily::Hexagonal;
            }
            return PieceFamily::Any;
        case vision::ShapeKind::Irregular: return PieceFamily::Any;
    }
    return PieceFamily::Any;
}

RecipeResult proposeWithRecipe(const cv::Mat& gray, const cv::Mat& mask,
                               const vision::Fixture& fixture, const MeasureRecipe& recipe,
                               double mmPerPixel) {
    RecipeResult result;
    if (gray.empty() || mask.empty()) {
        result.why = "No hay ninguna pieza sobre la que medir.";
        return result;
    }

    std::vector<std::vector<cv::Point>> outer;
    cv::findContours(mask, outer, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (outer.empty()) {
        result.why = "No hay ninguna pieza sobre la que medir.";
        return result;
    }
    const auto& contour = *std::max_element(
        outer.begin(), outer.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
    const vision::ShapeClass shape = vision::classifyShape(contour, mask);
    const PieceFamily seen = familyOf(shape);

    // ¿VA ESTA RECETA CON ESTA PIEZA?
    //
    // El engranaje se queda fuera de esta comprobación a propósito: para el
    // clasificador una rueda dentada es «irregular», así que exigirle una
    // familia la dejaría sin poder aplicarse nunca. Quien dice si es un
    // engranaje es la herramienta consiguiendo medirlo, y eso se ve unas líneas
    // más abajo — si no saca ninguna cota, se dice.
    if (recipe.family != PieceFamily::Any && recipe.family != PieceFamily::Gear &&
        recipe.family != seen) {
        result.why = "La receta «" + recipe.name + "» es para una " +
                     familyName(recipe.family) + ", y esta pieza se ha reconocido como " +
                     std::string(vision::shapeKindName(shape.kind)) +
                     ". Elige otra receta o mide sin ninguna: forzarla daría cotas que la "
                     "pieza no tiene.";
        return result;
    }

    result.applies = true;
    result.proposals = proposeTools(gray, mask, fixture, recipe.options, mmPerPixel,
                                    &result.dropped);
    if (result.proposals.empty()) {
        // Aplicaba y no ha salido nada. Es distinto de «no aplica» y lleva a
        // hacer algo distinto: ahí la receta era la equivocada; aquí la pieza
        // no da esas cotas —un engranaje al que la rueda no se le puede leer, o
        // una pieza tan pequeña que todos sus rasgos caen por debajo del
        // mínimo—.
        result.why = "La receta «" + recipe.name +
                     "» no ha conseguido ninguna cota sobre esta pieza. " +
                     std::string(shape.reason);
    } else {
        result.why = std::string(shape.reason);
    }
    return result;
}

}  // namespace pci::inspection
