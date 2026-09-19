#include "distributed_audio/audio_buffer.hpp"
#include "distributed_audio/audio_frame.hpp"
#include "distributed_audio/clock_estimator.hpp"
#include "distributed_audio/dsp_pipeline.hpp"
#include "distributed_audio/jitter_buffer.hpp"
#include "distributed_audio/network_impairment.hpp"
#include "distributed_audio/playback_scheduler.hpp"
#include "distributed_audio/pcm_conversion.hpp"
#include "distributed_audio/simulated_playback.hpp"
#include "distributed_audio/timing_utils.hpp"
#include "distributed_audio/udp_receiver.hpp"
#include "distributed_audio/udp_sender.hpp"

#include <cmath>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846F;
using namespace std::chrono_literals;

}  // namespace

int main() {
    std::cout << "Distributed Audio System\n"
              << "Version 0.1.0\n"
              << "System initialized successfully.\n";

    const distributed_audio::AudioFormat format{48'000, 2, 16};
    constexpr std::size_t sample_frames = 480;
    constexpr float frequency_hz = 440.0F;
    distributed_audio::NormalizedSamples samples;
    samples.reserve(sample_frames * format.channel_count);
    for (std::size_t frame_index = 0; frame_index < sample_frames; ++frame_index) {
        const auto sample = 0.5F * std::sin(2.0F * kPi * frequency_hz *
                                             static_cast<float>(frame_index) /
                                             static_cast<float>(format.sample_rate));
        samples.push_back(sample);
        samples.push_back(sample);
    }

    const distributed_audio::AudioFrame input_frame{
        format,
        1,
        distributed_audio::AudioFrame::Timestamp{0},
        distributed_audio::float_to_pcm16(samples, format)};
    distributed_audio::AudioBuffer buffer{2};
    if (!buffer.push(std::move(input_frame))) {
        std::cerr << "Failed to populate audio buffer.\n";
        return 1;
    }
    const auto buffered_frame = buffer.pop();
    if (!buffered_frame.has_value()) {
        std::cerr << "Failed to retrieve audio frame.\n";
        return 1;
    }

    std::cout << "Audio format: " << format.sample_rate << " Hz, "
              << format.channel_count << " channels, " << format.bits_per_sample
              << " bits\n";

    distributed_audio::GainProcessor gain;
    gain.set_decibels(-6.0F);
    const distributed_audio::DspPipeline pipeline{gain, 1};
    const auto result = pipeline.process(*buffered_frame);
    std::cout << "Processed frame sequence: " << result.audio.sequence_number << '\n'
              << "Samples: " << result.audio.samples.size() << " mono samples\n"
              << "Gain: -6 dB\n"
              << std::fixed << std::setprecision(4)
              << "Overall peak: " << result.peaks.overall << '\n';

    constexpr std::size_t network_frame_count = 10;
    constexpr std::size_t network_samples_per_channel = 240;
    distributed_audio::network::UdpReceiver receiver{"127.0.0.1", 0, 1000};
    distributed_audio::network::UdpSender sender{"127.0.0.1", receiver.port()};
    for (std::size_t frame_index = 0; frame_index < network_frame_count; ++frame_index) {
        distributed_audio::NormalizedSamples network_samples;
        network_samples.reserve(network_samples_per_channel * format.channel_count);
        for (std::size_t sample_index = 0; sample_index < network_samples_per_channel;
             ++sample_index) {
            const auto absolute_sample = frame_index * network_samples_per_channel + sample_index;
            const auto sample = 0.5F * std::sin(
                2.0F * kPi * frequency_hz * static_cast<float>(absolute_sample) /
                static_cast<float>(format.sample_rate));
            network_samples.push_back(sample);
            network_samples.push_back(sample);
        }
        const distributed_audio::AudioFrame network_frame{
            format,
            static_cast<std::uint64_t>(frame_index),
            distributed_audio::AudioFrame::Timestamp{
                static_cast<std::int64_t>(frame_index) * 5'000'000},
            distributed_audio::float_to_pcm16(network_samples, format)};
        const auto bytes_sent = sender.send(network_frame);
        if (bytes_sent == 0) {
            throw std::runtime_error("localhost UDP demo sent no bytes");
        }
    }

    std::size_t received = 0;
    while (received < network_frame_count) {
        const auto frame = receiver.receive();
        if (!frame.has_value()) {
            throw std::runtime_error("localhost UDP demo timed out");
        }
        ++received;
    }
    const auto& statistics = receiver.statistics();
    std::cout << "\nNetwork transport demo\n"
              << "Destination: 127.0.0.1:" << receiver.port() << '\n'
              << "Packet duration: 5 ms\n"
              << "Frames sent: " << network_frame_count << '\n'
              << "Frames received: " << statistics.packets_received << '\n'
              << "Frames accepted: " << statistics.packets_accepted << '\n'
              << "Malformed: " << statistics.malformed_packets << '\n'
              << "Duplicates: " << statistics.duplicate_packets << '\n'
              << "Out-of-order: " << statistics.out_of_order_packets << '\n'
              << "Estimated missing: " << statistics.estimated_missing_packets << '\n';

    std::vector<distributed_audio::AudioFrame> timing_frames;
    constexpr std::size_t timing_frame_count = 12;
    constexpr std::size_t timing_samples_per_channel = 240;
    for (std::uint64_t sequence = 1; sequence <= timing_frame_count; ++sequence) {
        distributed_audio::NormalizedSamples timing_samples(
            timing_samples_per_channel * format.channel_count, 0.0F);
        timing_frames.emplace_back(
            format,
            sequence,
            distributed_audio::AudioFrame::Timestamp{
                static_cast<std::int64_t>(sequence - 1) * 5'000'000},
            distributed_audio::float_to_pcm16(timing_samples, format));
    }

    distributed_audio::timing::ImpairmentPlan plan;
    plan.arrival_order = {1, 3, 2, 4, 5, 6, 8, 7, 9, 10, 12};
    plan.lost_sequences = {11};
    plan.duplicate_sequences = {3};
    plan.arrival_delays = {0ms, 12ms, 8ms, 17ms, 20ms, 25ms, 31ms, 28ms, 35ms, 40ms, 50ms};
    const auto impaired = distributed_audio::timing::apply_impairment(timing_frames, plan);
    distributed_audio::timing::JitterBuffer jitter_buffer{
        16, 4, 1ms, distributed_audio::timing::ConcealmentPolicy::silence};
    distributed_audio::timing::JitterEstimator jitter_estimator;
    for (const auto& event : impaired) {
        jitter_estimator.update(event.frame.timestamp(), event.arrival_time);
        const auto insertion = jitter_buffer.insert(event.frame, event.arrival_time);
        (void)insertion;
    }
    distributed_audio::timing::SimulatedPlayback playback;
    for (std::size_t index = 0; index < 10; ++index) {
        playback.consume(jitter_buffer.pop(distributed_audio::timing::ClockTimePoint{}));
    }
    const auto waiting = jitter_buffer.pop(distributed_audio::timing::ClockTimePoint{});
    (void)waiting;
    playback.consume(jitter_buffer.pop(distributed_audio::timing::ClockTimePoint{2ms}));
    playback.consume(jitter_buffer.pop(distributed_audio::timing::ClockTimePoint{2ms}));

    distributed_audio::timing::ClockEstimator clock_estimator;
    const auto first_sample = clock_estimator.add_sample(
        0ns, distributed_audio::timing::ClockTimePoint{20ms});
    const auto second_sample = clock_estimator.add_sample(
        5ms, distributed_audio::timing::ClockTimePoint{25ms});
    const auto third_sample = clock_estimator.add_sample(
        10ms, distributed_audio::timing::ClockTimePoint{30ms});
    (void)first_sample;
    (void)second_sample;
    (void)third_sample;
    distributed_audio::timing::PlaybackScheduler scheduler{20ms, clock_estimator};
    const auto scheduled = scheduler.schedule(1, 0ns);
    const auto timing_statistics = jitter_buffer.statistics();
    const auto playback_statistics = playback.statistics();
    std::cout << "\nReceiver timing demo\n"
              << "Frame duration: 5.0 ms\n"
              << "Target jitter depth: 4 frames\n"
              << "Playout delay: 20.0 ms\n"
              << "Packets generated: " << timing_frame_count << '\n'
              << "Packets reordered: " << timing_statistics.packets_reordered << '\n'
              << "Duplicates rejected: " << timing_statistics.duplicates_rejected << '\n'
              << "Late packets: " << timing_statistics.late_packets_rejected << '\n'
              << "Missing frames: " << timing_statistics.missing_frames_declared << '\n'
              << "Concealed frames: " << timing_statistics.concealed_frames_generated << '\n'
              << "Playback sequence: ";
    for (const auto sequence : playback_statistics.playback_sequence) {
        std::cout << sequence << ' ';
    }
    std::cout << "\nEstimated jitter: " << jitter_estimator.estimate().nanoseconds / 1'000'000.0
              << " ms\n"
              << "Clock offset: " << clock_estimator.estimate().offset_nanoseconds / 1'000'000.0
              << " ms\n"
              << "Clock drift: " << clock_estimator.estimate().drift_ppm << " ppm\n"
              << "First frame scheduled: " << (scheduled.has_value() ? "yes" : "not ready")
              << '\n';

    return 0;
}
