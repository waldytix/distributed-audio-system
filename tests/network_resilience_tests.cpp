#include "distributed_audio/network_resilience.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using distributed_audio::AudioFormat;
using distributed_audio::AudioFrame;
using distributed_audio::resilience::AdaptiveJitterBuffer;
using distributed_audio::resilience::AdaptiveJitterConfig;
using distributed_audio::resilience::NetworkStatistics;
using distributed_audio::resilience::RecoveryConfig;
using distributed_audio::resilience::ResilienceState;
using distributed_audio::resilience::SessionLiveness;
using distributed_audio::timing::ClockTimePoint;
using namespace std::chrono_literals;

void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

std::vector<AudioFrame> frames(std::size_t count) {
    std::vector<AudioFrame> result;
    for (std::uint64_t sequence = 0; sequence < count; ++sequence) {
        result.emplace_back(AudioFormat{48'000, 2, 16}, sequence,
                           AudioFrame::Timestamp{static_cast<std::int64_t>(sequence) * 5'000'000},
                           AudioFrame::Payload(960, static_cast<std::uint8_t>(sequence)));
    }
    return result;
}

void test_statistics() {
    NetworkStatistics stats;
    stats.packet_sent(10); stats.packet_received(10); stats.packet_accepted();
    stats.duplicate(); stats.malformed(); stats.reconnect(); stats.set_jitter(1.5); stats.set_buffer_depth(3);
    const auto snapshot = stats.snapshot();
    require(snapshot.packets_sent == 1 && snapshot.bytes_received == 10 &&
                snapshot.duplicate_packets == 1 && snapshot.jitter_ms == 1.5 && snapshot.loss_percent() == 0.0,
            "statistics snapshot was incorrect");
}

void test_adaptive_and_liveness() {
    AdaptiveJitterBuffer buffer{AdaptiveJitterConfig{8, 1, 2, 4, 2ms}};
    const auto input = frames(8);
    for (const auto& frame : input) {
        const auto inserted = buffer.insert(frame, ClockTimePoint{});
        require(inserted == distributed_audio::timing::InsertResult::inserted,
                "adaptive insertion failed");
    }
    const auto initial = buffer.target_depth();
    for (int index = 0; index < 10; ++index) buffer.observe_jitter(5.0);
    require(buffer.target_depth() > initial && buffer.target_depth() <= 4,
            "adaptive jitter growth exceeded bounds");
    for (int index = 0; index < 30; ++index) buffer.observe_jitter(0.0);
    require(buffer.target_depth() >= 1 && buffer.target_depth() < 4,
            "adaptive jitter contraction failed");

    SessionLiveness liveness{RecoveryConfig{50ms, 100ms, 10ms, 2}};
    liveness.valid_traffic(ClockTimePoint{});
    liveness.update(ClockTimePoint{60ms});
    require(liveness.state() == ResilienceState::recovering, "liveness warning state failed");
    liveness.update(ClockTimePoint{120ms});
    require(liveness.state() == ResilienceState::interrupted, "liveness disconnect state failed");
    require(liveness.should_retry(ClockTimePoint{120ms}), "recovery attempt was not scheduled");
    liveness.peer_available(true, ClockTimePoint{130ms});
    require(liveness.state() == ResilienceState::streaming, "liveness recovery failed");
    liveness.stop();
    require(liveness.state() == ResilienceState::stopped, "liveness stop failed");
}

void test_end_to_end_faults() {
    const auto input = frames(100);
    distributed_audio::timing::ImpairmentPlan plan;
    for (std::uint64_t sequence = 0; sequence < input.size(); ++sequence) {
        if (sequence < 30 || sequence >= 35) plan.arrival_order.push_back(sequence);
    }
    plan.lost_sequences = {10, 11, 12, 13, 14, 15, 70};
    plan.duplicate_sequences = {20};
    plan.arrival_delays.assign(plan.arrival_order.size(), 1ms);
    const auto result = distributed_audio::resilience::run_deterministic_resilience(
        input, plan, AdaptiveJitterConfig{}, RecoveryConfig{});
    require(result.playback_sequence.size() > 50, "resilient playback stalled");
    require(result.statistics.concealed_frames > 0 && result.statistics.unrecoverable_packets > 0,
            "loss concealment was not accounted for");
    require(result.final_buffer_depth <= 16, "resilience buffer exceeded capacity");
}

}  // namespace

int main() {
    try { test_statistics(); test_adaptive_and_liveness(); test_end_to_end_faults(); }
    catch (const std::exception& error) { std::cerr << "Resilience test failure: " << error.what() << '\n'; return 1; }
    std::cout << "All network resilience tests passed.\n";
    return 0;
}
