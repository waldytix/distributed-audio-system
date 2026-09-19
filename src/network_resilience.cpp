#include "distributed_audio/network_resilience.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace distributed_audio::resilience {

double NetworkStatisticsSnapshot::loss_percent() const noexcept {
    return packets_sent == 0 ? 0.0 : 100.0 * static_cast<double>(packets_sent - packets_accepted) /
                                             static_cast<double>(packets_sent);
}

void NetworkStatistics::packet_sent(std::size_t bytes) noexcept { std::lock_guard lock(mutex_); ++snapshot_.packets_sent; snapshot_.bytes_sent += bytes; }
void NetworkStatistics::packet_received(std::size_t bytes) noexcept { std::lock_guard lock(mutex_); ++snapshot_.packets_received; snapshot_.bytes_received += bytes; }
void NetworkStatistics::packet_accepted() noexcept { std::lock_guard lock(mutex_); ++snapshot_.packets_accepted; }
void NetworkStatistics::packet_rejected() noexcept { std::lock_guard lock(mutex_); ++snapshot_.packets_rejected; }
void NetworkStatistics::malformed() noexcept { std::lock_guard lock(mutex_); ++snapshot_.malformed_packets; }
void NetworkStatistics::duplicate() noexcept { std::lock_guard lock(mutex_); ++snapshot_.duplicate_packets; }
void NetworkStatistics::late() noexcept { std::lock_guard lock(mutex_); ++snapshot_.late_packets; }
void NetworkStatistics::out_of_order() noexcept { std::lock_guard lock(mutex_); ++snapshot_.out_of_order_packets; }
void NetworkStatistics::missing() noexcept { std::lock_guard lock(mutex_); ++snapshot_.missing_packets; }
void NetworkStatistics::recovered() noexcept { std::lock_guard lock(mutex_); ++snapshot_.recovered_packets; }
void NetworkStatistics::unrecoverable() noexcept { std::lock_guard lock(mutex_); ++snapshot_.unrecoverable_packets; }
void NetworkStatistics::concealed() noexcept { std::lock_guard lock(mutex_); ++snapshot_.concealed_frames; }
void NetworkStatistics::reconnect() noexcept { std::lock_guard lock(mutex_); ++snapshot_.reconnect_count; }
void NetworkStatistics::interrupted() noexcept { std::lock_guard lock(mutex_); ++snapshot_.session_interruptions; }
void NetworkStatistics::set_jitter(double value) noexcept { std::lock_guard lock(mutex_); snapshot_.jitter_ms = value; }
void NetworkStatistics::set_buffer_depth(std::size_t value) noexcept { std::lock_guard lock(mutex_); snapshot_.jitter_buffer_depth = value; }
NetworkStatisticsSnapshot NetworkStatistics::snapshot() const noexcept { std::lock_guard lock(mutex_); auto result = snapshot_; result.loss_percentage = result.loss_percent(); return result; }

AdaptiveJitterBuffer::AdaptiveJitterBuffer(AdaptiveJitterConfig config)
    : config_(config), target_depth_(config.initial_depth),
      buffer_(config.capacity, config.initial_depth, config.missing_wait) {
    if (config.capacity == 0 || config.minimum_depth == 0 || config.minimum_depth > config.initial_depth ||
        config.initial_depth > config.maximum_depth || config.maximum_depth > config.capacity) {
        throw std::invalid_argument("invalid adaptive jitter configuration");
    }
}

timing::InsertResult AdaptiveJitterBuffer::insert(AudioFrame frame, timing::ClockTimePoint arrival) { return buffer_.insert(std::move(frame), arrival); }
timing::PlaybackItem AdaptiveJitterBuffer::pop(timing::ClockTimePoint now) { return buffer_.pop(now); }
void AdaptiveJitterBuffer::observe_jitter(double milliseconds) noexcept {
    if (!std::isfinite(milliseconds) || milliseconds < 0.0) return;
    stable_jitter_ms_ = 0.875 * stable_jitter_ms_ + 0.125 * milliseconds;
    const auto desired = stable_jitter_ms_ > 2.0 ? target_depth_ + 1 : (stable_jitter_ms_ < 0.5 && target_depth_ > 1 ? target_depth_ - 1 : target_depth_);
    target_depth_ = std::clamp(desired, config_.minimum_depth, config_.maximum_depth);
}
void AdaptiveJitterBuffer::reset() noexcept { target_depth_ = config_.initial_depth; stable_jitter_ms_ = 0.0; buffer_.reset(); }
std::size_t AdaptiveJitterBuffer::target_depth() const noexcept { return target_depth_; }
std::size_t AdaptiveJitterBuffer::size() const noexcept { return buffer_.size(); }
const timing::JitterBufferStatistics& AdaptiveJitterBuffer::statistics() const noexcept { return buffer_.statistics(); }

SessionLiveness::SessionLiveness(RecoveryConfig config) : config_(config) {
    if (config_.warning_timeout <= std::chrono::nanoseconds::zero() || config_.disconnect_timeout <= config_.warning_timeout || config_.maximum_attempts == 0) throw std::invalid_argument("invalid recovery configuration");
}
void SessionLiveness::valid_traffic(timing::ClockTimePoint now) noexcept { last_valid_ = now; if (state_ == ResilienceState::recovering || state_ == ResilienceState::interrupted) { state_ = ResilienceState::streaming; attempts_ = 0; } }
void SessionLiveness::peer_available(bool available, timing::ClockTimePoint now) noexcept { peer_available_ = available; if (available) valid_traffic(now); else update(now); }
void SessionLiveness::update(timing::ClockTimePoint now) noexcept {
    if (state_ == ResilienceState::stopped || state_ == ResilienceState::failed) return;
    if (!peer_available_ || now - last_valid_ >= config_.disconnect_timeout) state_ = ResilienceState::interrupted;
    else if (now - last_valid_ >= config_.warning_timeout) state_ = ResilienceState::recovering;
}
void SessionLiveness::stop() noexcept { state_ = ResilienceState::stopped; }
ResilienceState SessionLiveness::state() const noexcept { return state_; }
std::size_t SessionLiveness::reconnect_attempts() const noexcept { return attempts_; }
bool SessionLiveness::should_retry(timing::ClockTimePoint now) noexcept {
    if (state_ != ResilienceState::interrupted && state_ != ResilienceState::recovering) return false;
    if (attempts_ >= config_.maximum_attempts || now < next_retry_) { if (attempts_ >= config_.maximum_attempts) state_ = ResilienceState::failed; return false; }
    ++attempts_; next_retry_ = now + config_.retry_interval; state_ = ResilienceState::recovering; return true;
}

ResilienceResult run_deterministic_resilience(const std::vector<AudioFrame>& frames,
                                              const timing::ImpairmentPlan& impairment,
                                              AdaptiveJitterConfig jitter_config,
                                              RecoveryConfig recovery_config) {
    NetworkStatistics stats;
    AdaptiveJitterBuffer buffer{jitter_config};
    SessionLiveness liveness{recovery_config};
    const auto arrivals = timing::apply_impairment(frames, impairment);
    for (const auto& event : arrivals) {
        stats.packet_sent(event.frame.payload().size());
        stats.packet_received(event.frame.payload().size());
        const auto inserted = buffer.insert(event.frame, event.arrival_time);
        if (inserted == timing::InsertResult::inserted) stats.packet_accepted();
        else if (inserted == timing::InsertResult::duplicate) stats.duplicate();
        else if (inserted == timing::InsertResult::late) stats.late();
        else stats.packet_rejected();
        liveness.valid_traffic(event.arrival_time);
        liveness.update(event.arrival_time);
    }
    std::vector<std::uint64_t> playback;
    const auto step = frames.empty() ? std::chrono::nanoseconds{5'000'000} :
        timing::frame_duration(frames.front()).value_or(std::chrono::nanoseconds{5'000'000});
    for (std::size_t index = 0; index < frames.size() + 2; ++index) {
        const auto item = buffer.pop(timing::ClockTimePoint{step * static_cast<std::int64_t>(index)});
        if (item.status == timing::PullStatus::frame || item.status == timing::PullStatus::missing) {
            playback.push_back(item.sequence_number);
            if (item.concealed) { stats.concealed(); stats.missing(); stats.unrecoverable(); }
        }
    }
    stats.set_buffer_depth(buffer.size());
    return ResilienceResult{std::move(playback), stats.snapshot(), buffer.size(), liveness.state() == ResilienceState::streaming};
}

}  // namespace distributed_audio::resilience
