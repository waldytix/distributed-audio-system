#include "distributed_audio/channel_mixer.hpp"

#include <stdexcept>

namespace distributed_audio {

NormalizedSamples convert_channels(const NormalizedSamples& samples,
                                   AudioFormat input_format,
                                   std::uint16_t output_channels) {
    if (!input_format.is_valid() || output_channels == 0 ||
        (input_format.channel_count != 1 && input_format.channel_count != 2) ||
        samples.size() % input_format.channel_count != 0) {
        throw std::invalid_argument("unsupported or invalid channel conversion");
    }
    if (input_format.channel_count == output_channels) {
        return samples;
    }
    if (input_format.channel_count == 1 && output_channels == 2) {
        NormalizedSamples output;
        output.reserve(samples.size() * 2U);
        for (const auto sample : samples) {
            output.push_back(sample);
            output.push_back(sample);
        }
        return output;
    }
    if (input_format.channel_count == 2 && output_channels == 1) {
        NormalizedSamples output;
        output.reserve(samples.size() / 2U);
        for (std::size_t index = 0; index < samples.size(); index += 2) {
            output.push_back(clamp_normalized_sample((samples[index] + samples[index + 1]) * 0.5F));
        }
        return output;
    }
    throw std::invalid_argument("only mono and stereo channel conversion is supported");
}

}  // namespace distributed_audio
