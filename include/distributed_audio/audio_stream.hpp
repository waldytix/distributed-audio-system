#pragma once

#include "distributed_audio/audio_session.hpp"
#include "distributed_audio/clock_estimator.hpp"
#include "distributed_audio/gain_processor.hpp"
#include "distributed_audio/jitter_buffer.hpp"
#include "distributed_audio/packet_serializer.hpp"
#include "distributed_audio/playback_scheduler.hpp"
#include "distributed_audio/sequence_tracker.hpp"
#include "distributed_audio/simulated_playback.hpp"
#include "distributed_audio/udp_socket.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <netinet/in.h>
#include <string>
#include <vector>

namespace distributed_audio::stream {

enum class StreamState : std::uint8_t {
    created,
    ready,
    running,
    draining,
    stopped,
    failed
};

struct StreamStatistics {
    std::uint64_t frames_generated{0};
    std::uint64_t frames_transmitted{0};
    std::uint64_t bytes_transmitted{0};
    std::uint64_t datagrams_received{0};
    std::uint64_t frames_accepted{0};
    std::uint64_t duplicate_frames{0};
    std::uint64_t out_of_order_frames{0};
    std::uint64_t estimated_missing_frames{0};
    std::uint64_t frames_inserted{0};
    std::uint64_t frames_played{0};
    std::uint64_t samples_played{0};
    std::uint64_t concealed_frames{0};
    std::uint64_t malformed_packets{0};
    std::uint64_t incompatible_packets{0};
    std::size_t jitter_buffer_depth{0};
    float peak_level{0.0F};
};

struct StreamFrameResult {
    AudioFrame frame;
    float peak_level{0.0F};
};

class SyntheticAudioSource {
public:
    SyntheticAudioSource(session::AudioSessionConfig config,
                         float frequency_hz,
                         float amplitude,
                         float linear_gain = 1.0F);

    [[nodiscard]] StreamFrameResult next();
    [[nodiscard]] const session::AudioSessionConfig& configuration() const noexcept;
    [[nodiscard]] std::uint64_t generated() const noexcept;

private:
    session::AudioSessionConfig config_;
    float frequency_hz_;
    float amplitude_;
    GainProcessor gain_;
    std::uint64_t sequence_{0};
};

[[nodiscard]] std::vector<std::uint8_t> serialize_media_frame(
    const session::SessionId& session_id, const AudioFrame& frame);
[[nodiscard]] std::optional<AudioFrame> deserialize_media_frame(
    const session::SessionId& expected_session,
    const std::vector<std::uint8_t>& packet);

class AudioStreamSender {
public:
    AudioStreamSender(session::SessionId session_id,
                      session::AudioSessionConfig config,
                      const std::string& address);

    [[nodiscard]] bool prepare() noexcept;
    [[nodiscard]] bool start() noexcept;
    [[nodiscard]] bool send(const AudioFrame& frame);
    [[nodiscard]] bool stop() noexcept;
    [[nodiscard]] StreamState state() const noexcept;
    [[nodiscard]] const StreamStatistics& statistics() const noexcept;

private:
    session::SessionId session_id_;
    session::AudioSessionConfig config_;
    network::UdpSocket socket_;
    sockaddr_in destination_{};
    StreamState state_{StreamState::created};
    StreamStatistics statistics_;
};

class AudioStreamReceiver {
public:
    AudioStreamReceiver(session::SessionId session_id,
                        session::AudioSessionConfig config,
                        std::uint16_t listen_port,
                        std::chrono::nanoseconds missing_wait = std::chrono::milliseconds{2});

    [[nodiscard]] bool prepare() noexcept;
    [[nodiscard]] bool start() noexcept;
    [[nodiscard]] std::size_t poll(timing::ClockTimePoint arrival_time);
    [[nodiscard]] std::size_t playback_step(timing::ClockTimePoint now);
    [[nodiscard]] bool stop() noexcept;
    [[nodiscard]] StreamState state() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] const StreamStatistics& statistics() const noexcept;
    [[nodiscard]] const timing::SimulatedPlaybackStatistics& playback_statistics() const noexcept;

private:
    session::SessionId session_id_;
    session::AudioSessionConfig config_;
    network::UdpSocket socket_;
    std::uint16_t port_{0};
    timing::JitterBuffer jitter_buffer_;
    network::SequenceTracker sequence_tracker_;
    timing::ClockEstimator clock_estimator_;
    timing::PlaybackScheduler scheduler_;
    timing::SimulatedPlayback playback_;
    StreamState state_{StreamState::created};
    StreamStatistics statistics_;
};

}  // namespace distributed_audio::stream
