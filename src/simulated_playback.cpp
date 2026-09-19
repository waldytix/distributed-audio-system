#include "distributed_audio/simulated_playback.hpp"

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
