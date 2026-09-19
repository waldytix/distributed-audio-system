#include "distributed_audio/audio_frame.hpp"

#include <stdexcept>
#include <utility>

namespace distributed_audio {

AudioFrame::AudioFrame(AudioFormat format,
                       std::uint64_t sequence_number,
                       Timestamp timestamp,
                       Payload payload)
    : format_(format),
      sequence_number_(sequence_number),
      timestamp_(timestamp),
      payload_(std::move(payload)) {
    if (!format_.is_valid()) {
        throw std::invalid_argument("audio frame requires a valid audio format");
    }

    const auto bytes_per_sample = format_.bits_per_sample / 8;
    const auto bytes_per_frame = static_cast<std::size_t>(format_.channel_count) *
                                 bytes_per_sample;
    if (bytes_per_frame == 0 || payload_.size() % bytes_per_frame != 0) {
        throw std::invalid_argument("audio frame payload is not sample-frame aligned");
    }
}

const AudioFormat& AudioFrame::format() const noexcept {
    return format_;
}

std::uint64_t AudioFrame::sequence_number() const noexcept {
    return sequence_number_;
}

AudioFrame::Timestamp AudioFrame::timestamp() const noexcept {
    return timestamp_;
}

const AudioFrame::Payload& AudioFrame::payload() const noexcept {
    return payload_;
}

}  // namespace distributed_audio
