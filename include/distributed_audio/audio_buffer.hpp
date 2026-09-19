#pragma once

#include "distributed_audio/audio_frame.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace distributed_audio {

class AudioBuffer {
public:
    explicit AudioBuffer(std::size_t capacity);

    [[nodiscard]] bool push(AudioFrame frame);
    [[nodiscard]] std::optional<AudioFrame> pop();
    [[nodiscard]] bool try_push(AudioFrame frame);
    [[nodiscard]] std::optional<AudioFrame> try_pop();

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool full() const;
    void close() noexcept;
    [[nodiscard]] bool closed() const;

private:
    const std::size_t capacity_;
    std::deque<AudioFrame> frames_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    bool closed_{false};
};

}  // namespace distributed_audio
