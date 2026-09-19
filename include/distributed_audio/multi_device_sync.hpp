#pragma once

#include "distributed_audio/clock.hpp"
#include "distributed_audio/clock_estimator.hpp"
#include "distributed_audio/jitter_buffer.hpp"
#include "distributed_audio/network_impairment.hpp"
#include "distributed_audio/simulated_playback.hpp"
#include "distributed_audio/timing_utils.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace distributed_audio::timing {

enum class SynchronizationState {
    synchronizing,
    synchronized,
    outside_tolerance,
    degraded,
    stopped
};

struct EndpointConfig {
    std::string name;
    std::chrono::nanoseconds initial_offset{0};
    double drift_ppm{0.0};
    ImpairmentPlan impairment;
};

struct SynchronizationParameters {
    std::chrono::nanoseconds tolerance{std::chrono::milliseconds{1}};
    std::chrono::nanoseconds maximum_correction{std::chrono::milliseconds{2}};
    double convergence_rate{0.25};
    std::chrono::nanoseconds update_interval{std::chrono::milliseconds{20}};
};

struct EndpointMetrics {
    std::chrono::nanoseconds initial_offset{0};
    double configured_drift_ppm{0.0};
    double estimated_offset_ms{0.0};
    double estimated_drift_ppm{0.0};
    std::chrono::nanoseconds current_error{0};
    std::chrono::nanoseconds applied_correction{0};
    std::chrono::nanoseconds maximum_applied_correction{0};
    SynchronizationState state{SynchronizationState::synchronizing};
    std::chrono::nanoseconds convergence_time{0};
    std::uint64_t frames_received{0};
    std::uint64_t frames_played{0};
    std::uint64_t missing_frames{0};
    std::uint64_t concealed_frames{0};
    std::uint64_t duplicates_rejected{0};
    std::uint64_t late_frames_rejected{0};
};

class PlaybackEndpoint {
public:
    PlaybackEndpoint(EndpointConfig config,
                     std::size_t jitter_capacity,
                     std::size_t prebuffer_target,
                     std::chrono::nanoseconds missing_wait);

    void load(const std::vector<AudioFrame>& frames,
              ClockTimePoint simulation_start = ClockTimePoint{});
    void advance(ClockTimePoint reference_time);
    void stop() noexcept;

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds timing_error(
        ClockTimePoint reference_time) const noexcept;
    [[nodiscard]] std::chrono::nanoseconds apply_correction(
        std::chrono::nanoseconds correction) noexcept;
    [[nodiscard]] const EndpointMetrics& metrics() const noexcept;
    [[nodiscard]] EndpointMetrics& metrics() noexcept;
    [[nodiscard]] const SimulatedPlaybackStatistics& playback_statistics() const noexcept;
    [[nodiscard]] bool stopped() const noexcept;

private:
    EndpointConfig config_;
    JitterBuffer jitter_buffer_;
    JitterEstimator jitter_estimator_;
    ClockEstimator clock_estimator_;
    SimulatedPlayback playback_;
    std::vector<ImpairedFrame> arrivals_;
    std::size_t next_arrival_{0};
    std::chrono::nanoseconds correction_{0};
    EndpointMetrics metrics_;
    bool loaded_{false};
};

class SynchronizationController {
public:
    explicit SynchronizationController(SynchronizationParameters parameters);

    void update(const std::vector<PlaybackEndpoint*>& endpoints,
                ClockTimePoint reference_time);
    [[nodiscard]] const SynchronizationParameters& parameters() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds maximum_skew(
        const std::vector<PlaybackEndpoint*>& endpoints,
        ClockTimePoint reference_time) const noexcept;

private:
    SynchronizationParameters parameters_;
    std::chrono::nanoseconds elapsed_{0};
};

}  // namespace distributed_audio::timing
