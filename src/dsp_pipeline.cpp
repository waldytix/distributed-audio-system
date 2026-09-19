#include "distributed_audio/dsp_pipeline.hpp"

#include "distributed_audio/channel_mixer.hpp"
#include "distributed_audio/pcm_conversion.hpp"

#include <utility>

namespace distributed_audio {

DspPipeline::DspPipeline(GainProcessor gain,
                         std::optional<std::uint16_t> output_channels)
    : gain_(std::move(gain)), output_channels_(output_channels) {}

DspResult DspPipeline::process(const AudioFrame& frame) const {
    ProcessedAudio audio{frame.format(), frame.sequence_number(), frame.timestamp(),
                         pcm16_to_float(frame)};
    if (output_channels_.has_value() && *output_channels_ != audio.format.channel_count) {
        audio.samples = convert_channels(audio.samples, audio.format, *output_channels_);
        audio.format.channel_count = *output_channels_;
    }
    gain_.process(audio.samples);

    PeakLevels peaks;
    meter_.measure(audio.samples, audio.format, peaks);
    return DspResult{std::move(audio), std::move(peaks)};
}

}  // namespace distributed_audio
