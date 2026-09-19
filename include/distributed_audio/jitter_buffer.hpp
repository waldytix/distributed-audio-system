#pragma once

#include "distributed_audio/audio_frame.hpp"
#include "distributed_audio/clock.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace distributed_audio::timing {

enum class BufferState {
    buffering,
    ready,
    draining,
    stopped
};

enum class ConcealmentPolicy {
    silence,
    repeat_previous
};

enum class InsertResult {
    inserted,
    duplicate,
    late,
    overflow,
    incompatible,
    stopped
};

enum class PullStatus {
    frame,
    missing,
    not_ready,
    stopped
};

struct PlaybackItem {
    PullStatus status{PullStatus::not_ready};
    std::uint64_t sequence_number{0};
    std::optional<AudioFrame> frame;
    bool concealed{false};
};

struct JitterBufferStatistics {
    std::uint64_t packets_inserted{0};
    std::uint64_t packets_reordered{0};
    std::uint64_t duplicates_rejected{0};
    std::uint64_t late_packets_rejected{0};
    std::uint64_t overflow_rejects{0};
    std::uint64_t incompatible_rejects{0};
    std::uint64_t missing_frames_declared{0};
    std::uint64_t concealed_frames_generated{0};
    std::size_t current_depth{0};
    std::size_t maximum_depth{0};
};

class JitterBuffer {
public:
    JitterBuffer(std::size_t capacity,
                 std::size_t prebuffer_target,
                 std::chrono::nanoseconds missing_wait,
                 ConcealmentPolicy concealment = ConcealmentPolicy::silence);

    [[nodiscard]] InsertResult insert(AudioFrame frame, ClockTimePoint arrival_time);
    [[nodiscard]] PlaybackItem pop(ClockTimePoint now);
    void close() noexcept;
    void reset() noexcept;

    [[nodiscard]] BufferState state() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] const JitterBufferStatistics& statistics() const noexcept;

private:
    [[nodiscard]] static std::uint64_t forward_distance(std::uint64_t from,
                                                         std::uint64_t to) noexcept;
    [[nodiscard]] std::optional<AudioFrame> make_concealed_frame() const;
    void refresh_state() noexcept;

    const std::size_t capacity_;
    const std::size_t prebuffer_target_;
    const std::chrono::nanoseconds missing_wait_;
    const ConcealmentPolicy concealment_;
    std::map<std::uint64_t, AudioFrame> frames_;
    std::optional<AudioFormat> format_;
    std::optional<std::chrono::nanoseconds> frame_duration_;
    std::optional<AudioFrame> previous_frame_;
    std::uint64_t next_sequence_{0};
    bool sequence_initialized_{false};
    bool waiting_for_missing_{false};
    ClockTimePoint missing_deadline_{};
    BufferState state_{BufferState::buffering};
    JitterBufferStatistics statistics_;
};

}  // namespace distributed_audio::timing
