#include "distributed_audio/clock_estimator.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace distributed_audio::timing {

ClockEstimator::ClockEstimator(std::size_t maximum_samples)
    : maximum_samples_(maximum_samples) {
    if (maximum_samples_ == 0) {
        throw std::invalid_argument("clock estimator requires sample capacity");
    }
    samples_.reserve(maximum_samples_);
}

bool ClockEstimator::add_sample(std::chrono::nanoseconds remote_time,
                                ClockTimePoint local_time) noexcept {
    const auto remote = remote_time.count();
    const auto local = local_time.time_since_epoch().count();
    if (!has_origin_) {
        remote_origin_ = remote;
        local_origin_ = local;
        last_remote_ = remote;
        last_local_ = local;
        has_origin_ = true;
        samples_.push_back({0.0, 0.0});
        estimate_ = {};
        return true;
    }
    if (remote < last_remote_ || local < last_local_) {
        return false;
    }
    const auto remote_delta = static_cast<double>(remote) - static_cast<double>(remote_origin_);
    const auto local_delta = static_cast<double>(local) - static_cast<double>(local_origin_);
    if (!std::isfinite(remote_delta) || !std::isfinite(local_delta)) {
        return false;
    }
    if (samples_.size() == maximum_samples_) {
        samples_.erase(samples_.begin());
    }
    samples_.push_back({remote_delta, local_delta});
    last_remote_ = remote;
    last_local_ = local;
    recalculate();
    return true;
}

void ClockEstimator::recalculate() noexcept {
    if (samples_.size() < 2) {
        estimate_.ready = false;
        return;
    }
    long double mean_remote = 0.0L;
    long double mean_local = 0.0L;
    for (const auto& sample : samples_) {
        mean_remote += sample.remote_delta;
        mean_local += sample.local_delta;
    }
    mean_remote /= static_cast<long double>(samples_.size());
    mean_local /= static_cast<long double>(samples_.size());
    long double covariance = 0.0L;
    long double variance = 0.0L;
    for (const auto& sample : samples_) {
        const auto remote = static_cast<long double>(sample.remote_delta) - mean_remote;
        const auto local = static_cast<long double>(sample.local_delta) - mean_local;
        covariance += remote * local;
        variance += remote * remote;
    }
    if (variance <= 0.0L) {
        estimate_.ready = false;
        return;
    }
    const auto rate = static_cast<double>(covariance / variance);
    const auto intercept = static_cast<double>(mean_local - covariance / variance * mean_remote);
    if (!std::isfinite(rate) || !std::isfinite(intercept)) {
        estimate_.ready = false;
        return;
    }
    estimate_.ready = true;
    estimate_.rate = rate;
    estimate_.offset_nanoseconds = static_cast<double>(local_origin_) -
                                   static_cast<double>(remote_origin_) + intercept;
    estimate_.drift_ppm = (rate - 1.0) * 1'000'000.0;
}

std::optional<ClockTimePoint> ClockEstimator::map_remote_to_local(
    std::chrono::nanoseconds remote_time) const noexcept {
    if (!estimate_.ready || !has_origin_) {
        return std::nullopt;
    }
    const auto remote_delta = static_cast<double>(remote_time.count()) -
                              static_cast<double>(remote_origin_);
    const auto local_delta = estimate_.rate * remote_delta +
                             (estimate_.offset_nanoseconds -
                              (static_cast<double>(local_origin_) -
                               static_cast<double>(remote_origin_)));
    const auto local = static_cast<long double>(local_origin_) + local_delta;
    if (!std::isfinite(static_cast<double>(local)) ||
        local < static_cast<long double>(std::numeric_limits<ClockDuration::rep>::min()) ||
        local > static_cast<long double>(std::numeric_limits<ClockDuration::rep>::max())) {
        return std::nullopt;
    }
    return ClockTimePoint{ClockDuration{static_cast<ClockDuration::rep>(local)}};
}

void ClockEstimator::reset() noexcept {
    samples_.clear();
    remote_origin_ = 0;
    local_origin_ = 0;
    last_remote_ = 0;
    last_local_ = 0;
    has_origin_ = false;
    estimate_ = {};
}

ClockEstimate ClockEstimator::estimate() const noexcept { return estimate_; }

}  // namespace distributed_audio::timing
