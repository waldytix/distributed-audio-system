#include "distributed_audio/audio_buffer.hpp"

#include <stdexcept>
#include <utility>

namespace distributed_audio {

AudioBuffer::AudioBuffer(std::size_t capacity) : capacity_(capacity) {
    if (capacity_ == 0) {
        throw std::invalid_argument("audio buffer capacity must be greater than zero");
    }
}

bool AudioBuffer::push(AudioFrame frame) {
    std::unique_lock lock(mutex_);
    not_full_.wait(lock, [this] { return closed_ || frames_.size() < capacity_; });
    if (closed_) {
        return false;
    }

    frames_.push_back(std::move(frame));
    lock.unlock();
    not_empty_.notify_one();
    return true;
}

std::optional<AudioFrame> AudioBuffer::pop() {
    std::unique_lock lock(mutex_);
    not_empty_.wait(lock, [this] { return closed_ || !frames_.empty(); });
    if (frames_.empty()) {
        return std::nullopt;
    }

    AudioFrame frame = std::move(frames_.front());
    frames_.pop_front();
    lock.unlock();
    not_full_.notify_one();
    return frame;
}

bool AudioBuffer::try_push(AudioFrame frame) {
    std::lock_guard lock(mutex_);
    if (closed_ || frames_.size() >= capacity_) {
        return false;
    }

    frames_.push_back(std::move(frame));
    not_empty_.notify_one();
    return true;
}

std::optional<AudioFrame> AudioBuffer::try_pop() {
    std::lock_guard lock(mutex_);
    if (frames_.empty()) {
        return std::nullopt;
    }

    AudioFrame frame = std::move(frames_.front());
    frames_.pop_front();
    not_full_.notify_one();
    return frame;
}

std::size_t AudioBuffer::size() const {
    std::lock_guard lock(mutex_);
    return frames_.size();
}

std::size_t AudioBuffer::capacity() const noexcept {
    return capacity_;
}

bool AudioBuffer::empty() const {
    std::lock_guard lock(mutex_);
    return frames_.empty();
}

bool AudioBuffer::full() const {
    std::lock_guard lock(mutex_);
    return frames_.size() == capacity_;
}

void AudioBuffer::close() noexcept {
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    not_full_.notify_all();
    not_empty_.notify_all();
}

bool AudioBuffer::closed() const {
    std::lock_guard lock(mutex_);
    return closed_;
}

}  // namespace distributed_audio
