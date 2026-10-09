// UN ÍNDICE O UN TIPO MAL ELEGIDO NO DA ERROR: GUARDA LA COLUMNA MAL.
//
// Los repositorios enlazaban cada parámetro a mano —139 `bindX(i, v)`, casi
// todos con su `if (!b.isOk()) return …`— y repetían en 32 escrituras el mismo
// prepare → comprobar → step → comprobar: 12 líneas para el UPDATE de dos
// columnas de `saveOrientationOffset`. `Statement::bindAll` y `Db::run` lo dicen
// en una línea: 18 de esas escrituras pasan ahora por `run` y otras 12
// sentencias enlazan con `bindAll` (los repositorios bajaron de 2057 a 1800
// líneas). Un fallo suyo ya no es de una función: es de todas a la vez.
//
// Y es de los que no avisan. Si `bindAll` contara desde 0, el primer bind
// fallaría y se vería; pero si eligiera mal el bindX por el tipo —un double
// guardado como entero, un texto como blob, el blob cortado en el primer byte
// cero— SQLite lo aceptaría sin protestar. Por eso se comprueba con `typeof` el
// TIPO con que queda cada columna, no solo el valor.
//
// Y que un bind fallido corta: si `run` siguiera hasta el step, una sentencia
// con un argumento de más se ejecutaría igual y devolvería ok.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "database/db.h"

using namespace pci;

namespace {

std::unique_ptr<database::Db> openInMemory() {
    auto opened = database::Db::open(":memory:");
    EXPECT_TRUE(opened.isOk()) << (opened.isOk() ? "" : opened.error().message);
    return opened.isOk() ? std::move(opened.value()) : nullptr;
}

}  // namespace

TEST(BindAll, CadaTipoLlegaASuParametroConSuTipo) {
    auto db = openInMemory();
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->exec("CREATE TABLE t (a, b, c, d, e, f, g, h, i);").isOk());

    // El blob lleva un 0 en medio: se enlaza por tamaño, no hasta el primer cero.
    const std::vector<unsigned char> blob{0x01, 0x00, 0xFF};
    const auto inserted =
        db->run("INSERT INTO t VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);", std::int64_t{-7}, 2.5,
                std::string("hola"), blob, nullptr, true, "literal", 3, 0.25F);
    ASSERT_TRUE(inserted.isOk()) << inserted.error().message;

    auto stmt = db->prepare(
        "SELECT typeof(a), typeof(b), typeof(c), typeof(d), typeof(e), typeof(f), typeof(g), "
        "typeof(h), typeof(i), a, b, c, d, f, g, h, i FROM t;");
    ASSERT_TRUE(stmt.isOk());
    auto& s = stmt.value();
    auto row = s.step();
    ASSERT_TRUE(row.isOk() && row.value());

    const std::vector<std::string> expectedTypes{"integer", "real",    "text",    "blob", "null",
                                                 "integer", "text",    "integer", "real"};
    for (int column = 0; column < 9; ++column) {
        EXPECT_EQ(s.columnText(column), expectedTypes[static_cast<std::size_t>(column)])
            << "columna " << column;
    }
    EXPECT_EQ(s.columnInt(9), -7);
    EXPECT_DOUBLE_EQ(s.columnDouble(10), 2.5);
    EXPECT_EQ(s.columnText(11), "hola");
    EXPECT_EQ(s.columnBlob(12), blob);
    EXPECT_EQ(s.columnInt(13), 1);
    EXPECT_EQ(s.columnText(14), "literal");
    EXPECT_EQ(s.columnInt(15), 3);
    EXPECT_DOUBLE_EQ(s.columnDouble(16), 0.25);
}

TEST(BindAll, ElArgumentoKVaAlParametroK) {
    auto db = openInMemory();
    ASSERT_NE(db, nullptr);
    // Parámetros numerados al revés: si bindAll se saltara uno o empezara en
    // otro índice, los textos saldrían cambiados o vacíos.
    auto stmt = db->prepare("SELECT ?3, ?2, ?1;");
    ASSERT_TRUE(stmt.isOk());
    ASSERT_TRUE(stmt.value().bindAll("uno", "dos", "tres").isOk());
    auto row = stmt.value().step();
    ASSERT_TRUE(row.isOk() && row.value());
    EXPECT_EQ(stmt.value().columnText(0), "tres");
    EXPECT_EQ(stmt.value().columnText(1), "dos");
    EXPECT_EQ(stmt.value().columnText(2), "uno");
}

TEST(DbRun, DevuelveElPrimerErrorYNoEscribeTrasUnBindFallido) {
    auto db = openInMemory();
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->exec("CREATE TABLE t (a NOT NULL);").isOk());

    // Un argumento de más: el bind 2 no tiene parámetro y la fila no se escribe.
    const auto extra = db->run("INSERT INTO t VALUES (?);", 1, 2);
    ASSERT_FALSE(extra.isOk());
    EXPECT_EQ(extra.error().message.rfind("Error en bind", 0), 0U) << extra.error().message;

    auto count = db->prepare("SELECT COUNT(*) FROM t;");
    ASSERT_TRUE(count.isOk());
    ASSERT_TRUE(count.value().step().isOk());
    EXPECT_EQ(count.value().columnInt(0), 0);

    // Cada fase devuelve su propio error, con el mismo texto que antes daba
    // cada repositorio a mano.
    const auto badSql = db->run("INSERT INTO no_existe VALUES (?);", 1);
    ASSERT_FALSE(badSql.isOk());
    EXPECT_EQ(badSql.error().message.rfind("Error preparando SQL", 0), 0U);

    const auto nulo = db->run("INSERT INTO t VALUES (?);", nullptr);
    ASSERT_FALSE(nulo.isOk());
    EXPECT_EQ(nulo.error().message.rfind("Error ejecutando SQL", 0), 0U);
    EXPECT_NE(nulo.error().message.find("NOT NULL"), std::string::npos);
}
