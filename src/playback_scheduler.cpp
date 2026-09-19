#include "distributed_audio/playback_scheduler.hpp"

#include <stdexcept>

namespace distributed_audio::timing {

PlaybackScheduler::PlaybackScheduler(std::chrono::nanoseconds playout_delay,
                                     const ClockEstimator& estimator)
    : playout_delay_(playout_delay), estimator_(estimator) {
    if (playout_delay_ < std::chrono::nanoseconds::zero()) {
        throw std::invalid_argument("playout delay must not be negative");
    }
}

std::optional<ScheduledPlayback> PlaybackScheduler::schedule(
    std::uint64_t sequence_number,
    std::chrono::nanoseconds remote_timestamp) const noexcept {
    const auto mapped = estimator_.map_remote_to_local(remote_timestamp);
    if (!mapped.has_value()) {
        return std::nullopt;
    }
    return ScheduledPlayback{sequence_number, *mapped + playout_delay_};
}

ScheduleDecision PlaybackScheduler::decide(const ScheduledPlayback& scheduled,
                                            ClockTimePoint now) const noexcept {
    if (now < scheduled.local_play_time) {
        return ScheduleDecision::too_early;
    }
    return now == scheduled.local_play_time ? ScheduleDecision::ready
                                             : ScheduleDecision::late;
}

std::chrono::nanoseconds PlaybackScheduler::playout_delay() const noexcept {
    return playout_delay_;
}

}  // namespace distributed_audio::timing
