#include "distributed_audio/audio_mixer.hpp"
#include "distributed_audio/audio_meter.hpp"
#include "distributed_audio/channel_mixer.hpp"
#include "distributed_audio/dsp_pipeline.hpp"
#include "distributed_audio/gain_processor.hpp"
#include "distributed_audio/pcm_conversion.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using distributed_audio::AudioFormat;
using distributed_audio::AudioFrame;
using distributed_audio::AudioMeter;
using distributed_audio::AudioMixer;
using distributed_audio::DspPipeline;
using distributed_audio::GainProcessor;
using distributed_audio::NormalizedSamples;
using distributed_audio::PeakLevels;
using distributed_audio::ProcessedAudio;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

AudioFrame make_frame(const NormalizedSamples& samples,
                      AudioFormat format = AudioFormat{48'000, 2, 16}) {
    return AudioFrame{format, 9, AudioFrame::Timestamp{1234},
                      distributed_audio::float_to_pcm16(samples, format)};
}

ProcessedAudio make_audio(NormalizedSamples samples,
                          AudioFormat format = AudioFormat{48'000, 1, 16}) {
    return ProcessedAudio{format, 5, AudioFrame::Timestamp{99}, std::move(samples)};
}

void test_pcm_conversion() {
    const AudioFormat format{48'000, 1, 16};
    const NormalizedSamples samples{-1.0F, -0.5F, 0.0F, 0.5F, 1.0F};
    const auto payload = distributed_audio::float_to_pcm16(samples, format);
    const auto frame = AudioFrame{format, 1, AudioFrame::Timestamp{}, payload};
    const auto converted = distributed_audio::pcm16_to_float(frame);

    require_near(converted[0], -1.0F, 0.0001F, "negative full scale conversion failed");
    require_near(converted[1], -0.5F, 0.0001F, "negative half scale conversion failed");
    require_near(converted[2], 0.0F, 0.0001F, "zero conversion failed");
    require_near(converted[3], 0.5F, 0.0001F, "positive half scale conversion failed");
    require_near(converted[4], 32767.0F / 32768.0F, 0.0001F,
                 "positive full scale conversion failed");

    const auto clipped = distributed_audio::float_to_pcm16(
        NormalizedSamples{-2.0F, 2.0F}, AudioFormat{48'000, 1, 16});
    const auto clipped_frame = AudioFrame{format, 2, AudioFrame::Timestamp{}, clipped};
    const auto clipped_samples = distributed_audio::pcm16_to_float(clipped_frame);
    require_near(clipped_samples[0], -1.0F, 0.0001F, "negative clipping failed");
    require_near(clipped_samples[1], 32767.0F / 32768.0F, 0.0001F,
                 "positive clipping failed");
}

void test_gain() {
    require_near(GainProcessor::decibels_to_linear(0.0F), 1.0F, 0.0001F,
                 "zero dB conversion failed");
    require_near(GainProcessor::decibels_to_linear(-6.0F), 0.501187F, 0.0001F,
                 "negative dB conversion failed");
    require_near(GainProcessor::decibels_to_linear(6.0F), 1.995262F, 0.0001F,
                 "positive dB conversion failed");

    NormalizedSamples samples{-0.5F, 0.5F, 0.75F};
    GainProcessor gain;
    gain.set_decibels(6.0F);
    gain.process(samples);
    require_near(samples[0], -0.997631F, 0.0001F, "gain negative application failed");
    require_near(samples[1], 0.997631F, 0.0001F, "gain application failed");
    require_near(samples[2], 1.0F, 0.0001F, "gain positive clipping failed");

    gain.set_bypass(true);
    const auto before_bypass = samples;
    gain.process(samples);
    require(samples == before_bypass, "bypass modified samples");
}

void test_channel_mixing() {
    const auto stereo = distributed_audio::convert_channels(
        NormalizedSamples{0.25F, -0.5F}, AudioFormat{48'000, 1, 16}, 2);
    require(stereo.size() == 4, "mono to stereo size failed");
    require_near(stereo[0], 0.25F, 0.0001F, "mono to stereo left failed");
    require_near(stereo[1], 0.25F, 0.0001F, "mono to stereo right failed");

    const auto mono = distributed_audio::convert_channels(
        NormalizedSamples{0.75F, -0.25F, -0.5F, 0.5F}, AudioFormat{48'000, 2, 16}, 1);
    require_near(mono[0], 0.25F, 0.0001F, "stereo to mono first frame failed");
    require_near(mono[1], 0.0F, 0.0001F, "stereo to mono second frame failed");

    bool rejected = false;
    try {
        const auto ignored = distributed_audio::convert_channels(
            NormalizedSamples{0.0F}, AudioFormat{48'000, 4, 16}, 1);
        (void)ignored;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "unsupported channel conversion was accepted");
}

void test_mixer() {
    AudioMixer mixer;
    const auto mixed = mixer.mix({make_audio({0.25F, -0.5F}), make_audio({0.25F, 0.5F})});
    require(mixed.has_value(), "mixer rejected compatible inputs");
    require_near(mixed->samples[0], 0.5F, 0.0001F, "mixer sum failed");
    require_near(mixed->samples[1], 0.0F, 0.0001F, "mixer cancellation failed");

    const auto saturated = mixer.mix({make_audio({0.75F}), make_audio({0.75F})});
    require_near(saturated->samples[0], 1.0F, 0.0001F, "mixer saturation failed");
    require(!mixer.mix({}).has_value(), "empty mixer input should be empty");

    bool rejected = false;
    try {
        const auto ignored = mixer.mix(
            {make_audio({0.1F}), make_audio({0.1F}, AudioFormat{44'100, 1, 16})});
        (void)ignored;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "mixer accepted incompatible formats");
}

void test_meter() {
    AudioMeter meter;
    PeakLevels peaks;
    const AudioFormat format{48'000, 2, 16};
    meter.measure(NormalizedSamples{0.0F, 0.0F, 0.0F, 0.0F}, format, peaks);
    require_near(peaks.overall, 0.0F, 0.0001F, "silence meter failed");

    meter.measure(NormalizedSamples{-0.25F, 0.75F, 0.5F, -0.1F}, format, peaks);
    require_near(peaks.per_channel[0], 0.5F, 0.0001F, "left peak failed");
    require_near(peaks.per_channel[1], 0.75F, 0.0001F, "right peak failed");
    require_near(peaks.overall, 0.75F, 0.0001F, "overall peak failed");
}

void test_pipeline() {
    GainProcessor gain;
    gain.set_decibels(-6.0F);
    DspPipeline pipeline{gain, 1};
    const auto result = pipeline.process(make_frame(NormalizedSamples{0.5F, 0.5F}));

    require(result.audio.sequence_number == 9, "pipeline changed sequence metadata");
    require(result.audio.timestamp == AudioFrame::Timestamp{1234},
            "pipeline changed timestamp metadata");
    require(result.audio.format.channel_count == 1, "pipeline channel conversion failed");
    require_near(result.audio.samples[0], 0.250594F, 0.0002F,
                 "pipeline processing order or gain failed");
    require_near(result.peaks.overall, 0.250594F, 0.0002F, "pipeline metering failed");
}

}  // namespace

int main() {
    try {
        test_pcm_conversion();
        test_gain();
        test_channel_mixing();
        test_mixer();
        test_meter();
        test_pipeline();
    } catch (const std::exception& error) {
        std::cerr << "DSP test failure: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All DSP tests passed.\n";
    return 0;
}
