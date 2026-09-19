#pragma once

#include <cstdint>

namespace distributed_audio {

struct AudioFormat {
    std::uint32_t sample_rate{44'100};
    std::uint16_t channel_count{2};
    std::uint16_t bits_per_sample{16};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return sample_rate > 0 && channel_count > 0 && bits_per_sample > 0 &&
               bits_per_sample % 8 == 0;
    }

    [[nodiscard]] constexpr bool operator==(const AudioFormat& other) const noexcept {
        return sample_rate == other.sample_rate &&
               channel_count == other.channel_count &&
               bits_per_sample == other.bits_per_sample;
    }

    [[nodiscard]] constexpr bool operator!=(const AudioFormat& other) const noexcept {
        return !(*this == other);
    }
};

}  // namespace distributed_audio
