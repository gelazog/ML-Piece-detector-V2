#pragma once

#include <string>
#include <vector>

#include "core/result.h"
#include "database/db.h"
#include "inspection_editor/measure_recipe.h"

namespace pci::repositories {

// LAS RECETAS DE MEDICIÓN QUE SE GUARDA EL OPERADOR.
//
// Las seis de fábrica viven en el código —`inspection::factoryRecipes()`— y no
// pasan por aquí. Esto es la otra mitad de «un conjunto PERSONALIZADO de reglas»:
// ajustar las casillas de una receta y guardar el resultado con nombre propio,
// «la mía de bridas», para no volver a marcarlas cada vez.
//
// Se guardan por NOMBRE y sin id que referencie nadie, porque el nombre ya es la
// clave con la que la pieza apunta a su receta (columna `measure_recipe`). Así
// una receta propia y una de fábrica se asignan exactamente igual, y la pieza no
// tiene que saber de cuál de las dos se trata.
//
// Y por eso mismo, guardar con el nombre de una de fábrica está PROHIBIDO: la
// pieza guardaría «Arandela» sin poder distinguir a cuál de las dos se refería, y
// la que ganara dependería del orden en que se buscara. Es el mismo desorden que
// dos herramientas con el mismo nombre.
class MeasureRecipeRepository {
public:
    explicit MeasureRecipeRepository(database::Db& db) : db_(db) {}

    // Crea o actualiza por nombre. Falla si el nombre es de una receta de
    // fábrica, o si está vacío.
    core::Result<void> save(const inspection::MeasureRecipe& recipe);
    core::Result<std::vector<inspection::MeasureRecipe>> list();
    core::Result<void> remove(const std::string& name);

private:
    database::Db& db_;
};

}  // namespace pci::repositories
