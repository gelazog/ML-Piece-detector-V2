#include "core/result.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using pci::core::Result;

TEST(Result, OkHoldsValue) {
    const auto result = Result<int>::ok(42);
    ASSERT_TRUE(result.isOk());
    EXPECT_EQ(result.value(), 42);
}

TEST(Result, ErrHoldsMessage) {
    const auto result = Result<int>::err("cámara no disponible");
    ASSERT_FALSE(result.isOk());
    EXPECT_EQ(result.error().message, "cámara no disponible");
}

TEST(Result, MovesNonCopyableValue) {
    auto result = Result<std::unique_ptr<int>>::ok(std::make_unique<int>(7));
    ASSERT_TRUE(result.isOk());
    EXPECT_EQ(*result.value(), 7);
}

TEST(ResultVoid, OkAndErr) {
    const auto okResult = Result<void>::ok();
    EXPECT_TRUE(okResult.isOk());

    const auto errResult = Result<void>::err("disco lleno");
    ASSERT_FALSE(errResult.isOk());
    EXPECT_EQ(errResult.error().message, "disco lleno");
}

// UN AJUSTE QUE NO SE PUEDE LEER NO PUEDE CERRAR EL PROGRAMA.
//
// La ventana leía 65 ajustes con `getInt("x", 0).value()`. Sobre un error,
// `value()` solo tiene un `assert` —que en release no existe— y `std::get`
// lanza `bad_variant_access`: el programa se cierra al abrir la ventana. Un
// «database is locked» basta, y puede pasar porque el hilo de inspección
// escribe en la misma base.
//
// `valueOr` devuelve el valor si lo hay y el de reserva si no. Esta prueba fija
// las dos mitades y, sobre todo, que con un error NO lanza.
TEST(Result, ValueOrGivesTheFallbackInsteadOfThrowing) {
    const auto ok = pci::core::Result<int>::ok(7);
    EXPECT_EQ(ok.valueOr(0), 7);

    const auto locked = pci::core::Result<int>::err("database is locked");
    EXPECT_NO_THROW({ EXPECT_EQ(locked.valueOr(42), 42); });

    const auto name = pci::core::Result<std::string>::err("sin conexión");
    EXPECT_EQ(name.valueOr("de fábrica"), "de fábrica");
}
