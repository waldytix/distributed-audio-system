#include "distributed_audio/clock.hpp"
#include "distributed_audio/clock_estimator.hpp"
#include "distributed_audio/jitter_buffer.hpp"
#include "distributed_audio/network_impairment.hpp"
#include "distributed_audio/playback_scheduler.hpp"
#include "distributed_audio/simulated_playback.hpp"
#include "distributed_audio/timing_utils.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using distributed_audio::AudioFormat;
using distributed_audio::AudioFrame;
using distributed_audio::timing::BufferState;
using distributed_audio::timing::ClockEstimator;
using distributed_audio::timing::ClockTimePoint;
using distributed_audio::timing::ConcealmentPolicy;
using distributed_audio::timing::ImpairmentPlan;
using distributed_audio::timing::InsertResult;
using distributed_audio::timing::JitterBuffer;
using distributed_audio::timing::JitterEstimator;
using distributed_audio::timing::ManualClock;
using distributed_audio::timing::PlaybackScheduler;
using distributed_audio::timing::PullStatus;
using distributed_audio::timing::ScheduleDecision;
using distributed_audio::timing::SimulatedPlayback;
using distributed_audio::timing::apply_impairment;
using distributed_audio::timing::frame_duration;
using namespace std::chrono_literals;

constexpr auto kFrameDuration = std::chrono::milliseconds{5};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, double tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

AudioFrame make_frame(std::uint64_t sequence, std::int64_t timestamp_ms = 0) {
    const AudioFormat format{48'000, 2, 16};
    return AudioFrame{format, sequence, AudioFrame::Timestamp{timestamp_ms * 1'000'000},
                      AudioFrame::Payload(960, static_cast<std::uint8_t>(sequence))};
}

void insert(JitterBuffer& buffer, AudioFrame frame, ClockTimePoint arrival = ClockTimePoint{}) {
    require(buffer.insert(std::move(frame), arrival) == InsertResult::inserted,
            "frame insertion failed");
}

void test_frame_duration_and_jitter() {
    require(frame_duration(make_frame(1)) == std::chrono::milliseconds{5},
            "frame duration calculation failed");

    JitterEstimator estimator;
    estimator.update(AudioFrame::Timestamp{0}, ClockTimePoint{std::chrono::milliseconds{100}});
        estimator.update(std::chrono::duration_cast<AudioFrame::Timestamp>(kFrameDuration),
                     ClockTimePoint{std::chrono::milliseconds{105}});
        estimator.update(std::chrono::duration_cast<AudioFrame::Timestamp>(2 * kFrameDuration),
                     ClockTimePoint{std::chrono::milliseconds{110}});
    require_near(estimator.estimate().nanoseconds, 0.0, 0.001, "constant jitter was not zero");

        estimator.update(std::chrono::duration_cast<AudioFrame::Timestamp>(3 * kFrameDuration),
                     ClockTimePoint{std::chrono::milliseconds{117}});
    require(estimator.estimate().nanoseconds > 0.0, "variable jitter was not detected");
    estimator.reset();
    require(estimator.estimate().samples == 0, "jitter reset failed");
}

void test_jitter_buffer_order_and_states() {
    JitterBuffer buffer{8, 4, std::chrono::milliseconds{2}};
    insert(buffer, make_frame(100));
    insert(buffer, make_frame(102));
    insert(buffer, make_frame(101));
    require(buffer.state() == BufferState::buffering, "buffer became ready too early");
    insert(buffer, make_frame(103));
    require(buffer.state() == BufferState::ready, "buffer did not reach prebuffer target");

    for (std::uint64_t expected = 100; expected <= 103; ++expected) {
        const auto item = buffer.pop(ClockTimePoint{});
        require(item.status == PullStatus::frame && item.sequence_number == expected,
                "jitter buffer order was incorrect");
    }
    require(buffer.statistics().packets_reordered >= 1, "reordering was not counted");

    buffer.close();
    require(buffer.state() == BufferState::stopped, "empty closed buffer did not stop");
    require(buffer.pop(ClockTimePoint{}).status == PullStatus::stopped,
            "stopped buffer returned an item");
}

void test_jitter_buffer_missing_late_overflow_reset() {
    JitterBuffer buffer{2, 1, std::chrono::milliseconds{10}, ConcealmentPolicy::silence};
    insert(buffer, make_frame(1, 0));
    insert(buffer, make_frame(2, 5));
    require(buffer.insert(make_frame(3), ClockTimePoint{}) == InsertResult::overflow,
            "buffer overflow was not rejected");
    require(buffer.pop(ClockTimePoint{}).status == PullStatus::frame, "first frame missing");
    require(buffer.pop(ClockTimePoint{}).status == PullStatus::frame, "second frame missing");
    require(buffer.insert(make_frame(0), ClockTimePoint{}) == InsertResult::late,
            "late frame was accepted");

    insert(buffer, make_frame(4, 15));
    const auto waiting = buffer.pop(ClockTimePoint{});
    require(waiting.status == PullStatus::not_ready, "missing frame did not wait for deadline");
    const auto missing = buffer.pop(ClockTimePoint{std::chrono::milliseconds{11}});
    require(missing.status == PullStatus::missing && missing.concealed,
            "missing frame was not concealed");
    require(missing.frame->payload() == AudioFrame::Payload(960, 0),
            "silence concealment was not silent");
    require(buffer.pop(ClockTimePoint{std::chrono::milliseconds{11}}).sequence_number == 4,
            "frame after missing packet was not emitted");

    buffer.reset();
    require(buffer.state() == BufferState::buffering && buffer.size() == 0,
            "jitter buffer reset failed");
}

void test_repeat_concealment_and_wraparound() {
    JitterBuffer repeat{4, 1, std::chrono::milliseconds{1}, ConcealmentPolicy::repeat_previous};
    insert(repeat, make_frame(10));
    require(repeat.pop(ClockTimePoint{}).status == PullStatus::frame, "repeat setup failed");
    insert(repeat, make_frame(12));
    require(repeat.pop(ClockTimePoint{}).status == PullStatus::not_ready,
            "repeat missing frame did not wait");
    const auto concealed = repeat.pop(ClockTimePoint{std::chrono::milliseconds{2}});
    require(concealed.concealed && concealed.frame->payload() == AudioFrame::Payload(960, 10),
            "repeat concealment failed");

    JitterBuffer wrap{8, 4, std::chrono::milliseconds{1}};
    insert(wrap, make_frame(UINT64_MAX - 1));
    insert(wrap, make_frame(0));
    insert(wrap, make_frame(UINT64_MAX));
    insert(wrap, make_frame(1));
    for (const auto expected : {UINT64_MAX - 1, UINT64_MAX, std::uint64_t{0}, std::uint64_t{1}}) {
        require(wrap.pop(ClockTimePoint{}).sequence_number == expected,
                "sequence wraparound order failed");
    }
}

void test_clock_estimator_and_scheduler() {
    ClockEstimator estimator;
    require(!estimator.map_remote_to_local(std::chrono::nanoseconds{0}).has_value(),
            "unready clock estimator mapped a timestamp");
    require(estimator.add_sample(std::chrono::nanoseconds{0},
                                 ClockTimePoint{std::chrono::milliseconds{100}}),
            "first clock sample failed");
    require(estimator.add_sample(std::chrono::milliseconds{10},
                                 ClockTimePoint{std::chrono::nanoseconds{110001000}}),
            "second clock sample failed");
    require(estimator.add_sample(std::chrono::milliseconds{20},
                                 ClockTimePoint{std::chrono::nanoseconds{120002000}}),
            "third clock sample failed");
    require_near(estimator.estimate().offset_nanoseconds, 100'000'000.0, 1.0,
                 "clock offset estimate failed");
    require_near(estimator.estimate().drift_ppm, 100.0, 0.1, "clock drift estimate failed");

    PlaybackScheduler scheduler{std::chrono::milliseconds{20}, estimator};
    const auto scheduled = scheduler.schedule(7, std::chrono::milliseconds{20});
    require(scheduled.has_value(), "scheduler did not schedule ready clock estimate");
        require(scheduler.decide(*scheduled, scheduled->local_play_time - 1ms) ==
                ScheduleDecision::too_early,
            "scheduler early decision failed");
        require(scheduler.decide(*scheduled, scheduled->local_play_time) ==
                ScheduleDecision::ready,
            "scheduler ready decision failed");
        require(scheduler.decide(*scheduled, scheduled->local_play_time + 1ms) ==
                ScheduleDecision::late,
            "scheduler late decision failed");
    require(!estimator.add_sample(std::chrono::milliseconds{15},
                                  ClockTimePoint{std::chrono::milliseconds{115}}),
            "non-monotonic timing sample was accepted");
    estimator.reset();
    require(!estimator.estimate().ready, "clock estimator reset failed");
}

void test_impairment_and_simulated_playback() {
    std::vector<AudioFrame> frames;
    for (std::uint64_t sequence = 100; sequence <= 105; ++sequence) {
        frames.push_back(make_frame(sequence));
    }
    ImpairmentPlan plan;
    plan.arrival_order = {100, 101, 103, 102, 105};
    plan.lost_sequences = {104};
    plan.duplicate_sequences = {101};
    plan.arrival_delays = {0ms, 12ms, 8ms, 17ms, 20ms};
    const auto impaired = apply_impairment(frames, plan);
    require(impaired.size() == 6, "impairment scenario size was incorrect");

    JitterBuffer buffer{8, 4, std::chrono::milliseconds{1}};
    for (const auto& event : impaired) {
        const auto result = buffer.insert(event.frame, event.arrival_time);
        require(result == InsertResult::inserted || result == InsertResult::duplicate,
                "impaired frame insertion failed");
    }
    SimulatedPlayback playback;
    for (std::uint64_t expected = 100; expected <= 103; ++expected) {
        const auto item = buffer.pop(ClockTimePoint{});
        require(item.status == PullStatus::frame && item.sequence_number == expected,
                "impaired playback order failed");
        playback.consume(item);
    }
    require(buffer.pop(ClockTimePoint{}).status == PullStatus::not_ready,
            "missing packet did not enter deadline wait");
    const auto concealed = buffer.pop(ClockTimePoint{std::chrono::milliseconds{2}});
    require(concealed.status == PullStatus::missing && concealed.sequence_number == 104,
            "impaired missing packet was not concealed");
    playback.consume(concealed);
    const auto final = buffer.pop(ClockTimePoint{std::chrono::milliseconds{2}});
    require(final.status == PullStatus::frame && final.sequence_number == 105,
            "impaired final packet was not played");
    playback.consume(final);

    const auto& stats = playback.statistics();
    require(stats.total_frames_processed == 6 && stats.concealed_frames_played == 1,
            "simulated playback statistics were incorrect");
    require(stats.playback_sequence == std::vector<std::uint64_t>({100, 101, 102, 103, 104, 105}),
            "simulated playback sequence was incorrect");
    require(buffer.statistics().duplicates_rejected == 1 &&
                buffer.statistics().missing_frames_declared == 1,
            "jitter buffer impairment statistics were incorrect");
}

}  // namespace

int main() {
    try {
        test_frame_duration_and_jitter();
        test_jitter_buffer_order_and_states();
        test_jitter_buffer_missing_late_overflow_reset();
        test_repeat_concealment_and_wraparound();
        test_clock_estimator_and_scheduler();
        test_impairment_and_simulated_playback();
    } catch (const std::exception& error) {
        std::cerr << "Timing test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All timing tests passed.\n";
    return 0;
}
