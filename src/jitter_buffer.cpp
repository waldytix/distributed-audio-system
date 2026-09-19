#include "distributed_audio/jitter_buffer.hpp"

#include "distributed_audio/timing_utils.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace distributed_audio::timing {

JitterBuffer::JitterBuffer(std::size_t capacity,
                           std::size_t prebuffer_target,
                           std::chrono::nanoseconds missing_wait,
                           ConcealmentPolicy concealment)
    : capacity_(capacity),
      prebuffer_target_(prebuffer_target),
      missing_wait_(missing_wait),
      concealment_(concealment) {
    if (capacity_ == 0 || prebuffer_target_ == 0 || prebuffer_target_ > capacity_ ||
        missing_wait_ < std::chrono::nanoseconds::zero()) {
        throw std::invalid_argument("invalid jitter buffer configuration");
    }
}

std::uint64_t JitterBuffer::forward_distance(std::uint64_t from,
                                              std::uint64_t to) noexcept {
    return to - from;
}

InsertResult JitterBuffer::insert(AudioFrame frame, ClockTimePoint arrival_time) {
    (void)arrival_time;
    if (state_ == BufferState::stopped) {
        return InsertResult::stopped;
    }
    const auto duration = frame_duration(frame);
    if (!duration.has_value() || duration->count() == 0) {
        ++statistics_.incompatible_rejects;
        return InsertResult::incompatible;
    }
    if (!format_.has_value()) {
        format_ = frame.format();
        frame_duration_ = duration;
    } else if (*format_ != frame.format() || *frame_duration_ != *duration) {
        ++statistics_.incompatible_rejects;
        return InsertResult::incompatible;
    }
    if (!sequence_initialized_) {
        next_sequence_ = frame.sequence_number();
        sequence_initialized_ = true;
    }
    const auto distance = forward_distance(next_sequence_, frame.sequence_number());
    if (distance >= (std::uint64_t{1} << 63U)) {
        ++statistics_.late_packets_rejected;
        return InsertResult::late;
    }
    if (frames_.find(frame.sequence_number()) != frames_.end()) {
        ++statistics_.duplicates_rejected;
        return InsertResult::duplicate;
    }
    if (frames_.size() >= capacity_) {
        ++statistics_.overflow_rejects;
        return InsertResult::overflow;
    }
    if (distance != frames_.size() && !frames_.empty()) {
        ++statistics_.packets_reordered;
    }
    frames_.emplace(frame.sequence_number(), std::move(frame));
    ++statistics_.packets_inserted;
    statistics_.current_depth = frames_.size();
    statistics_.maximum_depth = std::max(statistics_.maximum_depth, frames_.size());
    if (state_ == BufferState::buffering && frames_.size() >= prebuffer_target_) {
        state_ = BufferState::ready;
    }
    return InsertResult::inserted;
}

std::optional<AudioFrame> JitterBuffer::make_concealed_frame() const {
    if (!previous_frame_.has_value() || !frame_duration_.has_value()) {
        return std::nullopt;
    }
    auto payload = previous_frame_->payload();
    if (concealment_ == ConcealmentPolicy::silence) {
        std::fill(payload.begin(), payload.end(), std::uint8_t{0});
    }
    const auto timestamp = previous_frame_->timestamp() + *frame_duration_;
    return AudioFrame{previous_frame_->format(), next_sequence_, timestamp, std::move(payload)};
}

PlaybackItem JitterBuffer::pop(ClockTimePoint now) {
    if (state_ == BufferState::stopped && frames_.empty()) {
        return PlaybackItem{PullStatus::stopped, next_sequence_, std::nullopt, false};
    }
    if (!sequence_initialized_ || (state_ == BufferState::buffering && frames_.size() < prebuffer_target_)) {
        return PlaybackItem{PullStatus::not_ready, next_sequence_, std::nullopt, false};
    }

    const auto found = frames_.find(next_sequence_);
    if (found != frames_.end()) {
        auto frame = std::move(found->second);
        frames_.erase(found);
        previous_frame_ = frame;
        ++next_sequence_;
        waiting_for_missing_ = false;
        statistics_.current_depth = frames_.size();
        refresh_state();
        return PlaybackItem{PullStatus::frame, frame.sequence_number(), std::move(frame), false};
    }

    if (!waiting_for_missing_) {
        waiting_for_missing_ = true;
        missing_deadline_ = now + missing_wait_;
        return PlaybackItem{PullStatus::not_ready, next_sequence_, std::nullopt, false};
    }
    if (now < missing_deadline_) {
        return PlaybackItem{PullStatus::not_ready, next_sequence_, std::nullopt, false};
    }

    const auto concealed = make_concealed_frame();
    if (!concealed.has_value()) {
        state_ = BufferState::stopped;
        return PlaybackItem{PullStatus::stopped, next_sequence_, std::nullopt, false};
    }
    auto result = *concealed;
    ++next_sequence_;
    waiting_for_missing_ = false;
    ++statistics_.missing_frames_declared;
    ++statistics_.concealed_frames_generated;
    refresh_state();
    return PlaybackItem{PullStatus::missing, result.sequence_number(), std::move(result), true};
}

void JitterBuffer::close() noexcept {
    if (state_ != BufferState::stopped) {
        state_ = BufferState::draining;
    }
    refresh_state();
}

void JitterBuffer::reset() noexcept {
    frames_.clear();
    format_.reset();
    frame_duration_.reset();
    previous_frame_.reset();
    next_sequence_ = 0;
    sequence_initialized_ = false;
    waiting_for_missing_ = false;
    state_ = BufferState::buffering;
    statistics_ = {};
}

BufferState JitterBuffer::state() const noexcept { return state_; }
std::size_t JitterBuffer::size() const noexcept { return frames_.size(); }
std::size_t JitterBuffer::capacity() const noexcept { return capacity_; }
const JitterBufferStatistics& JitterBuffer::statistics() const noexcept { return statistics_; }

void JitterBuffer::refresh_state() noexcept {
    statistics_.current_depth = frames_.size();
    if (state_ == BufferState::draining && frames_.empty()) {
        state_ = BufferState::stopped;
    } else if (state_ == BufferState::buffering && frames_.size() >= prebuffer_target_) {
        state_ = BufferState::ready;
    }
}

}  // namespace distributed_audio::timing
