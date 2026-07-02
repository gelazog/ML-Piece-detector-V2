#pragma once

#include <chrono>
#include <deque>

namespace pci::core {

// Contador de FPS por ventana deslizante. El instante de tiempo se inyecta
// en cada llamada para poder probarlo sin reloj real ni hardware.
class FpsCounter {
public:
    using Clock = std::chrono::steady_clock;

    explicit FpsCounter(std::chrono::milliseconds window = std::chrono::milliseconds(1000))
        : window_(window) {}

    void tick(Clock::time_point now = Clock::now()) {
        stamps_.push_back(now);
        prune(now);
    }

    [[nodiscard]] double fps(Clock::time_point now = Clock::now()) {
        prune(now);
        const double seconds = std::chrono::duration<double>(window_).count();
        return static_cast<double>(stamps_.size()) / seconds;
    }

private:
    void prune(Clock::time_point now) {
        while (!stamps_.empty() && now - stamps_.front() > window_) {
            stamps_.pop_front();
        }
    }

    std::chrono::milliseconds window_;
    std::deque<Clock::time_point> stamps_;
};

}  // namespace pci::core
