#include "distributed_audio/timing_utils.hpp"

#include <cmath>
#include <limits>

namespace distributed_audio::timing {

std::optional<ClockDuration> frame_duration(const AudioFrame& frame) noexcept {
    const auto& format = frame.format();
    if (!format.is_valid() || format.channel_count == 0 || format.bits_per_sample == 0) {
        return std::nullopt;
    }
    const auto bytes_per_sample = static_cast<std::size_t>(format.bits_per_sample / 8);
    const auto bytes_per_frame = static_cast<std::size_t>(format.channel_count) * bytes_per_sample;
    if (bytes_per_frame == 0 || frame.payload().size() % bytes_per_frame != 0) {
        return std::nullopt;
    }
    const auto samples_per_channel = frame.payload().size() / bytes_per_frame;
    const auto rate = static_cast<std::uint64_t>(format.sample_rate);
    if (samples_per_channel > std::numeric_limits<std::uint64_t>::max() / 1'000'000'000ULL) {
        return std::nullopt;
    }
    const auto duration_ns = (static_cast<std::uint64_t>(samples_per_channel) * 1'000'000'000ULL) /
                             rate;
    if (duration_ns > static_cast<std::uint64_t>(std::numeric_limits<ClockDuration::rep>::max())) {
        return std::nullopt;
    }
    return ClockDuration{static_cast<ClockDuration::rep>(duration_ns)};
}

void JitterEstimator::update(AudioFrame::Timestamp sender_timestamp,
                             ClockTimePoint arrival_time) noexcept {
    const auto arrival_ns = static_cast<double>(arrival_time.time_since_epoch().count());
    const auto sender_ns = static_cast<double>(sender_timestamp.count());
    if (!initialized_) {
        initialized_ = true;
        previous_transit_ = arrival_ns - sender_ns;
        estimate_.samples = 1;
        return;
    }
    const auto transit = arrival_ns - sender_ns;
    const auto difference = std::abs(transit - previous_transit_);
    previous_transit_ = transit;
    estimate_.nanoseconds += (static_cast<double>(difference) - estimate_.nanoseconds) / 16.0;
    ++estimate_.samples;
}

void JitterEstimator::reset() noexcept {
    initialized_ = false;
    previous_transit_ = 0;
    estimate_ = {};
}

JitterEstimate JitterEstimator::estimate() const noexcept { return estimate_; }

}  // namespace distributed_audio::timing
