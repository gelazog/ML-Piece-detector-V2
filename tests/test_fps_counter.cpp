#include "core/fps_counter.h"

#include <gtest/gtest.h>

#include <chrono>

using pci::core::FpsCounter;
using namespace std::chrono_literals;

namespace {

FpsCounter::Clock::time_point at(std::chrono::milliseconds offset) {
    return FpsCounter::Clock::time_point(offset);
}

}  // namespace

TEST(FpsCounter, EmptyReportsZero) {
    FpsCounter counter;
    EXPECT_DOUBLE_EQ(counter.fps(at(1000ms)), 0.0);
}

TEST(FpsCounter, CountsTicksInsideWindow) {
    FpsCounter counter(1000ms);
    for (int i = 0; i < 10; ++i) {
        counter.tick(at(std::chrono::milliseconds(i * 100)));
    }
    // 10 ticks dentro de la ventana de 1 s -> 10 fps.
    EXPECT_DOUBLE_EQ(counter.fps(at(1000ms)), 10.0);
}

TEST(FpsCounter, DropsTicksOutsideWindow) {
    FpsCounter counter(1000ms);
    counter.tick(at(0ms));
    counter.tick(at(100ms));
    counter.tick(at(2000ms));
    // Los dos primeros ticks quedan fuera de la ventana [1000, 2000].
    EXPECT_DOUBLE_EQ(counter.fps(at(2000ms)), 1.0);
}

TEST(FpsCounter, AllTicksExpire) {
    FpsCounter counter(1000ms);
    counter.tick(at(0ms));
    counter.tick(at(50ms));
    EXPECT_DOUBLE_EQ(counter.fps(at(5000ms)), 0.0);
}
