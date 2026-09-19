#pragma once

#include "distributed_audio/pcm_conversion.hpp"

namespace distributed_audio {

class GainProcessor {
public:
    explicit GainProcessor(float linear_gain = 1.0F);

    void set_linear_gain(float linear_gain);
    void set_decibels(float decibels);
    void set_bypass(bool bypass) noexcept;

    [[nodiscard]] float linear_gain() const noexcept;
    [[nodiscard]] bool bypassed() const noexcept;

    void process(NormalizedSamples& samples) const noexcept;

    [[nodiscard]] static float decibels_to_linear(float decibels) noexcept;

private:
    float linear_gain_;
    bool bypassed_{false};
};

}  // namespace distributed_audio
