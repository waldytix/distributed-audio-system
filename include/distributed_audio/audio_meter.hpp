#pragma once

#include "distributed_audio/audio_format.hpp"
#include "distributed_audio/pcm_conversion.hpp"

#include <vector>

namespace distributed_audio {

struct PeakLevels {
    std::vector<float> per_channel;
    float overall{0.0F};
};

class AudioMeter {
public:
    void measure(const NormalizedSamples& samples,
                 AudioFormat format,
                 PeakLevels& output) const;
};

}  // namespace distributed_audio
