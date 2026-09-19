#pragma once

#include "distributed_audio/jitter_buffer.hpp"

#include <cstdint>
#include <vector>

namespace distributed_audio::timing {

struct SimulatedPlaybackStatistics {
    std::uint64_t real_frames_played{0};
    std::uint64_t concealed_frames_played{0};
    std::uint64_t late_frames{0};
    std::uint64_t underruns{0};
    std::uint64_t total_frames_processed{0};
    std::uint64_t samples_played{0};
    std::uint64_t first_sequence{0};
    std::uint64_t last_sequence{0};
    float peak_level{0.0F};
    std::uint64_t payload_checksum{0};
    bool has_sequence{false};
    std::vector<std::uint64_t> playback_sequence;
};

class SimulatedPlayback {
public:
    void consume(const PlaybackItem& item);
    [[nodiscard]] const SimulatedPlaybackStatistics& statistics() const noexcept;
    void reset() noexcept;

private:
    SimulatedPlaybackStatistics statistics_;
};

}  // namespace distributed_audio::timing
