#include "distributed_audio/audio_stream.hpp"

#include "distributed_audio/audio_meter.hpp"
#include "distributed_audio/network_protocol.hpp"
#include "distributed_audio/pcm_conversion.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace distributed_audio::stream {
namespace {

constexpr std::size_t kSessionEnvelopeSize = 16;
constexpr float kPi = 3.14159265358979323846F;

void append_session_id(std::vector<std::uint8_t>& packet,
                       const session::SessionId& id) {
    packet.insert(packet.end(), id.bytes().begin(), id.bytes().end());
}

bool frame_matches(const AudioFrame& frame, const session::AudioSessionConfig& config) {
    if (frame.format() != config.format) return false;
    const auto bytes_per_sample = static_cast<std::size_t>(config.format.bits_per_sample / 8);
    const auto bytes_per_frame = static_cast<std::size_t>(config.format.channel_count) * bytes_per_sample;
    return bytes_per_frame != 0 && frame.payload().size() ==
        static_cast<std::size_t>(config.samples_per_frame) * bytes_per_frame;
}

}  // namespace

SyntheticAudioSource::SyntheticAudioSource(session::AudioSessionConfig config,
                                           float frequency_hz,
                                           float amplitude,
                                           float linear_gain)
    : config_(std::move(config)),
      frequency_hz_(frequency_hz),
      amplitude_(amplitude),
      gain_(linear_gain) {
    if (!config_.is_valid() || !std::isfinite(frequency_hz_) || frequency_hz_ < 0.0F ||
        !std::isfinite(amplitude_) || amplitude_ < 0.0F || amplitude_ > 1.0F) {
        throw std::invalid_argument("invalid synthetic audio source configuration");
    }
}

StreamFrameResult SyntheticAudioSource::next() {
    NormalizedSamples samples;
    samples.reserve(static_cast<std::size_t>(config_.samples_per_frame) * config_.format.channel_count);
    for (std::size_t sample_index = 0; sample_index < config_.samples_per_frame; ++sample_index) {
        const auto absolute_sample = sequence_ * config_.samples_per_frame + sample_index;
        const auto value = amplitude_ * std::sin(
            2.0F * kPi * frequency_hz_ * static_cast<float>(absolute_sample) /
            static_cast<float>(config_.format.sample_rate));
        for (std::uint16_t channel = 0; channel < config_.format.channel_count; ++channel) {
            samples.push_back(value);
        }
    }
    gain_.process(samples);
    AudioMeter meter;
    PeakLevels peaks;
    meter.measure(samples, config_.format, peaks);
    const auto timestamp = AudioFrame::Timestamp{
        static_cast<std::int64_t>(sequence_) * config_.frame_duration().count()};
    AudioFrame frame{config_.format, sequence_, timestamp,
                     float_to_pcm16(samples, config_.format)};
    ++sequence_;
    return StreamFrameResult{std::move(frame), peaks.overall};
}

const session::AudioSessionConfig& SyntheticAudioSource::configuration() const noexcept {
    return config_;
}
std::uint64_t SyntheticAudioSource::generated() const noexcept { return sequence_; }

std::vector<std::uint8_t> serialize_media_frame(const session::SessionId& session_id,
                                                const AudioFrame& frame) {
    if (!session_id.valid()) throw std::invalid_argument("invalid media session id");
    auto packet = network::serialize_audio_frame(frame);
    std::vector<std::uint8_t> envelope;
    envelope.reserve(kSessionEnvelopeSize + packet.size());
    append_session_id(envelope, session_id);
    envelope.insert(envelope.end(), packet.begin(), packet.end());
    return envelope;
}

std::optional<AudioFrame> deserialize_media_frame(
    const session::SessionId& expected_session,
    const std::vector<std::uint8_t>& packet) {
    if (!expected_session.valid() || packet.size() <= kSessionEnvelopeSize) return std::nullopt;
    std::array<std::uint8_t, 16> id_bytes{};
    std::copy_n(packet.begin(), id_bytes.size(), id_bytes.begin());
    const session::SessionId packet_session{id_bytes};
    if (packet_session != expected_session) return std::nullopt;
    std::vector<std::uint8_t> audio_packet(packet.begin() + static_cast<std::ptrdiff_t>(kSessionEnvelopeSize), packet.end());
    const auto parsed = network::deserialize_audio_frame(audio_packet);
    return parsed.frame;
}

AudioStreamSender::AudioStreamSender(session::SessionId session_id,
                                     session::AudioSessionConfig config,
                                     const std::string& address)
    : session_id_(session_id), config_(config), socket_(::socket(AF_INET, SOCK_DGRAM, 0)) {
    if (!session_id_.valid() || !config_.is_valid() || !socket_.valid())
        throw std::invalid_argument("invalid audio stream sender configuration");
    destination_.sin_family = AF_INET;
    destination_.sin_port = htons(config_.receiver_audio_port);
    if (inet_pton(AF_INET, address.c_str(), &destination_.sin_addr) != 1)
        throw std::invalid_argument("invalid stream destination address");
}

bool AudioStreamSender::prepare() noexcept {
    if (state_ != StreamState::created) return false;
    state_ = StreamState::ready;
    return true;
}

bool AudioStreamSender::start() noexcept {
    if (state_ != StreamState::ready) return false;
    state_ = StreamState::running;
    return true;
}

bool AudioStreamSender::send(const AudioFrame& frame) {
    if (state_ != StreamState::running || !frame_matches(frame, config_)) return false;
    const auto packet = serialize_media_frame(session_id_, frame);
    const auto result = sendto(socket_.descriptor(), packet.data(), packet.size(), 0,
                               reinterpret_cast<const sockaddr*>(&destination_), sizeof(destination_));
    if (result < 0 || static_cast<std::size_t>(result) != packet.size()) {
        state_ = StreamState::failed;
        return false;
    }
    ++statistics_.frames_transmitted;
    statistics_.bytes_transmitted += packet.size();
    return true;
}

bool AudioStreamSender::stop() noexcept {
    if (state_ == StreamState::stopped) return true;
    if (state_ != StreamState::running && state_ != StreamState::ready) return false;
    state_ = StreamState::draining;
    state_ = StreamState::stopped;
    return true;
}

StreamState AudioStreamSender::state() const noexcept { return state_; }
const StreamStatistics& AudioStreamSender::statistics() const noexcept { return statistics_; }

AudioStreamReceiver::AudioStreamReceiver(session::SessionId session_id,
                                         session::AudioSessionConfig config,
                                         std::uint16_t listen_port,
                                         std::chrono::nanoseconds missing_wait)
    : session_id_(session_id), config_(config), socket_(::socket(AF_INET, SOCK_DGRAM, 0)),
    jitter_buffer_(8, 1, missing_wait), scheduler_(std::chrono::nanoseconds::zero(), clock_estimator_) {
    if (!session_id_.valid() || !config_.is_valid() || !socket_.valid())
        throw std::invalid_argument("invalid audio stream receiver configuration");
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(listen_port);
    if (bind(socket_.descriptor(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0)
        throw std::runtime_error(std::string("stream bind failed: ") + std::strerror(errno));
    sockaddr_in bound{};
    socklen_t size = sizeof(bound);
    if (getsockname(socket_.descriptor(), reinterpret_cast<sockaddr*>(&bound), &size) != 0)
        throw std::runtime_error("stream getsockname failed");
    port_ = ntohs(bound.sin_port);
}

bool AudioStreamReceiver::prepare() noexcept {
    if (state_ != StreamState::created) return false;
    state_ = StreamState::ready;
    return true;
}

bool AudioStreamReceiver::start() noexcept {
    if (state_ != StreamState::ready) return false;
    state_ = StreamState::running;
    return true;
}

std::size_t AudioStreamReceiver::poll(timing::ClockTimePoint arrival_time) {
    if (state_ != StreamState::running) return 0;
    std::size_t processed = 0;
    std::vector<std::uint8_t> packet(network::kMaxPacketSize + kSessionEnvelopeSize);
    while (true) {
        const auto result = recvfrom(socket_.descriptor(), packet.data(), packet.size(), MSG_DONTWAIT,
                                     nullptr, nullptr);
        if (result < 0) break;
        ++statistics_.datagrams_received;
        if (static_cast<std::size_t>(result) > packet.size()) {
            ++statistics_.malformed_packets;
            continue;
        }
        packet.resize(static_cast<std::size_t>(result));
        const auto frame = deserialize_media_frame(session_id_, packet);
        packet.resize(network::kMaxPacketSize + kSessionEnvelopeSize);
        if (!frame.has_value() || !frame_matches(*frame, config_)) {
            ++statistics_.incompatible_packets;
            continue;
        }
        ++processed;
        sequence_tracker_.record_received();
        const auto disposition = sequence_tracker_.observe(frame->sequence_number());
        const auto accepted_sample = clock_estimator_.add_sample(frame->timestamp(), arrival_time);
        (void)accepted_sample;
        if (disposition == network::SequenceDisposition::duplicate) ++statistics_.duplicate_frames;
        if (disposition == network::SequenceDisposition::out_of_order) ++statistics_.out_of_order_frames;
        statistics_.estimated_missing_frames = sequence_tracker_.statistics().estimated_missing_packets;
        const auto inserted = jitter_buffer_.insert(*frame, arrival_time);
        if (inserted == timing::InsertResult::inserted) {
            ++statistics_.frames_accepted;
            ++statistics_.frames_inserted;
        }
        statistics_.jitter_buffer_depth = jitter_buffer_.size();
    }
    return processed;
}

std::size_t AudioStreamReceiver::playback_step(timing::ClockTimePoint now) {
    if (state_ != StreamState::running) return 0;
    const auto item = jitter_buffer_.pop(now);
    if (item.status != timing::PullStatus::frame && item.status != timing::PullStatus::missing) return 0;
    if (item.frame.has_value()) {
        const auto scheduled = scheduler_.schedule(item.sequence_number, item.frame->timestamp());
        if (scheduled.has_value()) {
            const auto decision = scheduler_.decide(*scheduled, now);
            if (decision == timing::ScheduleDecision::too_early) return 0;
        }
    }
    playback_.consume(item);
    ++statistics_.frames_played;
    statistics_.samples_played += item.frame.has_value()
        ? item.frame->payload().size() /
          (static_cast<std::size_t>(config_.format.channel_count) *
           static_cast<std::size_t>(config_.format.bits_per_sample / 8)) * config_.format.channel_count
        : 0;
    if (item.concealed) ++statistics_.concealed_frames;
    const auto& playback_statistics = playback_.statistics();
    statistics_.peak_level = playback_statistics.peak_level;
    statistics_.jitter_buffer_depth = jitter_buffer_.size();
    return 1;
}

bool AudioStreamReceiver::stop() noexcept {
    if (state_ == StreamState::stopped) return true;
    if (state_ != StreamState::running && state_ != StreamState::ready) return false;
    state_ = StreamState::draining;
    jitter_buffer_.close();
    state_ = StreamState::stopped;
    return true;
}

StreamState AudioStreamReceiver::state() const noexcept { return state_; }
std::uint16_t AudioStreamReceiver::port() const noexcept { return port_; }
const StreamStatistics& AudioStreamReceiver::statistics() const noexcept { return statistics_; }
const timing::SimulatedPlaybackStatistics& AudioStreamReceiver::playback_statistics() const noexcept {
    return playback_.statistics();
}

}  // namespace distributed_audio::stream
