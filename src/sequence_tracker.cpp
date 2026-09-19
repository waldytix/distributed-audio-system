#include "distributed_audio/sequence_tracker.hpp"

namespace distributed_audio::network {

SequenceDisposition SequenceTracker::observe(std::uint64_t sequence) noexcept {
    ++statistics_.packets_accepted;
    if (!has_last_sequence_) {
        has_last_sequence_ = true;
        last_sequence_ = sequence;
        return SequenceDisposition::first;
    }

    const auto delta = sequence - last_sequence_;
    if (delta == 0) {
        ++statistics_.duplicate_packets;
        return SequenceDisposition::duplicate;
    }
    if (delta < (std::uint64_t{1} << 63U)) {
        last_sequence_ = sequence;
        if (delta == 1) {
            return SequenceDisposition::expected;
        }
        statistics_.estimated_missing_packets += delta - 1;
        return SequenceDisposition::gap;
    }
    ++statistics_.out_of_order_packets;
    return SequenceDisposition::out_of_order;
}

void SequenceTracker::record_received() noexcept {
    ++statistics_.packets_received;
}

void SequenceTracker::record_malformed() noexcept {
    ++statistics_.malformed_packets;
}

const SequenceStatistics& SequenceTracker::statistics() const noexcept {
    return statistics_;
}

}  // namespace distributed_audio::network
