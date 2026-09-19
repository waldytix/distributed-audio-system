#include "distributed_audio/gain_processor.hpp"

#include <cmath>
#include <stdexcept>

namespace distributed_audio {

GainProcessor::GainProcessor(float linear_gain) : linear_gain_(linear_gain) {
    set_linear_gain(linear_gain);
}

void GainProcessor::set_linear_gain(float linear_gain) {
    if (!std::isfinite(linear_gain) || linear_gain < 0.0F) {
        throw std::invalid_argument("linear gain must be finite and non-negative");
    }
    linear_gain_ = linear_gain;
}

void GainProcessor::set_decibels(float decibels) {
    set_linear_gain(decibels_to_linear(decibels));
}

void GainProcessor::set_bypass(bool bypass) noexcept {
    bypassed_ = bypass;
}

float GainProcessor::linear_gain() const noexcept {
    return linear_gain_;
}

bool GainProcessor::bypassed() const noexcept {
    return bypassed_;
}

void GainProcessor::process(NormalizedSamples& samples) const noexcept {
    if (bypassed_) {
        return;
    }
    for (auto& sample : samples) {
        sample = clamp_normalized_sample(sample * linear_gain_);
    }
}

float GainProcessor::decibels_to_linear(float decibels) noexcept {
    return std::pow(10.0F, decibels / 20.0F);
}

}  // namespace distributed_audio
