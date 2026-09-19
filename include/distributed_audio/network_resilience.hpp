#pragma once

#include "distributed_audio/audio_frame.hpp"
#include "distributed_audio/clock.hpp"
#include "distributed_audio/jitter_buffer.hpp"
#include "distributed_audio/network_impairment.hpp"
#include "distributed_audio/timing_utils.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace distributed_audio::resilience {

struct NetworkStatisticsSnapshot {
    std::uint64_t packets_sent{0};
    std::uint64_t packets_received{0};
    std::uint64_t packets_accepted{0};
    std::uint64_t packets_rejected{0};
    std::uint64_t malformed_packets{0};
    std::uint64_t duplicate_packets{0};
    std::uint64_t late_packets{0};
    std::uint64_t out_of_order_packets{0};
    std::uint64_t missing_packets{0};
    std::uint64_t recovered_packets{0};
    std::uint64_t unrecoverable_packets{0};
    std::uint64_t concealed_frames{0};
    std::uint64_t bytes_sent{0};
    std::uint64_t bytes_received{0};
    std::uint64_t playback_underruns{0};
    std::uint64_t capture_overflows{0};
    std::uint64_t reconnect_count{0};
    std::uint64_t session_interruptions{0};
    double loss_percentage{0.0};
    double jitter_ms{0.0};
    std::size_t jitter_buffer_depth{0};

    [[nodiscard]] double loss_percent() const noexcept;
};

class NetworkStatistics {
public:
    void packet_sent(std::size_t bytes) noexcept;
    void packet_received(std::size_t bytes) noexcept;
    void packet_accepted() noexcept;
    void packet_rejected() noexcept;
    void malformed() noexcept;
    void duplicate() noexcept;
    void late() noexcept;
    void out_of_order() noexcept;
    void missing() noexcept;
    void recovered() noexcept;
    void unrecoverable() noexcept;
    void concealed() noexcept;
    void reconnect() noexcept;
    void interrupted() noexcept;
    void set_jitter(double milliseconds) noexcept;
    void set_buffer_depth(std::size_t depth) noexcept;
    [[nodiscard]] NetworkStatisticsSnapshot snapshot() const noexcept;

private:
    mutable std::mutex mutex_;
    NetworkStatisticsSnapshot snapshot_;
};

struct AdaptiveJitterConfig {
    std::size_t capacity{16};
    std::size_t minimum_depth{1};
    std::size_t initial_depth{2};
    std::size_t maximum_depth{8};
    std::chrono::nanoseconds missing_wait{std::chrono::milliseconds{2}};
};

class AdaptiveJitterBuffer {
public:
    explicit AdaptiveJitterBuffer(AdaptiveJitterConfig config);

    [[nodiscard]] timing::InsertResult insert(AudioFrame frame,
                                               timing::ClockTimePoint arrival);
    [[nodiscard]] timing::PlaybackItem pop(timing::ClockTimePoint now);
    void observe_jitter(double milliseconds) noexcept;
    void reset() noexcept;
    [[nodiscard]] std::size_t target_depth() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const timing::JitterBufferStatistics& statistics() const noexcept;

private:
    AdaptiveJitterConfig config_;
    std::size_t target_depth_;
    double stable_jitter_ms_{0.0};
    timing::JitterBuffer buffer_;
};

enum class ResilienceState {
    streaming,
    interrupted,
    recovering,
    failed,
    stopped
};

struct RecoveryConfig {
    std::chrono::nanoseconds warning_timeout{std::chrono::milliseconds{50}};
    std::chrono::nanoseconds disconnect_timeout{std::chrono::milliseconds{150}};
    std::chrono::nanoseconds retry_interval{std::chrono::milliseconds{20}};
    std::size_t maximum_attempts{3};
};

class SessionLiveness {
public:
    explicit SessionLiveness(RecoveryConfig config);
    void valid_traffic(timing::ClockTimePoint now) noexcept;
    void peer_available(bool available, timing::ClockTimePoint now) noexcept;
    void update(timing::ClockTimePoint now) noexcept;
    void stop() noexcept;
    [[nodiscard]] ResilienceState state() const noexcept;
    [[nodiscard]] std::size_t reconnect_attempts() const noexcept;
    [[nodiscard]] bool should_retry(timing::ClockTimePoint now) noexcept;

private:
    RecoveryConfig config_;
    timing::ClockTimePoint last_valid_{};
    timing::ClockTimePoint next_retry_{};
    ResilienceState state_{ResilienceState::streaming};
    std::size_t attempts_{0};
    bool peer_available_{true};
};

struct ResilienceResult {
    std::vector<std::uint64_t> playback_sequence;
    NetworkStatisticsSnapshot statistics;
    std::size_t final_buffer_depth{0};
    bool recovered{false};
};

[[nodiscard]] ResilienceResult run_deterministic_resilience(
    const std::vector<AudioFrame>& frames,
    const timing::ImpairmentPlan& impairment,
    AdaptiveJitterConfig jitter_config,
    RecoveryConfig recovery_config);

}  // namespace distributed_audio::resilience
