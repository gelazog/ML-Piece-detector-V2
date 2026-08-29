#include "repositories/measure_recipe_repository.h"

#include <algorithm>

#include "database/statement.h"

namespace pci::repositories {

core::Result<void> MeasureRecipeRepository::save(const inspection::MeasureRecipe& recipe) {
    if (recipe.name.empty()) {
        return core::Result<void>::err("Una receta sin nombre no se puede guardar: el "
                                       "nombre es con lo que la pieza la recuerda");
    }
    // NO SE PUEDE PISAR UNA DE FÁBRICA.
    //
    // La pieza guarda el NOMBRE de su receta, así que dos recetas llamadas
    // «Arandela» —una de fábrica y otra propia— dejarían a la pieza apuntando a
    // las dos, y cuál gana dependería del orden en que se busquen. Es el mismo
    // desorden que dos herramientas con el mismo nombre, y se corta aquí.
    if (inspection::recipeNamed(recipe.name) != nullptr) {
        return core::Result<void>::err("«" + recipe.name +
                                       "» es una receta de fábrica: elige otro nombre para "
                                       "la tuya, porque la pieza las recuerda por el nombre "
                                       "y no sabría a cuál de las dos te referías");
    }

    auto stmt = db_.prepare(
        "INSERT INTO MeasureRecipes(name, what, family, tool_types) VALUES(?, ?, ?, ?) "
        "ON CONFLICT(name) DO UPDATE SET what=excluded.what, family=excluded.family, "
        "tool_types=excluded.tool_types;");
    if (!stmt.isOk()) {
        return core::Result<void>::err(stmt.error().message);
    }
    auto& s = stmt.value();
    if (auto b = s.bindText(1, recipe.name); !b.isOk()) return b;
    if (auto b = s.bindText(2, recipe.what); !b.isOk()) return b;
    if (auto b = s.bindText(3, std::string(inspection::familyKey(recipe.family))); !b.isOk()) {
        return b;
    }
    if (auto b = s.bindText(4, inspection::typesToText(recipe.options.allowedTypes));
        !b.isOk()) {
        return b;
    }
    auto step = s.step();
    if (!step.isOk()) {
        return core::Result<void>::err(step.error().message);
    }
    return core::Result<void>::ok();
}

core::Result<std::vector<inspection::MeasureRecipe>> MeasureRecipeRepository::list() {
    using ResultT = core::Result<std::vector<inspection::MeasureRecipe>>;
    auto stmt = db_.prepare(
        "SELECT name, what, family, tool_types FROM MeasureRecipes ORDER BY name;");
    if (!stmt.isOk()) {
        return ResultT::err(stmt.error().message);
    }
    std::vector<inspection::MeasureRecipe> recipes;
    while (true) {
        auto row = stmt.value().step();
        if (!row.isOk()) {
            return ResultT::err(row.error().message);
        }
        if (!row.value()) {
            break;
        }
        inspection::MeasureRecipe recipe;
        recipe.name = stmt.value().columnText(0);
        recipe.what = stmt.value().columnText(1);
        recipe.family = inspection::familyFromKey(stmt.value().columnText(2));
        recipe.options.allowedTypes =
            inspection::typesFromText(stmt.value().columnText(3));
        // Sin clases marcadas, una receta guardada equivaldría a «todas», que no
        // es lo que nadie guarda a propósito. Se salta: una receta que no acota
        // nada no es una receta, y ofrecerla confundiría con «Todas las cotas».
        if (recipe.options.allowedTypes.empty()) {
            continue;
        }
        recipes.push_back(std::move(recipe));
    }
    return ResultT::ok(std::move(recipes));
}

core::Result<void> MeasureRecipeRepository::remove(const std::string& name) {
    auto stmt = db_.prepare("DELETE FROM MeasureRecipes WHERE name = ?;");
    if (!stmt.isOk()) {
        return core::Result<void>::err(stmt.error().message);
    }
    if (auto b = stmt.value().bindText(1, name); !b.isOk()) {
        return b;
    }
    auto step = stmt.value().step();
    if (!step.isOk()) {
        return core::Result<void>::err(step.error().message);
    }
    return core::Result<void>::ok();
}

}  // namespace pci::repositories
