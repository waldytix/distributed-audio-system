#pragma once

#include "distributed_audio/clock.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace distributed_audio::timing {

struct ClockEstimate {
    bool ready{false};
    double offset_nanoseconds{0.0};
    double rate{1.0};
    double drift_ppm{0.0};
};

class ClockEstimator {
public:
    explicit ClockEstimator(std::size_t maximum_samples = 32);

    [[nodiscard]] bool add_sample(std::chrono::nanoseconds remote_time,
                                  ClockTimePoint local_time) noexcept;
    [[nodiscard]] std::optional<ClockTimePoint> map_remote_to_local(
        std::chrono::nanoseconds remote_time) const noexcept;
    void reset() noexcept;
    [[nodiscard]] ClockEstimate estimate() const noexcept;

private:
    struct Sample {
        double remote_delta;
        double local_delta;
    };

    void recalculate() noexcept;

    const std::size_t maximum_samples_;
    std::vector<Sample> samples_;
    std::int64_t remote_origin_{0};
    std::int64_t local_origin_{0};
    std::int64_t last_remote_{0};
    std::int64_t last_local_{0};
    bool has_origin_{false};
    ClockEstimate estimate_;
};

}  // namespace distributed_audio::timing
