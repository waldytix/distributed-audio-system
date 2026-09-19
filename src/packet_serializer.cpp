#include "distributed_audio/packet_serializer.hpp"

#include "distributed_audio/network_protocol.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace distributed_audio::network {
namespace {

void append_u8(std::vector<std::uint8_t>& output, std::uint8_t value) {
    output.push_back(value);
}

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint8_t read_u8(const std::vector<std::uint8_t>& packet, std::size_t& offset) {
    return packet[offset++];
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& packet, std::size_t& offset) {
    const auto value = static_cast<std::uint16_t>(packet[offset]) << 8U |
                       static_cast<std::uint16_t>(packet[offset + 1]);
    offset += 2;
    return value;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& packet, std::size_t& offset) {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value = (value << 8U) | packet[offset++];
    }
    return value;
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& packet, std::size_t& offset) {
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
        value = (value << 8U) | packet[offset++];
    }
    return value;
}

std::uint64_t timestamp_bits(AudioFrame::Timestamp timestamp) noexcept {
    return static_cast<std::uint64_t>(timestamp.count());
}

AudioFrame::Timestamp timestamp_from_bits(std::uint64_t value) noexcept {
    return AudioFrame::Timestamp{static_cast<std::int64_t>(value)};
}

ParseResult failure(ParseError error) noexcept {
    return ParseResult{std::nullopt, error};
}

}  // namespace

std::vector<std::uint8_t> serialize_audio_frame(const AudioFrame& frame) {
    const auto& format = frame.format();
    if (!format.is_valid() || format.bits_per_sample != 16 ||
        format.channel_count > kMaxChannels || format.sample_rate > kMaxSampleRate) {
        throw std::invalid_argument("audio frame format is not supported by network protocol");
    }
    const auto bytes_per_sample = std::size_t{2};
    const auto bytes_per_frame = static_cast<std::size_t>(format.channel_count) * bytes_per_sample;
    if (bytes_per_frame == 0 || frame.payload().size() % bytes_per_frame != 0) {
        throw std::invalid_argument("audio frame payload is not aligned");
    }
    const auto samples_per_channel = frame.payload().size() / bytes_per_frame;
    if (samples_per_channel > kMaxSamplesPerChannel || frame.payload().size() > kMaxPayloadSize) {
        throw std::invalid_argument("audio frame exceeds network protocol limits");
    }

    std::vector<std::uint8_t> packet;
    packet.reserve(kHeaderSize + frame.payload().size());
    append_u32(packet, kMagic);
    append_u8(packet, kProtocolVersion);
    append_u8(packet, 0);
    append_u16(packet, 0);
    append_u64(packet, frame.sequence_number());
    append_u64(packet, timestamp_bits(frame.timestamp()));
    append_u32(packet, format.sample_rate);
    append_u16(packet, format.channel_count);
    append_u8(packet, static_cast<std::uint8_t>(SampleFormat::pcm_s16_le));
    append_u8(packet, 0);
    append_u32(packet, static_cast<std::uint32_t>(samples_per_channel));
    append_u32(packet, static_cast<std::uint32_t>(frame.payload().size()));
    packet.insert(packet.end(), frame.payload().begin(), frame.payload().end());
    return packet;
}

ParseResult deserialize_audio_frame(const std::vector<std::uint8_t>& packet) noexcept {
    if (packet.size() < kHeaderSize) {
        return failure(ParseError::truncated_header);
    }
    if (packet.size() > kMaxPacketSize) {
        return failure(ParseError::excessive_packet_size);
    }

    std::size_t offset = 0;
    const auto magic = read_u32(packet, offset);
    const auto version = read_u8(packet, offset);
    const auto flags = read_u8(packet, offset);
    const auto reserved = read_u16(packet, offset);
    const auto sequence = read_u64(packet, offset);
    const auto timestamp = timestamp_from_bits(read_u64(packet, offset));
    const auto sample_rate = read_u32(packet, offset);
    const auto channels = read_u16(packet, offset);
    const auto sample_format = static_cast<SampleFormat>(read_u8(packet, offset));
    const auto sample_reserved = read_u8(packet, offset);
    const auto samples_per_channel = read_u32(packet, offset);
    const auto payload_size = read_u32(packet, offset);

    if (magic != kMagic) return failure(ParseError::invalid_magic);
    if (version != kProtocolVersion) return failure(ParseError::unsupported_version);
    if (flags != 0 || reserved != 0 || sample_reserved != 0) return failure(ParseError::invalid_flags);
    if (sample_rate == 0 || sample_rate > kMaxSampleRate) return failure(ParseError::invalid_sample_rate);
    if (channels == 0 || channels > kMaxChannels) return failure(ParseError::invalid_channels);
    if (sample_format != SampleFormat::pcm_s16_le) return failure(ParseError::unsupported_sample_format);
    if (samples_per_channel == 0 || samples_per_channel > kMaxSamplesPerChannel) {
        return failure(ParseError::invalid_sample_count);
    }
    const auto channels_size = static_cast<std::size_t>(channels);
    const auto samples_size = static_cast<std::size_t>(samples_per_channel);
    if (samples_size > std::numeric_limits<std::size_t>::max() / channels_size / 2U) {
        return failure(ParseError::invalid_payload_size);
    }
    const auto expected_payload = samples_size * channels_size * 2U;
    if (payload_size != expected_payload || payload_size > kMaxPayloadSize) {
        return failure(ParseError::invalid_payload_size);
    }
    if (packet.size() < kHeaderSize + static_cast<std::size_t>(payload_size)) {
        return failure(ParseError::truncated_payload);
    }
    if (packet.size() != kHeaderSize + static_cast<std::size_t>(payload_size)) {
        return failure(ParseError::invalid_payload_size);
    }

    AudioFormat format{sample_rate, channels, 16};
    try {
        AudioFrame::Payload payload(packet.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                                    packet.end());
        return ParseResult{AudioFrame{format, sequence, timestamp, std::move(payload)},
                           ParseError::none};
    } catch (...) {
        return failure(ParseError::invalid_audio_frame);
    }
}

std::string_view parse_error_message(ParseError error) noexcept {
    switch (error) {
    case ParseError::none: return "none";
    case ParseError::truncated_header: return "truncated header";
    case ParseError::invalid_magic: return "invalid magic";
    case ParseError::unsupported_version: return "unsupported version";
    case ParseError::invalid_flags: return "invalid flags";
    case ParseError::invalid_sample_rate: return "invalid sample rate";
    case ParseError::invalid_channels: return "invalid channels";
    case ParseError::unsupported_sample_format: return "unsupported sample format";
    case ParseError::invalid_sample_count: return "invalid sample count";
    case ParseError::invalid_payload_size: return "invalid payload size";
    case ParseError::truncated_payload: return "truncated payload";
    case ParseError::excessive_packet_size: return "excessive packet size";
    case ParseError::invalid_audio_frame: return "invalid audio frame";
    }
    return "unknown parse error";
}

}  // namespace distributed_audio::network
