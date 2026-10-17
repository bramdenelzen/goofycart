#pragma once
#include <cstdint>

namespace gk {

// The simulation always steps at 60 Hz, like MK8 Deluxe; frame-based constants
// (mini-turbo charge per frame, boost frames) are only valid at this rate.
inline constexpr int kTickRate = 60;
inline constexpr double kTickSeconds = 1.0 / kTickRate;
inline constexpr float kDt = 1.0f / kTickRate;

// Accumulates real time and reports how many fixed ticks to run, plus the
// interpolation fraction for rendering between the last two ticks.
class FixedStep {
public:
    explicit FixedStep(int max_catch_up_ticks = 8) : max_catch_up_(max_catch_up_ticks) {}

    int advance(double real_seconds) {
        accum_ += real_seconds;
        int ticks = 0;
        while (accum_ >= kTickSeconds && ticks < max_catch_up_) {
            accum_ -= kTickSeconds;
            ++ticks;
        }
        if (ticks == max_catch_up_ && accum_ > kTickSeconds) accum_ = 0.0; // drop time after a hitch
        return ticks;
    }

    float alpha() const { return static_cast<float>(accum_ / kTickSeconds); }

private:
    double accum_ = 0.0;
    int max_catch_up_;
};

} // namespace gk
