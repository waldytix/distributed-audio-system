#pragma once

#include "distributed_audio/audio_format.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace distributed_audio {

class AudioFrame {
public:
    using Timestamp = std::chrono::nanoseconds;
    using Payload = std::vector<std::uint8_t>;

    AudioFrame(AudioFormat format,
               std::uint64_t sequence_number,
               Timestamp timestamp,
               Payload payload);

    [[nodiscard]] const AudioFormat& format() const noexcept;
    [[nodiscard]] std::uint64_t sequence_number() const noexcept;
    [[nodiscard]] Timestamp timestamp() const noexcept;
    [[nodiscard]] const Payload& payload() const noexcept;

private:
    AudioFormat format_;
    std::uint64_t sequence_number_;
    Timestamp timestamp_;
    Payload payload_;
};

}  // namespace distributed_audio
