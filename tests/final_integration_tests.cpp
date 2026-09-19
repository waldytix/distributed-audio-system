#include "distributed_audio/audio_stream.hpp"
#include "distributed_audio/network_resilience.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

std::vector<distributed_audio::AudioFrame> make_frames(std::size_t count) {
    std::vector<distributed_audio::AudioFrame> frames;
    for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
        frames.emplace_back(distributed_audio::AudioFormat{48'000, 2, 16}, sequence,
                            distributed_audio::AudioFrame::Timestamp{static_cast<std::int64_t>(sequence) * 5'000'000},
                            distributed_audio::AudioFrame::Payload(960, static_cast<std::uint8_t>(sequence)));
    }
    return frames;
}
}

int main() {
    try {
        auto frames = make_frames(250);
        distributed_audio::timing::ImpairmentPlan plan;
        for (std::uint64_t sequence = 0; sequence < frames.size(); ++sequence) {
            if (sequence < 100 || sequence >= 110) plan.arrival_order.push_back(sequence);
        }
        plan.lost_sequences = {30, 31, 32, 33, 150};
        plan.duplicate_sequences = {80};
        plan.arrival_delays.assign(plan.arrival_order.size(), std::chrono::milliseconds{1});
        const auto result = distributed_audio::resilience::run_deterministic_resilience(
            frames, plan, distributed_audio::resilience::AdaptiveJitterConfig{},
            distributed_audio::resilience::RecoveryConfig{});
        require(result.playback_sequence.size() > 50, "full integration did not continue playback");
        require(result.statistics.concealed_frames > 0, "full integration did not conceal loss");
        require(result.final_buffer_depth <= 16, "full integration exceeded buffer bound");
        std::cout << "Final integration passed: " << result.playback_sequence.size()
                  << " playback frames, " << result.statistics.concealed_frames << " concealed.\n";
    } catch (const std::exception& error) {
        std::cerr << "Final integration failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
