#pragma once

#include "distributed_audio/audio_frame.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace distributed_audio::network {

enum class ParseError {
    none,
    truncated_header,
    invalid_magic,
    unsupported_version,
    invalid_flags,
    invalid_sample_rate,
    invalid_channels,
    unsupported_sample_format,
    invalid_sample_count,
    invalid_payload_size,
    truncated_payload,
    excessive_packet_size,
    invalid_audio_frame
};

struct ParseResult {
    std::optional<AudioFrame> frame;
    ParseError error{ParseError::none};

    [[nodiscard]] explicit operator bool() const noexcept { return frame.has_value(); }
};

[[nodiscard]] std::vector<std::uint8_t> serialize_audio_frame(const AudioFrame& frame);
[[nodiscard]] ParseResult deserialize_audio_frame(const std::vector<std::uint8_t>& packet) noexcept;
[[nodiscard]] std::string_view parse_error_message(ParseError error) noexcept;

}  // namespace distributed_audio::network
