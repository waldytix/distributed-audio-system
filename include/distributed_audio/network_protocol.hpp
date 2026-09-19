#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace distributed_audio::network {

enum class SampleFormat : std::uint8_t {
    pcm_s16_le = 1
};

constexpr std::uint32_t kMagic = 0x44415331U;
constexpr std::uint8_t kProtocolVersion = 1;
constexpr std::size_t kHeaderSize = 40;
constexpr std::uint16_t kMaxChannels = 8;
constexpr std::uint32_t kMaxSampleRate = 384'000;
constexpr std::uint32_t kMaxSamplesPerChannel = 4'096;
constexpr std::uint32_t kMaxPayloadSize = 1'200;
constexpr std::size_t kMaxPacketSize = kHeaderSize + kMaxPayloadSize;

[[nodiscard]] std::string_view sample_format_name(SampleFormat format) noexcept;

}  // namespace distributed_audio::network
