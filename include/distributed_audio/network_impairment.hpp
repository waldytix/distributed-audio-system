#pragma once

#include "distributed_audio/audio_frame.hpp"
#include "distributed_audio/clock.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace distributed_audio::timing {

struct ImpairedFrame {
    AudioFrame frame;
    ClockTimePoint arrival_time;
};

struct ImpairmentPlan {
    std::vector<std::uint64_t> arrival_order;
    std::vector<std::uint64_t> lost_sequences;
    std::vector<std::uint64_t> duplicate_sequences;
    std::vector<std::chrono::milliseconds> arrival_delays;
};

[[nodiscard]] std::vector<ImpairedFrame> apply_impairment(
    const std::vector<AudioFrame>& frames,
    const ImpairmentPlan& plan,
    ClockTimePoint start_time = ClockTimePoint{});

}  // namespace distributed_audio::timing
