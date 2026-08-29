// «LA MÍA DE BRIDAS»: LA RECETA QUE SE GUARDA EL OPERADOR.
//
// Era la mitad que faltaba de la petición original —«un conjunto PERSONALIZADO
// de reglas»—: las seis de fábrica cubren las familias corrientes, pero quien
// mide sus propias piezas quiere quitar y poner una clase y que eso se quede.
// Hasta ahora se podía ajustar las casillas y el ajuste duraba lo que la sesión.
//
// Tres decisiones, y las tres se comprueban aquí porque las tres se pueden
// deshacer sin que nada falle:
//
//   1. **No se puede pisar una receta de fábrica.** La pieza guarda el NOMBRE de
//      su receta, así que dos llamadas «Arandela» dejarían a la pieza apuntando
//      a las dos y ganaría la que se buscara primero. Es el mismo desorden que
//      dos herramientas con el mismo nombre.
//   2. **Se guarda por claves de texto, no por números de enum.** Reordenar
//      `PieceFamily` —añadir «tuerca cuadrada» en medio— convertiría en silencio
//      las recetas de una familia en recetas de otra, y el operador vería su
//      receta de arandelas negándose a aplicarse a una arandela.
//   3. **Una clase que ya no existe se salta, no tira la receta.** Si una
//      versión futura quita una clase de cota, la receta sigue sirviendo para
//      las demás; devolver un error dejaría al operador sin ella por algo que ya
//      no le importa.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "database/db.h"
#include "database/schema.h"
#include "inspection_editor/measure_recipe.h"
#include "repositories/measure_recipe_repository.h"

using namespace pci;

namespace {

std::unique_ptr<database::Db> freshDb(const char* name) {
    const auto path = (std::filesystem::temp_directory_path() / name).string();
    std::filesystem::remove(path);
    auto opened = database::Db::open(path);
    if (!opened.isOk()) {
        return nullptr;
    }
    auto db = std::move(opened.value());
    if (!database::migrate(*db).isOk()) {
        return nullptr;
    }
    return db;
}

inspection::MeasureRecipe mine(const std::string& name) {
    inspection::MeasureRecipe recipe;
    recipe.name = name;
    recipe.what = "Receta propia";
    recipe.family = inspection::PieceFamily::FourSided;
    recipe.options.allowedTypes = {inspection::ToolType::Ruler, inspection::ToolType::Angle,
                                   inspection::ToolType::Region};
    return recipe;
}

}  // namespace

TEST(OwnRecipes, ItComesBackExactlyAsItWasSaved) {
    auto db = freshDb("pci_own_recipes.db");
    ASSERT_NE(db, nullptr);
    repositories::MeasureRecipeRepository repo(*db);

    ASSERT_TRUE(repo.save(mine("bridas del proveedor B")).isOk());
    auto listed = repo.list();
    ASSERT_TRUE(listed.isOk()) << listed.error().message;
    ASSERT_EQ(listed.value().size(), 1U);

    const auto& back = listed.value().front();
    std::printf("  [receta] «%s», familia %s, %d clases\n", back.name.c_str(),
                inspection::familyName(back.family),
                static_cast<int>(back.options.allowedTypes.size()));
    EXPECT_EQ(back.name, "bridas del proveedor B");
    EXPECT_EQ(back.family, inspection::PieceFamily::FourSided)
        << "la familia no vuelve: la receta se aplicaría a piezas que no son las suyas";
    EXPECT_EQ(back.options.allowedTypes, mine("x").options.allowedTypes)
        << "las clases no vuelven, o vuelven en otro orden: la receta guardada y la "
           "elegida serían dos objetos distintos con el mismo nombre";
}

TEST(OwnRecipes, ItCannotBeNamedLikeAFactoryOne) {
    // LA REGLA QUE PROTEGE LA ASIGNACIÓN. La pieza guarda el nombre, así que dos
    // recetas «Arandela» la dejarían apuntando a las dos.
    auto db = freshDb("pci_own_recipes_clash.db");
    ASSERT_NE(db, nullptr);
    repositories::MeasureRecipeRepository repo(*db);

    const auto refused = repo.save(mine("Arandela"));
    std::printf("  [receta] guardar «Arandela»: %s\n",
                refused.isOk() ? "aceptada" : refused.error().message.c_str());
    EXPECT_FALSE(refused.isOk())
        << "se puede guardar una receta propia con el nombre de una de fábrica: la pieza "
           "que guarde «Arandela» no sabrá a cuál de las dos se refería";

    // Y una sin nombre tampoco: el nombre no es decoración, es la clave.
    EXPECT_FALSE(repo.save(mine("")).isOk());
    auto listed = repo.list();
    ASSERT_TRUE(listed.isOk());
    EXPECT_TRUE(listed.value().empty()) << "se guardó alguna de las dos rechazadas";
}

TEST(OwnRecipes, SavingTwiceWithTheSameNameReplacesInsteadOfDuplicating) {
    // Guardar dos veces con la misma etiqueta es lo que el operador espera que
    // sobrescriba —es lo que ya hacen los perfiles de detección—. Duplicar
    // dejaría dos entradas idénticas en el desplegable y una de ellas
    // inalcanzable.
    auto db = freshDb("pci_own_recipes_twice.db");
    ASSERT_NE(db, nullptr);
    repositories::MeasureRecipeRepository repo(*db);

    ASSERT_TRUE(repo.save(mine("la mía")).isOk());
    inspection::MeasureRecipe changed = mine("la mía");
    changed.options.allowedTypes = {inspection::ToolType::Circle};
    changed.family = inspection::PieceFamily::Round;
    ASSERT_TRUE(repo.save(changed).isOk());

    auto listed = repo.list();
    ASSERT_TRUE(listed.isOk());
    ASSERT_EQ(listed.value().size(), 1U) << "se ha duplicado en vez de sustituir";
    EXPECT_EQ(listed.value().front().family, inspection::PieceFamily::Round);
    EXPECT_EQ(listed.value().front().options.allowedTypes.size(), 1U);

    ASSERT_TRUE(repo.remove("la mía").isOk());
    listed = repo.list();
    ASSERT_TRUE(listed.isOk());
    EXPECT_TRUE(listed.value().empty()) << "borrarla no la borra";
}

TEST(OwnRecipes, TheKeysSurviveAReorderOfTheEnums) {
    // POR QUÉ SE GUARDAN CLAVES Y NO NÚMEROS. Un número de enum vale lo que el
    // orden del enum, y ese orden cambia cuando alguien añade una familia en
    // medio: todas las recetas guardadas pasarían a ser de otra familia sin que
    // nada fallara.
    for (const auto family :
         {inspection::PieceFamily::Any, inspection::PieceFamily::Round,
          inspection::PieceFamily::Ring, inspection::PieceFamily::FourSided,
          inspection::PieceFamily::Hexagonal, inspection::PieceFamily::Gear}) {
        EXPECT_EQ(inspection::familyFromKey(inspection::familyKey(family)), family)
            << "la familia «" << inspection::familyName(family)
            << "» no sobrevive a guardarse y volver";
    }
    // Una clave desconocida —una receta de una versión más nueva— vale para
    // cualquier pieza en vez de negarse siempre: lo que acota son sus clases de
    // cota, y ésas sí se entienden.
    EXPECT_EQ(inspection::familyFromKey("familia que no existe"),
              inspection::PieceFamily::Any);

    // Y las clases, por sus nombres de siempre.
    const std::vector<inspection::ToolType> types{
        inspection::ToolType::Circle, inspection::ToolType::Angle,
        inspection::ToolType::Region};
    const std::string text = inspection::typesToText(types);
    std::printf("  [receta] tres clases -> «%s»\n", text.c_str());
    EXPECT_EQ(inspection::typesFromText(text), types);

    // Una clase que ya no existe se SALTA, no tira la receta entera.
    const auto survivors =
        inspection::typesFromText("circle,una_clase_que_ya_no_existe,angle");
    EXPECT_EQ(survivors.size(), 2U)
        << "una clase desconocida se lleva por delante la receta entera: quien actualice "
           "se queda sin ella por una cota que ya no le importa";
}
