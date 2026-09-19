#pragma once

#include <cstdint>

namespace distributed_audio::network {

enum class SequenceDisposition {
    first,
    expected,
    duplicate,
    gap,
    out_of_order
};

struct SequenceStatistics {
    std::uint64_t packets_received{0};
    std::uint64_t packets_accepted{0};
    std::uint64_t malformed_packets{0};
    std::uint64_t duplicate_packets{0};
    std::uint64_t out_of_order_packets{0};
    std::uint64_t estimated_missing_packets{0};
};

class SequenceTracker {
public:
    [[nodiscard]] SequenceDisposition observe(std::uint64_t sequence) noexcept;
    void record_received() noexcept;
    void record_malformed() noexcept;
    [[nodiscard]] const SequenceStatistics& statistics() const noexcept;

private:
    SequenceStatistics statistics_;
    bool has_last_sequence_{false};
    std::uint64_t last_sequence_{0};
};

}  // namespace distributed_audio::network
