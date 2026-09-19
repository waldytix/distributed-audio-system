#pragma once

#include "distributed_audio/audio_format.hpp"
#include "distributed_audio/pcm_conversion.hpp"

namespace distributed_audio {

[[nodiscard]] NormalizedSamples convert_channels(const NormalizedSamples& samples,
                                                  AudioFormat input_format,
                                                  std::uint16_t output_channels);

}  // namespace distributed_audio
