#include "distributed_audio/network_protocol.hpp"

namespace distributed_audio::network {

std::string_view sample_format_name(SampleFormat format) noexcept {
    if (format == SampleFormat::pcm_s16_le) {
        return "PCM16 little-endian";
    }
    return "unknown";
}

}  // namespace distributed_audio::network
