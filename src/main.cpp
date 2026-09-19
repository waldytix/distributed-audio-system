#include "distributed_audio/audio_buffer.hpp"
#include "distributed_audio/audio_frame.hpp"
#include "distributed_audio/clock_estimator.hpp"
#include "distributed_audio/dsp_pipeline.hpp"
#include "distributed_audio/device_discovery.hpp"
#include "distributed_audio/audio_session.hpp"
#include "distributed_audio/audio_stream.hpp"
#include "distributed_audio/jitter_buffer.hpp"
#include "distributed_audio/network_impairment.hpp"
#include "distributed_audio/multi_device_sync.hpp"
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
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846F;
using distributed_audio::timing::ClockTimePoint;
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

    distributed_audio::timing::EndpointConfig reference_config{"reference", 0ms, 0.0, {}};
    distributed_audio::timing::EndpointConfig fast_config{"fast-clock", 8ms, 120.0, {}};
    distributed_audio::timing::EndpointConfig slow_config{"slow-clock", -7ms, -100.0, {}};
    std::vector<distributed_audio::AudioFrame> synchronization_frames;
    constexpr std::size_t synchronization_frame_count = 300;
    synchronization_frames.reserve(synchronization_frame_count);
    for (std::uint64_t sequence = 1; sequence <= synchronization_frame_count; ++sequence) {
        distributed_audio::NormalizedSamples sync_samples(
            timing_samples_per_channel * format.channel_count, 0.0F);
        synchronization_frames.emplace_back(
            format, sequence,
            distributed_audio::AudioFrame::Timestamp{
                static_cast<std::int64_t>(sequence - 1) * 5'000'000},
            distributed_audio::float_to_pcm16(sync_samples, format));
    }
    std::vector<std::chrono::milliseconds> synchronization_delays;
    synchronization_delays.reserve(synchronization_frame_count);
    for (std::size_t index = 0; index < synchronization_frame_count; ++index) {
        synchronization_delays.push_back(static_cast<int>(index * 5) * 1ms);
    }
    reference_config.impairment.arrival_delays = synchronization_delays;
    fast_config.impairment.arrival_delays = synchronization_delays;
    slow_config.impairment.arrival_delays = synchronization_delays;
    slow_config.impairment.arrival_order.resize(synchronization_frame_count);
    std::iota(slow_config.impairment.arrival_order.begin(),
              slow_config.impairment.arrival_order.end(), std::uint64_t{1});
    slow_config.impairment.duplicate_sequences = {3};
    slow_config.impairment.lost_sequences = {20, 55};
    slow_config.impairment.arrival_delays = {0ms, 3ms, 1ms};
    distributed_audio::timing::PlaybackEndpoint reference_endpoint{
        reference_config, 16, 2, 1ms};
    distributed_audio::timing::PlaybackEndpoint fast_endpoint{fast_config, 16, 2, 1ms};
    distributed_audio::timing::PlaybackEndpoint slow_endpoint{slow_config, 16, 2, 1ms};
    reference_endpoint.load(synchronization_frames);
    fast_endpoint.load(synchronization_frames);
    slow_endpoint.load(synchronization_frames);
    std::vector<distributed_audio::timing::PlaybackEndpoint*> endpoints{
        &reference_endpoint, &fast_endpoint, &slow_endpoint};
    distributed_audio::timing::SynchronizationParameters sync_parameters;
    sync_parameters.tolerance = 500us;
    sync_parameters.maximum_correction = 1ms;
    sync_parameters.convergence_rate = 0.2;
    sync_parameters.update_interval = 20ms;
    distributed_audio::timing::SynchronizationController sync_controller{sync_parameters};
    const auto initial_skew = sync_controller.maximum_skew(endpoints, ClockTimePoint{});
    constexpr auto simulation_step = 5ms;
    constexpr int simulation_steps = 320;
    for (int step = 0; step < simulation_steps; ++step) {
        const auto now = ClockTimePoint{step * simulation_step};
        for (auto* endpoint : endpoints) {
            endpoint->advance(now);
        }
        if (step % 4 == 0) {
            sync_controller.update(endpoints, now);
        }
    }
    const auto final_time = ClockTimePoint{1500ms};
    const auto final_skew = sync_controller.maximum_skew(endpoints, final_time);
    std::cout << "\nMulti-device synchronization demo\n"
              << "Tolerance: " << sync_parameters.tolerance.count() / 1'000'000.0 << " ms\n"
              << "Maximum correction/update: "
              << sync_parameters.maximum_correction.count() / 1'000'000.0 << " ms\n"
              << "Initial inter-device skew: " << initial_skew.count() / 1'000'000.0 << " ms\n"
              << "Final inter-device skew: " << final_skew.count() / 1'000'000.0 << " ms\n";
    for (const auto* endpoint : endpoints) {
        const auto& metrics = endpoint->metrics();
        std::cout << endpoint->name() << ": initial offset "
                  << metrics.initial_offset.count() / 1'000'000.0 << " ms, drift "
                  << metrics.configured_drift_ppm << " ppm, final error "
                  << metrics.current_error.count() / 1'000'000.0 << " ms, correction "
                  << metrics.applied_correction.count() / 1'000'000.0 << " ms, state "
                  << static_cast<int>(metrics.state) << ", concealed "
                  << metrics.concealed_frames << '\n';
    }

    auto make_device = [](std::uint64_t id, const std::string& name,
                          std::uint16_t audio_port) {
        distributed_audio::discovery::DeviceInfo info;
        info.id = distributed_audio::discovery::DeviceId::from_u64(id);
        info.name = name;
        info.sample_rates = {48'000};
        info.channel_counts = {2};
        info.bits_per_sample = {16};
        info.audio_port = audio_port;
        return info;
    };
    using distributed_audio::discovery::DiscoveryService;
    using distributed_audio::discovery::MessageType;
    DiscoveryService living_room{make_device(101, "Living Room Speaker", 5101), 0, 10ms, 200ms};
    DiscoveryService studio{make_device(102, "Studio Speaker", 5102), 0, 10ms, 200ms};
    DiscoveryService desktop{make_device(103, "Desktop Receiver", 5103), 0, 10ms, 200ms};
    const auto discovery_now = ClockTimePoint{};
    const auto sent_ab = living_room.send(MessageType::announce, "127.0.0.1", studio.port());
    const auto sent_ac = living_room.send(MessageType::announce, "127.0.0.1", desktop.port());
    const auto sent_ba = studio.send(MessageType::announce, "127.0.0.1", living_room.port());
    const auto sent_bc = studio.send(MessageType::announce, "127.0.0.1", desktop.port());
    const auto sent_ca = desktop.send(MessageType::announce, "127.0.0.1", living_room.port());
    const auto sent_cb = desktop.send(MessageType::announce, "127.0.0.1", studio.port());
    (void)sent_ab;
    (void)sent_ac;
    (void)sent_ba;
    (void)sent_bc;
    (void)sent_ca;
    (void)sent_cb;
    const auto discovery_poll_a = living_room.poll(discovery_now);
    const auto discovery_poll_b = studio.poll(discovery_now);
    const auto discovery_poll_c = desktop.poll(discovery_now);
    (void)discovery_poll_a;
    (void)discovery_poll_b;
    (void)discovery_poll_c;
    std::cout << "\nDevice discovery demo\n"
              << "Known devices at Living Room: " << living_room.registry().size() << '\n'
              << "Known devices at Studio: " << studio.registry().size() << '\n'
              << "Known devices at Desktop: " << desktop.registry().size() << '\n';
    const auto compatibility = distributed_audio::discovery::check_compatibility(
        living_room.local_info(), studio.local_info());
    std::cout << "Living Room Speaker <-> Studio Speaker: "
              << (compatibility.compatible ? "compatible" : "incompatible") << '\n';

    DiscoveryService late_device{make_device(104, "Late Joiner", 5104), 0, 10ms, 200ms};
    const auto sent_late = late_device.send(MessageType::announce, "127.0.0.1", living_room.port());
    (void)sent_late;
    const auto late_poll = living_room.poll(discovery_now + 1ms);
    (void)late_poll;
    std::cout << "Late device join: "
              << (living_room.registry().lookup(late_device.local_info().id).has_value()
                      ? "detected" : "not detected") << '\n';

    auto updated_studio = studio.local_info();
    updated_studio.name = "Studio Speaker Updated";
    studio.update_local_info(updated_studio);
    const auto sent_update = studio.send(MessageType::announce, "127.0.0.1", living_room.port());
    (void)sent_update;
    const auto update_poll = living_room.poll(discovery_now + 2ms);
    (void)update_poll;
    const auto updated_entry = living_room.registry().lookup(studio.local_info().id);
    std::cout << "Device update: "
              << (updated_entry.has_value() && updated_entry->info.name == updated_studio.name
                      ? "detected without duplicate registry entry" : "not detected") << '\n';

    const auto sent_goodbye = desktop.send(MessageType::goodbye, "127.0.0.1", living_room.port());
    (void)sent_goodbye;
    const auto goodbye_poll = living_room.poll(discovery_now + 3ms);
    (void)goodbye_poll;
    std::cout << "Graceful departure: "
              << (!living_room.registry().lookup(desktop.local_info().id).has_value()
                      ? "detected" : "not detected") << '\n';
    const auto stale_count = living_room.registry().expire(discovery_now + 201ms);
    std::cout << "Stale device expiration: " << (stale_count > 0 ? "detected" : "not detected")
              << '\n';

    living_room.shutdown();
    studio.shutdown();
    desktop.shutdown();
    late_device.shutdown();

    distributed_audio::session::AudioSessionManager session_initiator{
        living_room.local_info(), 500ms};
    distributed_audio::session::AudioSessionManager session_receiver{
        studio.local_info(), 500ms};
    distributed_audio::session::SessionControlService control_initiator{
        session_initiator, 0, 10ms};
    distributed_audio::session::SessionControlService control_receiver{
        session_receiver, 0, 10ms};
    const auto session_offer = session_initiator.create_offer(
        studio.local_info(), discovery_now);
    if (!session_offer.has_value()) {
        std::cerr << "Session offer creation failed.\n";
        return 1;
    }
    const auto offer_sent = control_initiator.send(
        *session_offer, "127.0.0.1", control_receiver.port());
    const auto receiver_poll = control_receiver.poll(discovery_now);
    const auto initiator_poll = control_initiator.poll(discovery_now);
    (void)offer_sent;
    (void)receiver_poll;
    (void)initiator_poll;
    const auto established = session_initiator.lookup(session_offer->session_id);
    std::cout << "\nAudio session negotiation demo\n"
              << "Session offer: " << (session_offer.has_value() ? "sent" : "not created") << '\n'
              << "Negotiated format: " << session_offer->configuration->format.sample_rate
              << " Hz / " << session_offer->configuration->format.channel_count
              << " channels / " << session_offer->configuration->format.bits_per_sample
              << " bit\n"
              << "Session ID: " << session_offer->session_id.to_string() << '\n'
              << "State: " << (established.has_value() &&
                                  established->state == distributed_audio::session::AudioSessionState::established
                                      ? "Established" : "Failed") << '\n';

    const auto start_message = session_initiator.start(session_offer->session_id, discovery_now + 1ms);
    if (!start_message.has_value()) {
        std::cerr << "Session start creation failed.\n";
        return 1;
    }
    const auto start_sent = control_initiator.send(
        *start_message, "127.0.0.1", control_receiver.port());
    const auto start_receiver_poll = control_receiver.poll(discovery_now + 1ms);
    const auto start_initiator_poll = control_initiator.poll(discovery_now + 1ms);
    (void)start_sent;
    (void)start_receiver_poll;
    (void)start_initiator_poll;
    const auto streaming = session_initiator.lookup(session_offer->session_id);
    std::cout << "Starting session: "
              << (streaming.has_value() && streaming->state == distributed_audio::session::AudioSessionState::streaming
                      ? "Streaming" : "not streaming") << '\n'
              << "Audio transport associated with negotiated session.\n";

    distributed_audio::stream::AudioStreamReceiver media_receiver{
        session_offer->session_id, *session_offer->configuration,
        session_offer->configuration->receiver_audio_port, 1ms};
    distributed_audio::stream::AudioStreamSender media_sender{
        session_offer->session_id, *session_offer->configuration, "127.0.0.1"};
    if (!media_receiver.prepare() || !media_receiver.start() ||
        !media_sender.prepare() || !media_sender.start()) {
        std::cerr << "Media stream startup failed.\n";
        return 1;
    }
    distributed_audio::stream::SyntheticAudioSource media_source{
        *session_offer->configuration, 440.0F, 0.5F, 0.5F};
    constexpr int media_frame_count = 20;
    for (int index = 0; index < media_frame_count; ++index) {
        const auto generated = media_source.next();
        const auto sent = media_sender.send(generated.frame);
        if (!sent) {
            std::cerr << "Media UDP transmission failed.\n";
            return 1;
        }
        const auto media_now = ClockTimePoint{std::chrono::nanoseconds{
            static_cast<std::int64_t>(index) * 5'000'000}};
        const auto received_now = media_receiver.poll(media_now);
        const auto played_now = media_receiver.playback_step(media_now);
        (void)received_now;
        (void)played_now;
    }
    const auto received_media = media_receiver.statistics().datagrams_received;
    const auto& media_sender_stats = media_sender.statistics();
    const auto& media_receiver_stats = media_receiver.statistics();
    std::cout << "Streaming over UDP 127.0.0.1:"
              << session_offer->configuration->receiver_audio_port << '\n'
              << "Frames generated: " << media_source.generated() << '\n'
              << "Frames transmitted: " << media_sender_stats.frames_transmitted << '\n'
              << "Frames received: " << received_media << '\n'
              << "Frames accepted: " << media_receiver_stats.frames_accepted << '\n'
              << "Frames played: " << media_receiver_stats.frames_played << '\n'
              << "Samples played: " << media_receiver_stats.samples_played << '\n'
              << "Concealed frames: " << media_receiver_stats.concealed_frames << '\n'
              << "Peak level: " << media_receiver_stats.peak_level << '\n';
    const auto media_stopped = media_sender.stop() && media_receiver.stop();
    std::cout << "Stream state: " << (media_stopped ? "Stopped" : "Failed") << '\n';

    const auto stop_message = session_initiator.stop(session_offer->session_id, discovery_now + 2ms);
    if (!stop_message.has_value()) {
        std::cerr << "Session stop creation failed.\n";
        return 1;
    }
    const auto stop_sent = control_initiator.send(
        *stop_message, "127.0.0.1", control_receiver.port());
    const auto stop_receiver_poll = control_receiver.poll(discovery_now + 2ms);
    const auto stop_initiator_poll = control_initiator.poll(discovery_now + 2ms);
    (void)stop_sent;
    (void)stop_receiver_poll;
    (void)stop_initiator_poll;
    const auto closed = session_initiator.lookup(session_offer->session_id);
    std::cout << "Stopping session: "
              << (closed.has_value() && closed->state == distributed_audio::session::AudioSessionState::closed
                      ? "Closed" : "not closed") << '\n';

    auto incompatible_config = *session_offer->configuration;
    incompatible_config.format.sample_rate = 96'000;
    const auto rejection = session_receiver.handle(
        {distributed_audio::session::SessionMessageType::offer,
         distributed_audio::session::SessionId::from_u64(900),
         living_room.local_info().id, studio.local_info().id,
         incompatible_config, distributed_audio::session::SessionRejectReason::none},
        discovery_now + 3ms);
    std::cout << "Rejected-session demonstration: "
              << (rejection.has_value() && rejection->reason ==
                          distributed_audio::session::SessionRejectReason::incompatible_format
                      ? "incompatible audio configuration" : "not rejected") << '\n';
    control_initiator.shutdown();
    control_receiver.shutdown();

    return 0;
}
