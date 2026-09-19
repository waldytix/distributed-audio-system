#include "distributed_audio/pcm_conversion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace distributed_audio {

float clamp_normalized_sample(float sample) noexcept {
    if (std::isnan(sample)) {
        return 0.0F;
    }
    return std::clamp(sample, -1.0F, 1.0F);
}

NormalizedSamples pcm16_to_float(const AudioFrame& frame) {
    if (!frame.format().is_valid() || frame.format().bits_per_sample != 16) {
        throw std::invalid_argument("PCM conversion requires a valid 16-bit format");
    }

    const auto& payload = frame.payload();
    if (payload.size() % sizeof(std::int16_t) != 0) {
        throw std::invalid_argument("16-bit PCM payload must contain complete samples");
    }

    NormalizedSamples samples;
    samples.reserve(payload.size() / sizeof(std::int16_t));
    for (std::size_t index = 0; index < payload.size(); index += 2) {
        const auto unsigned_value = static_cast<std::uint16_t>(payload[index]) |
                                    (static_cast<std::uint16_t>(payload[index + 1]) << 8U);
        const auto signed_value = static_cast<std::int16_t>(unsigned_value);
        samples.push_back(static_cast<float>(signed_value) / 32768.0F);
    }
    return samples;
}

AudioFrame::Payload float_to_pcm16(const NormalizedSamples& samples,
                                   AudioFormat format) {
    if (!format.is_valid() || format.bits_per_sample != 16) {
        throw std::invalid_argument("PCM conversion requires a valid 16-bit format");
    }
    const auto bytes_per_frame = static_cast<std::size_t>(format.channel_count) * 2U;
    if (samples.size() % format.channel_count != 0) {
        throw std::invalid_argument("samples must contain complete channel frames");
    }

    AudioFrame::Payload payload;
    payload.reserve(samples.size() * 2U);
    for (const auto sample : samples) {
        const auto clamped = clamp_normalized_sample(sample);
        const auto scaled = clamped <= -1.0F
                                ? -32768
                                : static_cast<int>(std::lround(clamped * 32767.0F));
        const auto value = static_cast<std::int16_t>(std::clamp(scaled, -32768, 32767));
        const auto unsigned_value = static_cast<std::uint16_t>(value);
        payload.push_back(static_cast<std::uint8_t>(unsigned_value & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((unsigned_value >> 8U) & 0xFFU));
    }

    if (payload.size() % bytes_per_frame != 0) {
        throw std::logic_error("PCM conversion produced an unaligned payload");
    }
    return payload;
}

}  // namespace distributed_audio
