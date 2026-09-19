#pragma once

#include "distributed_audio/device_discovery.hpp"
#include "distributed_audio/clock.hpp"
#include "distributed_audio/udp_socket.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace distributed_audio::session {

class SessionId {
public:
    SessionId() = default;
    explicit SessionId(std::array<std::uint8_t, 16> bytes) noexcept;
    [[nodiscard]] static SessionId from_u64(std::uint64_t value) noexcept;
    [[nodiscard]] static std::optional<SessionId> from_string(const std::string& text);
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] const std::array<std::uint8_t, 16>& bytes() const noexcept;
    [[nodiscard]] bool operator==(const SessionId& other) const noexcept;
    [[nodiscard]] bool operator!=(const SessionId& other) const noexcept;
    [[nodiscard]] bool operator<(const SessionId& other) const noexcept;

private:
    std::array<std::uint8_t, 16> bytes_{};
};

struct AudioSessionConfig {
    AudioFormat format;
    std::uint32_t samples_per_frame{240};
    discovery::DeviceId sender;
    discovery::DeviceId receiver;
    std::uint16_t sender_audio_port{0};
    std::uint16_t receiver_audio_port{0};

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds frame_duration() const noexcept;
    [[nodiscard]] bool operator==(const AudioSessionConfig& other) const noexcept;
    [[nodiscard]] bool operator!=(const AudioSessionConfig& other) const noexcept;
};

enum class AudioSessionState : std::uint8_t {
    idle,
    negotiating,
    established,
    streaming,
    stopping,
    closed,
    failed
};

enum class SessionMessageType : std::uint8_t {
    offer = 1,
    accept = 2,
    reject = 3,
    start = 4,
    stop = 5,
    ack = 6
};

enum class SessionRejectReason : std::uint8_t {
    none = 0,
    incompatible_format = 1,
    unsupported_sample_rate = 2,
    unsupported_channels = 3,
    invalid_configuration = 4,
    unknown_device = 5,
    busy = 6,
    protocol_error = 7,
    timeout = 8,
    remote_unavailable = 9
};

enum class SessionParseError {
    none,
    truncated,
    invalid_magic,
    unsupported_version,
    invalid_type,
    invalid_length,
    invalid_session_id,
    invalid_device_id,
    invalid_configuration,
    invalid_reason,
    excessive_packet
};

struct SessionControlMessage {
    SessionMessageType type{SessionMessageType::offer};
    SessionId session_id;
    discovery::DeviceId source;
    discovery::DeviceId destination;
    std::optional<AudioSessionConfig> configuration;
    SessionRejectReason reason{SessionRejectReason::none};
};

struct SessionParseResult {
    std::optional<SessionControlMessage> message;
    SessionParseError error{SessionParseError::none};
    [[nodiscard]] explicit operator bool() const noexcept { return message.has_value(); }
};

constexpr std::uint32_t kSessionMagic = 0x53455331U;
constexpr std::uint8_t kSessionProtocolVersion = 1;
constexpr std::size_t kSessionHeaderSize = 12;
constexpr std::size_t kSessionMaxPacketSize = 256;
constexpr std::size_t kSessionMaxSamplesPerFrame = 4'096;

[[nodiscard]] std::vector<std::uint8_t> serialize_session_message(
    const SessionControlMessage& message);
[[nodiscard]] SessionParseResult deserialize_session_message(
    const std::vector<std::uint8_t>& packet) noexcept;
[[nodiscard]] std::string_view session_parse_error_message(SessionParseError error) noexcept;

struct SessionInfo {
    SessionId id;
    discovery::DeviceId local_device;
    discovery::DeviceId remote_device;
    AudioSessionConfig configuration;
    AudioSessionState state{AudioSessionState::idle};
    timing::ClockTimePoint created_at;
    timing::ClockTimePoint last_activity;
    SessionRejectReason failure_reason{SessionRejectReason::none};
};

class AudioSessionManager {
public:
    AudioSessionManager(discovery::DeviceInfo local_device,
                        std::chrono::nanoseconds negotiation_timeout);

    [[nodiscard]] std::optional<SessionControlMessage> create_offer(
        const discovery::DeviceInfo& remote, timing::ClockTimePoint now);
    [[nodiscard]] std::optional<SessionControlMessage> handle(
        const SessionControlMessage& message, timing::ClockTimePoint now);
    [[nodiscard]] std::optional<SessionControlMessage> start(
        const SessionId& id, timing::ClockTimePoint now);
    [[nodiscard]] std::optional<SessionControlMessage> stop(
        const SessionId& id, timing::ClockTimePoint now);
    [[nodiscard]] std::size_t expire(timing::ClockTimePoint now);
    void device_unavailable(const discovery::DeviceId& id, timing::ClockTimePoint now);

    [[nodiscard]] std::optional<SessionInfo> lookup(const SessionId& id) const;
    [[nodiscard]] std::vector<SessionInfo> sessions() const;
    [[nodiscard]] const discovery::DeviceInfo& local_device() const noexcept;

private:
    [[nodiscard]] std::optional<AudioSessionConfig> choose_configuration(
        const discovery::DeviceInfo& remote) const;
    [[nodiscard]] bool transition(SessionInfo& info, AudioSessionState next) noexcept;
    [[nodiscard]] SessionId next_session_id() noexcept;

    discovery::DeviceInfo local_device_;
    std::chrono::nanoseconds negotiation_timeout_;
    std::uint64_t next_id_{1};
    mutable std::mutex mutex_;
    std::map<SessionId, SessionInfo> sessions_;
};

class SessionControlService {
public:
    SessionControlService(AudioSessionManager& manager,
                          std::uint16_t listen_port,
                          std::chrono::milliseconds receive_timeout);

    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] bool send(const SessionControlMessage& message,
                             const std::string& address,
                             std::uint16_t port) const;
    [[nodiscard]] std::size_t poll(timing::ClockTimePoint now);
    void shutdown() noexcept;

private:
    network::UdpSocket socket_;
    AudioSessionManager& manager_;
    std::uint16_t port_{0};
    std::chrono::milliseconds receive_timeout_;
    bool stopped_{false};
};

}  // namespace distributed_audio::session
