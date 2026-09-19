#include "distributed_audio/multi_device_sync.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace distributed_audio::timing {
namespace {

std::chrono::nanoseconds clamp_duration(std::chrono::nanoseconds value,
                                         std::chrono::nanoseconds limit) noexcept {
    return std::clamp(value, -limit, limit);
}

}  // namespace

PlaybackEndpoint::PlaybackEndpoint(EndpointConfig config,
                                   std::size_t jitter_capacity,
                                   std::size_t prebuffer_target,
                                   std::chrono::nanoseconds missing_wait)
    : config_(std::move(config)),
      jitter_buffer_(jitter_capacity, prebuffer_target, missing_wait),
            metrics_{} {
    if (config_.name.empty() || !std::isfinite(config_.drift_ppm)) {
        throw std::invalid_argument("invalid playback endpoint configuration");
    }
    metrics_.initial_offset = config_.initial_offset;
    metrics_.configured_drift_ppm = config_.drift_ppm;
}

void PlaybackEndpoint::load(const std::vector<AudioFrame>& frames,
                            ClockTimePoint simulation_start) {
    arrivals_ = apply_impairment(frames, config_.impairment, simulation_start);
    next_arrival_ = 0;
    loaded_ = true;
    metrics_.state = SynchronizationState::synchronizing;
}

void PlaybackEndpoint::advance(ClockTimePoint reference_time) {
    if (!loaded_ || stopped()) {
        return;
    }
    while (next_arrival_ < arrivals_.size() && arrivals_[next_arrival_].arrival_time <= reference_time) {
        auto event = arrivals_[next_arrival_++];
        ++metrics_.frames_received;
        jitter_estimator_.update(event.frame.timestamp(), event.arrival_time);
        const auto remote_ns = event.frame.timestamp().count();
        const auto drift = static_cast<long double>(config_.drift_ppm) / 1'000'000.0L;
        const auto local_ns = static_cast<long double>(remote_ns) * (1.0L + drift) +
                              static_cast<long double>(config_.initial_offset.count());
        const auto accepted_sample = clock_estimator_.add_sample(
            event.frame.timestamp(),
            ClockTimePoint{ClockDuration{static_cast<ClockDuration::rep>(local_ns)}});
        (void)accepted_sample;
        const auto result = jitter_buffer_.insert(std::move(event.frame), event.arrival_time);
        if (result == InsertResult::duplicate) {
            ++metrics_.duplicates_rejected;
        } else if (result == InsertResult::late) {
            ++metrics_.late_frames_rejected;
        }
    }
    const auto item = jitter_buffer_.pop(reference_time);
    playback_.consume(item);
    if (item.status == PullStatus::frame || item.status == PullStatus::missing) {
        ++metrics_.frames_played;
        if (item.concealed) {
            ++metrics_.concealed_frames;
        }
    }
    const auto estimate = clock_estimator_.estimate();
    metrics_.estimated_offset_ms = estimate.offset_nanoseconds / 1'000'000.0;
    metrics_.estimated_drift_ppm = estimate.drift_ppm;
    metrics_.missing_frames = jitter_buffer_.statistics().missing_frames_declared;
    metrics_.state = jitter_buffer_.state() == BufferState::stopped
                         ? SynchronizationState::stopped
                         : metrics_.state;
}

void PlaybackEndpoint::stop() noexcept {
    jitter_buffer_.close();
    metrics_.state = SynchronizationState::stopped;
}

const std::string& PlaybackEndpoint::name() const noexcept { return config_.name; }

std::chrono::nanoseconds PlaybackEndpoint::timing_error(
    ClockTimePoint reference_time) const noexcept {
    const auto elapsed = reference_time.time_since_epoch().count();
    const auto drift = static_cast<long double>(config_.drift_ppm) / 1'000'000.0L;
    const auto error = static_cast<long double>(config_.initial_offset.count()) +
                       drift * static_cast<long double>(elapsed) +
                       static_cast<long double>(correction_.count());
    if (error > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::chrono::nanoseconds::max();
    }
    if (error < static_cast<long double>(std::numeric_limits<std::int64_t>::min())) {
        return std::chrono::nanoseconds::min();
    }
    return std::chrono::nanoseconds{static_cast<std::int64_t>(error)};
}

std::chrono::nanoseconds PlaybackEndpoint::apply_correction(
    std::chrono::nanoseconds correction) noexcept {
    correction_ += correction;
    metrics_.applied_correction = correction_;
    metrics_.maximum_applied_correction = std::max(
        metrics_.maximum_applied_correction,
        std::chrono::nanoseconds{std::llabs(correction_.count())});
    return correction_;
}

const EndpointMetrics& PlaybackEndpoint::metrics() const noexcept { return metrics_; }
EndpointMetrics& PlaybackEndpoint::metrics() noexcept { return metrics_; }
const SimulatedPlaybackStatistics& PlaybackEndpoint::playback_statistics() const noexcept {
    return playback_.statistics();
}
bool PlaybackEndpoint::stopped() const noexcept {
    return metrics_.state == SynchronizationState::stopped;
}

SynchronizationController::SynchronizationController(SynchronizationParameters parameters)
    : parameters_(parameters) {
    if (parameters_.tolerance <= std::chrono::nanoseconds::zero() ||
        parameters_.maximum_correction <= std::chrono::nanoseconds::zero() ||
        parameters_.convergence_rate <= 0.0 || parameters_.convergence_rate > 1.0 ||
        parameters_.update_interval <= std::chrono::nanoseconds::zero()) {
        throw std::invalid_argument("invalid synchronization parameters");
    }
}

void SynchronizationController::update(
    const std::vector<PlaybackEndpoint*>& endpoints,
    ClockTimePoint reference_time) {
    for (auto* endpoint : endpoints) {
        if (endpoint == nullptr || endpoint->stopped()) {
            continue;
        }
        const auto error = endpoint->timing_error(reference_time);
        const auto absolute_error = std::chrono::nanoseconds{std::llabs(error.count())};
        const auto correction_value = static_cast<long double>(error.count()) *
                                       parameters_.convergence_rate;
        auto correction = std::chrono::nanoseconds{static_cast<std::int64_t>(-correction_value)};
        correction = clamp_duration(correction, parameters_.maximum_correction);
        const auto applied = endpoint->apply_correction(correction);
        (void)applied;
        const auto remaining = endpoint->timing_error(reference_time);
        auto& metrics = endpoint->metrics();
        metrics.current_error = remaining;
        if (std::chrono::nanoseconds{std::llabs(remaining.count())} <= parameters_.tolerance) {
            if (metrics.state != SynchronizationState::synchronized) {
                metrics.state = SynchronizationState::synchronized;
                metrics.convergence_time = elapsed_;
            }
        } else {
            metrics.state = absolute_error > parameters_.tolerance * 8
                                 ? SynchronizationState::degraded
                                 : SynchronizationState::synchronizing;
        }
    }
    elapsed_ += parameters_.update_interval;
}

const SynchronizationParameters& SynchronizationController::parameters() const noexcept {
    return parameters_;
}

std::chrono::nanoseconds SynchronizationController::maximum_skew(
    const std::vector<PlaybackEndpoint*>& endpoints,
    ClockTimePoint reference_time) const noexcept {
    if (endpoints.empty()) {
        return std::chrono::nanoseconds::zero();
    }
    auto minimum = endpoints.front()->timing_error(reference_time);
    auto maximum = minimum;
    for (const auto* endpoint : endpoints) {
        const auto error = endpoint->timing_error(reference_time);
        minimum = std::min(minimum, error);
        maximum = std::max(maximum, error);
    }
    return maximum - minimum;
}

}  // namespace distributed_audio::timing
