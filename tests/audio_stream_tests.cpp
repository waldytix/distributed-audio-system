#include "distributed_audio/audio_stream.hpp"

#include "distributed_audio/pcm_conversion.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using distributed_audio::AudioFormat;
using distributed_audio::AudioFrame;
using distributed_audio::discovery::DeviceId;
using distributed_audio::session::AudioSessionConfig;
using distributed_audio::session::SessionId;
using distributed_audio::stream::AudioStreamReceiver;
using distributed_audio::stream::AudioStreamSender;
using distributed_audio::stream::StreamState;
using distributed_audio::stream::SyntheticAudioSource;
using distributed_audio::timing::ClockTimePoint;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

AudioSessionConfig config() {
    return AudioSessionConfig{AudioFormat{48'000, 2, 16}, 240,
                              DeviceId::from_u64(1), DeviceId::from_u64(2), 55122, 55123};
}

void test_source_and_stream_states() {
    const auto session_id = SessionId::from_u64(1);
    const auto session_config = config();
    SyntheticAudioSource source{session_config, 440.0F, 0.5F, 0.5F};
    const auto first = source.next();
    const auto second = source.next();
    require(first.frame.format() == session_config.format, "source format mismatch");
    require(first.frame.payload().size() == 960, "source payload size mismatch");
    require(second.frame.sequence_number() == first.frame.sequence_number() + 1,
            "source sequence did not advance");
    require(second.frame.timestamp() > first.frame.timestamp(), "source timestamp did not advance");
    require(first.peak_level > 0.0F, "source did not generate audio");

    AudioStreamReceiver receiver{session_id, session_config, 55123, 1ms};
    AudioStreamSender sender{session_id, session_config, "127.0.0.1"};
    require(!sender.start(), "sender started before preparation");
    require(sender.prepare() && sender.start(), "sender did not enter running state");
    require(receiver.prepare() && receiver.start(), "receiver did not enter running state");
    require(!receiver.stop() || receiver.state() == StreamState::stopped,
            "receiver stop behavior was invalid");
    require(receiver.state() == StreamState::stopped, "receiver did not stop");
    require(receiver.stop(), "receiver stop was not idempotent");
    require(sender.stop(), "sender did not stop");
}

void test_real_udp_integrity_and_gain() {
    const auto session_id = SessionId::from_u64(2);
    const auto session_config = config();
    AudioStreamReceiver receiver{session_id, session_config, 55123, 1ms};
    AudioStreamSender sender{session_id, session_config, "127.0.0.1"};
    require(receiver.prepare() && receiver.start() && sender.prepare() && sender.start(),
            "stream setup failed");
    SyntheticAudioSource source{session_config, 440.0F, 0.5F, 1.0F};
    std::vector<distributed_audio::stream::StreamFrameResult> generated;
    std::uint64_t expected_checksum = 0;
    for (int index = 0; index < 3; ++index) {
        generated.push_back(source.next());
        for (const auto byte : generated.back().frame.payload()) {
            expected_checksum = expected_checksum * 1099511628211ULL + byte;
        }
        require(sender.send(generated.back().frame), "media UDP send failed");
    }
    require(receiver.poll(ClockTimePoint{}) == 3, "media UDP receive count failed");
    for (int index = 0; index < 3; ++index) {
        const auto played = receiver.playback_step(
            ClockTimePoint{static_cast<std::int64_t>(index) * 5'000'000ns});
        (void)played;
    }
    const auto& sender_stats = sender.statistics();
    const auto& receiver_stats = receiver.statistics();
    require(sender_stats.frames_transmitted == 3 && sender_stats.bytes_transmitted > 0,
            "sender statistics were incorrect");
    require(receiver_stats.datagrams_received == 3 && receiver_stats.frames_accepted == 3,
            "receiver statistics were incorrect");
    require(receiver_stats.frames_played >= 2 && receiver.playback_statistics().samples_played > 0,
            "simulated playback did not consume PCM");
    require(receiver.playback_statistics().peak_level > 0.0F, "playback peak was empty");
        require(receiver.playback_statistics().payload_checksum == expected_checksum,
            "PCM payload checksum did not survive the media path");

    SyntheticAudioSource half_gain{session_config, 440.0F, 0.5F, 0.5F};
    const auto unity = distributed_audio::pcm16_to_float(generated.front().frame);
    const auto half = distributed_audio::pcm16_to_float(half_gain.next().frame);
    require(unity.size() == half.size(), "gain comparison size mismatch");
    bool changed = false;
    for (std::size_t index = 0; index < unity.size(); ++index) {
        if (std::fabs(unity[index] - half[index]) > 0.01F) changed = true;
    }
    require(changed, "DSP gain did not modify transmitted source data");
    require(sender.stop() && receiver.stop(), "stream shutdown failed");
}

void test_session_isolation_and_incompatible_media() {
    const auto session_id = SessionId::from_u64(3);
    const auto session_config = config();
    AudioStreamReceiver receiver{session_id, session_config, 55123, 1ms};
    AudioStreamSender wrong_sender{SessionId::from_u64(4), session_config, "127.0.0.1"};
    require(receiver.prepare() && receiver.start() && wrong_sender.prepare() && wrong_sender.start(),
            "isolation setup failed");
    SyntheticAudioSource source{session_config, 220.0F, 0.25F};
    require(wrong_sender.send(source.next().frame), "wrong-session send failed");
    require(receiver.poll(ClockTimePoint{}) == 0, "wrong session packet was accepted");
    require(receiver.statistics().incompatible_packets == 1,
            "wrong session packet was not counted");
    require(wrong_sender.stop() && receiver.stop(), "isolation shutdown failed");
}

}  // namespace

int main() {
    try {
        test_source_and_stream_states();
        test_real_udp_integrity_and_gain();
        test_session_isolation_and_incompatible_media();
    } catch (const std::exception& error) {
        std::cerr << "Stream test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All end-to-end stream tests passed.\n";
    return 0;
}
