#include "distributed_audio/simulated_playback.hpp"

#include "distributed_audio/pcm_conversion.hpp"

#include <algorithm>
#include <cmath>

namespace distributed_audio::timing {

void SimulatedPlayback::consume(const PlaybackItem& item) {
    if (item.status == PullStatus::not_ready || item.status == PullStatus::stopped) {
        if (item.status == PullStatus::not_ready) {
            ++statistics_.underruns;
        }
        return;
    }
    ++statistics_.total_frames_processed;
    statistics_.playback_sequence.push_back(item.sequence_number);
    if (!statistics_.has_sequence) {
        statistics_.first_sequence = item.sequence_number;
        statistics_.has_sequence = true;
    }
    statistics_.last_sequence = item.sequence_number;
    if (item.frame.has_value()) {
        const auto samples = pcm16_to_float(*item.frame);
        statistics_.samples_played += samples.size();
        for (const auto byte : item.frame->payload()) {
            statistics_.payload_checksum = statistics_.payload_checksum * 1099511628211ULL + byte;
        }
        for (const auto sample : samples) {
            statistics_.peak_level = std::max(statistics_.peak_level, std::fabs(sample));
        }
    }
    if (item.concealed) {
        ++statistics_.concealed_frames_played;
    } else {
        ++statistics_.real_frames_played;
    }
}

const SimulatedPlaybackStatistics& SimulatedPlayback::statistics() const noexcept {
    return statistics_;
}

void SimulatedPlayback::reset() noexcept { statistics_ = {}; }

}  // namespace distributed_audio::timing
