#pragma once

#include "distributed_audio/pcm_conversion.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace distributed_audio {

struct ProcessedAudio {
    AudioFormat format;
    std::uint64_t sequence_number{0};
    AudioFrame::Timestamp timestamp{};
    NormalizedSamples samples;
};

class AudioMixer {
public:
    [[nodiscard]] std::optional<ProcessedAudio> mix(
        const std::vector<ProcessedAudio>& inputs) const;
};

}  // namespace distributed_audio
