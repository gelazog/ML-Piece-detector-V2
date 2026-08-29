// ELEGIR LA RECETA CIEN VECES PARA UN LOTE DE CIEN.
//
// La petición de uso decía «de un lote o de una pieza», y esa es la mitad que
// faltaba: la receta de medición se elegía en cada apertura del diálogo y se
// perdía al cerrarlo. Con una bandeja de cien engranajes, eso es volver a decir
// «Engranaje» cien veces — y a la tercera, nadie la elige.
//
// Se guarda POR PIEZA, como el perfil de detección y como el recuento esperado,
// por la misma razón: «esto es un engranaje» es una propiedad del trabajo, no de
// la máquina.
//
// Y SE GUARDA EL NOMBRE, no un identificador ni las casillas. Tres decisiones,
// cada una con su porqué:
//
//   - el nombre, porque las recetas viven en el código y no en la base: una
//     tabla de recetas sería una copia que se queda vieja;
//   - un nombre que ya no existe se trata como «sin receta», igual que un perfil
//     de detección borrado devuelve a los ajustes globales — una receta que no
//     está no se puede aplicar, y fallar por eso dejaría la pieza sin poder
//     medirse;
//   - las casillas ajustadas a mano NO se guardan: si se guardaran, el
//     desplegable diría «Arandela» mientras las clases son otras, y eso se lee
//     peor que no recordarlo.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "database/db.h"
#include "database/schema.h"
#include "inspection_editor/measure_recipe.h"
#include "repositories/piece_repository.h"

using namespace pci;

namespace {

std::string readSource(const std::string& relative) {
    for (const auto* root : {"src/", "../src/", "../../src/", "../../../src/"}) {
        std::ifstream file(std::string(root) + relative);
        if (file) {
            std::ostringstream all;
            all << file.rdbuf();
            return all.str();
        }
    }
    return {};
}

}  // namespace

TEST(RecipeIsRemembered, ThePieceKeepsTheRecipeAndStartsWithoutOne) {
    const auto path =
        (std::filesystem::temp_directory_path() / "pci_recipe_memory.db").string();
    std::filesystem::remove(path);
    auto opened = database::Db::open(path);
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(database::migrate(*db).isOk());

    repositories::PieceRepository pieces(*db);
    const auto pieceId = pieces.createPiece("engranaje de prueba");
    ASSERT_TRUE(pieceId.isOk()) << pieceId.error().message;

    // Nace SIN receta, que es exactamente como se comportaba la aplicación antes
    // de que existieran. Una pieza registrada hace un año no puede empezar a
    // recibir un juego de cotas distinto porque se haya añadido esta función.
    auto fresh = pieces.loadMeasureRecipe(pieceId.value());
    ASSERT_TRUE(fresh.isOk()) << fresh.error().message;
    EXPECT_TRUE(fresh.value().empty())
        << "una pieza recién creada ya trae receta: «" << fresh.value() << "»";

    ASSERT_TRUE(pieces.saveMeasureRecipe(pieceId.value(), "Engranaje").isOk());
    auto stored = pieces.loadMeasureRecipe(pieceId.value());
    ASSERT_TRUE(stored.isOk()) << stored.error().message;
    EXPECT_EQ(stored.value(), "Engranaje");
    std::printf("  [receta] la pieza recuerda «%s»\n", stored.value().c_str());

    // Y el nombre guardado tiene que ser uno de verdad: si alguien renombra una
    // receta de fábrica, las piezas que la usaban se quedan sin ella en silencio.
    EXPECT_NE(inspection::recipeNamed(stored.value()), nullptr)
        << "la receta guardada ya no existe en el código. El nombre es la clave con la que "
           "se guarda en la pieza, así que renombrar una receta de fábrica deja huérfanas "
           "a las piezas que la usaban";

    // Quitarla se puede, y devuelve a «sin receta».
    ASSERT_TRUE(pieces.saveMeasureRecipe(pieceId.value(), "").isOk());
    auto cleared = pieces.loadMeasureRecipe(pieceId.value());
    ASSERT_TRUE(cleared.isOk());
    EXPECT_TRUE(cleared.value().empty());
}

TEST(RecipeIsRemembered, ARecipeThatNoLongerExistsIsTreatedAsNone) {
    // El caso que hay que decidir a propósito: la base guarda un nombre que el
    // código ya no tiene —una receta renombrada, una versión más vieja—. La
    // respuesta es «sin receta», nunca un error: el operador abriría el diálogo
    // y no podría medir por algo que no ha hecho él.
    const auto path =
        (std::filesystem::temp_directory_path() / "pci_recipe_gone.db").string();
    std::filesystem::remove(path);
    auto opened = database::Db::open(path);
    ASSERT_TRUE(opened.isOk());
    auto db = std::move(opened.value());
    ASSERT_TRUE(database::migrate(*db).isOk());
    repositories::PieceRepository pieces(*db);
    const auto pieceId = pieces.createPiece("pieza de una versión anterior");
    ASSERT_TRUE(pieceId.isOk());
    ASSERT_TRUE(pieces.saveMeasureRecipe(pieceId.value(), "Receta que ya no existe").isOk());

    auto stored = pieces.loadMeasureRecipe(pieceId.value());
    ASSERT_TRUE(stored.isOk()) << "leer una receta desconocida da error: el operador se "
                                  "queda sin poder medir por algo que no ha hecho";
    EXPECT_EQ(inspection::recipeNamed(stored.value()), nullptr);
    std::printf("  [receta] «%s» ya no existe -> se mide sin receta\n",
                stored.value().c_str());
}

TEST(RecipeIsRemembered, TheEditorReadsItBeforeAskingAndWritesItAfterAccepting) {
    // LA COSTURA, que es donde esto se rompe sin que nada falle. El modelo y el
    // repositorio pueden estar perfectos y la receta no llegar nunca al diálogo:
    // son seis líneas de pegamento en `onAutoMeasureClicked` y no hay ninguna
    // aserción que las sostenga.
    //
    // Se comprueban las dos direcciones y el ORDEN, porque cada una falla de una
    // forma distinta: sin la lectura, el operador vuelve a elegir la receta cada
    // vez (que es justo lo que esto vino a arreglar); sin la escritura, la elige
    // y no se entera de que no se guardó.
    const std::string source = readSource("inspection_editor/editor_window.cpp");
    ASSERT_FALSE(source.empty()) << "no se encuentra editor_window.cpp";
    const auto opens = source.find("void EditorWindow::onAutoMeasureClicked()");
    ASSERT_NE(opens, std::string::npos)
        << "`onAutoMeasureClicked` se ha renombrado: hay que actualizar esta prueba, no "
           "borrarla";
    const std::string body = source.substr(opens, 6000);

    const auto reads = body.find("loadMeasureRecipe");
    const auto writes = body.find("saveMeasureRecipe");
    EXPECT_NE(reads, std::string::npos)
        << "el editor no lee la receta guardada: la pieza la recuerda y el diálogo abre sin "
           "ella, así que el operador la vuelve a elegir cada vez";
    EXPECT_NE(writes, std::string::npos)
        << "el editor no guarda la receta elegida: se elige, se mide, y la próxima vez hay "
           "que volver a elegirla";
    EXPECT_LT(reads, writes)
        << "se guarda antes de leer: entonces lo que se guarda es la receta por defecto y "
           "la de la pieza se pierde en la primera apertura";

    // Y se guarda al ACEPTAR: si se guardara antes del `exec`, cancelar dejaría
    // cambiada la pieza, y cancelar tiene que no haber pasado.
    const auto exec = body.find("dialog.exec()");
    ASSERT_NE(exec, std::string::npos);
    EXPECT_LT(reads, exec) << "la receta se lee después de abrir el diálogo: llega tarde";
    EXPECT_GT(writes, exec)
        << "la receta se guarda antes de abrir el diálogo: cancelar dejaría la pieza "
           "cambiada";
}
