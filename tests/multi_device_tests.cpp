#include "distributed_audio/multi_device_sync.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using distributed_audio::AudioFormat;
using distributed_audio::AudioFrame;
using distributed_audio::timing::ClockTimePoint;
using distributed_audio::timing::EndpointConfig;
using distributed_audio::timing::ImpairmentPlan;
using distributed_audio::timing::PlaybackEndpoint;
using distributed_audio::timing::SynchronizationController;
using distributed_audio::timing::SynchronizationParameters;
using distributed_audio::timing::SynchronizationState;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

AudioFrame make_frame(std::uint64_t sequence) {
    return AudioFrame{AudioFormat{48'000, 2, 16}, sequence,
                      AudioFrame::Timestamp{static_cast<std::int64_t>(sequence - 1) * 5'000'000},
                      AudioFrame::Payload(960, static_cast<std::uint8_t>(sequence))};
}

std::vector<AudioFrame> make_stream(std::size_t count) {
    std::vector<AudioFrame> frames;
    frames.reserve(count);
    for (std::uint64_t sequence = 1; sequence <= count; ++sequence) {
        frames.push_back(make_frame(sequence));
    }
    return frames;
}

void test_three_independent_endpoints_converge() {
    const auto frames = make_stream(80);
    EndpointConfig reference{"reference", 0ms, 0.0, {}};
    EndpointConfig fast{"fast", 8ms, 120.0, {}};
    EndpointConfig slow{"slow", -7ms, -100.0, {}};
    slow.impairment.arrival_order = {1, 3, 2};
    slow.impairment.duplicate_sequences = {3};
    slow.impairment.lost_sequences = {20, 55};
    slow.impairment.arrival_delays = {0ms, 3ms, 1ms};

    PlaybackEndpoint endpoint_reference{reference, 16, 2, 1ms};
    PlaybackEndpoint endpoint_fast{fast, 16, 2, 1ms};
    PlaybackEndpoint endpoint_slow{slow, 16, 2, 1ms};
    endpoint_reference.load(frames);
    endpoint_fast.load(frames);
    endpoint_slow.load(frames);
    std::vector<PlaybackEndpoint*> endpoints{
        &endpoint_reference, &endpoint_fast, &endpoint_slow};

    SynchronizationParameters parameters;
    parameters.tolerance = 500us;
    parameters.maximum_correction = 1ms;
    parameters.convergence_rate = 0.2;
    parameters.update_interval = 20ms;
    SynchronizationController controller{parameters};

    const auto initial_skew = controller.maximum_skew(endpoints, ClockTimePoint{});
    require(initial_skew >= 15ms, "initial endpoint offsets were not represented");
    for (int step = 0; step < 250; ++step) {
        const auto now = ClockTimePoint{step * parameters.update_interval};
        for (auto* endpoint : endpoints) {
            endpoint->advance(now);
        }
        controller.update(endpoints, now);
    }

    const auto final_skew = controller.maximum_skew(endpoints, ClockTimePoint{5s});
    require(final_skew <= 2ms, "endpoints did not converge toward the reference");
    require(endpoint_fast.metrics().maximum_applied_correction <= 250ms,
            "fast endpoint correction became unbounded");
    require(endpoint_slow.metrics().concealed_frames >= 1,
            "packet loss did not produce concealment");
    require(endpoint_slow.metrics().duplicates_rejected >= 1,
            "duplicate packet was not rejected");
    require(endpoint_reference.metrics().state == SynchronizationState::synchronized ||
                endpoint_fast.metrics().state == SynchronizationState::synchronized,
            "no endpoint reached synchronized state");
    require(endpoint_fast.metrics().estimated_drift_ppm > 0.0,
            "positive endpoint drift was not estimated");
    require(endpoint_slow.metrics().estimated_drift_ppm < 0.0,
            "negative endpoint drift was not estimated");
}

void test_controller_bounds_and_shutdown() {
    EndpointConfig config{"bounded", 100ms, 0.0, {}};
    PlaybackEndpoint endpoint{config, 4, 1, 1ms};
    endpoint.load(make_stream(2));
    SynchronizationParameters parameters;
    parameters.tolerance = 1ms;
    parameters.maximum_correction = 2ms;
    parameters.convergence_rate = 1.0;
    SynchronizationController controller{parameters};
    std::vector<PlaybackEndpoint*> endpoints{&endpoint};
    controller.update(endpoints, ClockTimePoint{});
    require(endpoint.metrics().maximum_applied_correction <= 2ms,
            "controller exceeded maximum correction per update");
    endpoint.stop();
    controller.update(endpoints, ClockTimePoint{1s});
    require(endpoint.metrics().state == SynchronizationState::stopped,
            "endpoint shutdown state was not preserved");
}

}  // namespace

int main() {
    try {
        test_three_independent_endpoints_converge();
        test_controller_bounds_and_shutdown();
    } catch (const std::exception& error) {
        std::cerr << "Multi-device test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All multi-device synchronization tests passed.\n";
    return 0;
}
