#pragma once

#include <chrono>

namespace distributed_audio::timing {

using ClockDuration = std::chrono::nanoseconds;
using ClockTimePoint = std::chrono::time_point<std::chrono::steady_clock, ClockDuration>;

class Clock {
public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual ClockTimePoint now() const noexcept = 0;
};

class SteadyClock final : public Clock {
public:
    [[nodiscard]] ClockTimePoint now() const noexcept override;
};

class ManualClock final : public Clock {
public:
    explicit ManualClock(ClockTimePoint initial = ClockTimePoint{});

    [[nodiscard]] ClockTimePoint now() const noexcept override;
    void set(ClockTimePoint time) noexcept;
    void advance(ClockDuration duration) noexcept;

private:
    ClockTimePoint current_;
};

}  // namespace distributed_audio::timing
