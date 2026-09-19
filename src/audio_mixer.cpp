#include "distributed_audio/audio_mixer.hpp"

#include <stdexcept>

namespace distributed_audio {

std::optional<ProcessedAudio> AudioMixer::mix(
    const std::vector<ProcessedAudio>& inputs) const {
    if (inputs.empty()) {
        return std::nullopt;
    }
    const auto& first = inputs.front();
    if (!first.format.is_valid()) {
        throw std::invalid_argument("cannot mix audio with an invalid format");
    }

    ProcessedAudio output{first.format, first.sequence_number, first.timestamp,
                          NormalizedSamples(first.samples.size(), 0.0F)};
    for (const auto& input : inputs) {
        if (input.format != first.format || input.samples.size() != first.samples.size()) {
            throw std::invalid_argument("audio mixer inputs must have compatible formats and sizes");
        }
        for (std::size_t index = 0; index < input.samples.size(); ++index) {
            output.samples[index] += input.samples[index];
        }
    }
    for (auto& sample : output.samples) {
        sample = clamp_normalized_sample(sample);
    }
    return output;
}

}  // namespace distributed_audio
