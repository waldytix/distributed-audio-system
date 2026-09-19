#pragma once

#include "distributed_audio/audio_frame.hpp"
#include "distributed_audio/clock.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace distributed_audio::timing {

[[nodiscard]] std::optional<ClockDuration> frame_duration(const AudioFrame& frame) noexcept;

struct JitterEstimate {
    double nanoseconds{0.0};
    std::uint64_t samples{0};
};

class JitterEstimator {
public:
    void update(AudioFrame::Timestamp sender_timestamp, ClockTimePoint arrival_time) noexcept;
    void reset() noexcept;
    [[nodiscard]] JitterEstimate estimate() const noexcept;

private:
    bool initialized_{false};
    double previous_transit_{0.0};
    JitterEstimate estimate_;
};

}  // namespace distributed_audio::timing
