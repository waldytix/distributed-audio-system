#pragma once

#include "distributed_audio/audio_mixer.hpp"
#include "distributed_audio/audio_meter.hpp"
#include "distributed_audio/gain_processor.hpp"

#include <cstdint>
#include <optional>

namespace distributed_audio {

struct DspResult {
    ProcessedAudio audio;
    PeakLevels peaks;
};

class DspPipeline {
public:
    explicit DspPipeline(GainProcessor gain = GainProcessor{},
                         std::optional<std::uint16_t> output_channels = std::nullopt);

    [[nodiscard]] DspResult process(const AudioFrame& frame) const;

private:
    GainProcessor gain_;
    std::optional<std::uint16_t> output_channels_;
    AudioMeter meter_;
};

}  // namespace distributed_audio
