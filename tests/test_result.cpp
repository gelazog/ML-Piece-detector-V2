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
