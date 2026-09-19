#include "distributed_audio/audio_meter.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace distributed_audio {

void AudioMeter::measure(const NormalizedSamples& samples,
                         AudioFormat format,
                         PeakLevels& output) const {
    if (!format.is_valid() || samples.size() % format.channel_count != 0) {
        throw std::invalid_argument("meter requires valid, frame-aligned audio");
    }
    output.per_channel.assign(format.channel_count, 0.0F);
    output.overall = 0.0F;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto magnitude = std::fabs(samples[index]);
        const auto channel = index % format.channel_count;
        output.per_channel[channel] = std::max(output.per_channel[channel], magnitude);
        output.overall = std::max(output.overall, magnitude);
    }
}

}  // namespace distributed_audio
