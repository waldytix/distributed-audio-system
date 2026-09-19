#include "distributed_audio/network_impairment.hpp"

#include <algorithm>
#include <stdexcept>

namespace distributed_audio::timing {
namespace {

bool contains(const std::vector<std::uint64_t>& values, std::uint64_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

const AudioFrame* find_frame(const std::vector<AudioFrame>& frames, std::uint64_t sequence) {
    const auto found = std::find_if(frames.begin(), frames.end(),
                                    [sequence](const AudioFrame& frame) {
                                        return frame.sequence_number() == sequence;
                                    });
    return found == frames.end() ? nullptr : &*found;
}

}  // namespace

std::vector<ImpairedFrame> apply_impairment(const std::vector<AudioFrame>& frames,
                                            const ImpairmentPlan& plan,
                                            ClockTimePoint start_time) {
    std::vector<std::uint64_t> order = plan.arrival_order;
    if (order.empty()) {
        order.reserve(frames.size());
        for (const auto& frame : frames) {
            order.push_back(frame.sequence_number());
        }
    }
    std::vector<ImpairedFrame> result;
    result.reserve(order.size() + plan.duplicate_sequences.size());
    auto delay = std::chrono::milliseconds::zero();
    std::size_t delay_index = 0;
    for (const auto sequence : order) {
        if (contains(plan.lost_sequences, sequence)) {
            continue;
        }
        const auto* frame = find_frame(frames, sequence);
        if (frame == nullptr) {
            throw std::invalid_argument("impairment plan references unknown sequence");
        }
        if (delay_index < plan.arrival_delays.size()) {
            delay = plan.arrival_delays[delay_index++];
        }
        if (delay < std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("impairment delay must not be negative");
        }
        result.push_back(ImpairedFrame{*frame, start_time + delay});
        if (contains(plan.duplicate_sequences, sequence)) {
            result.push_back(ImpairedFrame{*frame, start_time + delay});
        }
    }
    return result;
}

}  // namespace distributed_audio::timing
