#include "distributed_audio/clock.hpp"

namespace distributed_audio::timing {

ClockTimePoint SteadyClock::now() const noexcept {
    return std::chrono::time_point_cast<ClockDuration>(std::chrono::steady_clock::now());
}

ManualClock::ManualClock(ClockTimePoint initial) : current_(initial) {}

ClockTimePoint ManualClock::now() const noexcept { return current_; }

void ManualClock::set(ClockTimePoint time) noexcept { current_ = time; }

void ManualClock::advance(ClockDuration duration) noexcept { current_ += duration; }

}  // namespace distributed_audio::timing
