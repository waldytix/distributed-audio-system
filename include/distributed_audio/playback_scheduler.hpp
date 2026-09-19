#pragma once

#include "distributed_audio/clock.hpp"
#include "distributed_audio/clock_estimator.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace distributed_audio::timing {

enum class ScheduleDecision {
    not_ready,
    too_early,
    ready,
    late
};

struct ScheduledPlayback {
    std::uint64_t sequence_number{0};
    ClockTimePoint local_play_time{};
};

class PlaybackScheduler {
public:
    PlaybackScheduler(std::chrono::nanoseconds playout_delay,
                      const ClockEstimator& estimator);

    [[nodiscard]] std::optional<ScheduledPlayback> schedule(
        std::uint64_t sequence_number,
        std::chrono::nanoseconds remote_timestamp) const noexcept;
    [[nodiscard]] ScheduleDecision decide(const ScheduledPlayback& scheduled,
                                           ClockTimePoint now) const noexcept;
    [[nodiscard]] std::chrono::nanoseconds playout_delay() const noexcept;

private:
    const std::chrono::nanoseconds playout_delay_;
    const ClockEstimator& estimator_;
};

}  // namespace distributed_audio::timing
