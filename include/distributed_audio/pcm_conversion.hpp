#pragma once

#include "distributed_audio/audio_frame.hpp"

#include <cstdint>
#include <vector>

namespace distributed_audio {

using NormalizedSamples = std::vector<float>;

[[nodiscard]] NormalizedSamples pcm16_to_float(const AudioFrame& frame);

[[nodiscard]] AudioFrame::Payload float_to_pcm16(const NormalizedSamples& samples,
                                                  AudioFormat format);

[[nodiscard]] float clamp_normalized_sample(float sample) noexcept;

}  // namespace distributed_audio
