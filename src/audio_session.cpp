#include "distributed_audio/audio_session.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace distributed_audio::session {
namespace {

void append_u8(std::vector<std::uint8_t>& out, std::uint8_t value) { out.push_back(value); }
void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}
void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}
std::uint8_t read_u8(const std::vector<std::uint8_t>& data, std::size_t& offset) { return data[offset++]; }
std::uint16_t read_u16(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    const auto value = static_cast<std::uint16_t>(data[offset]) << 8U |
                       static_cast<std::uint16_t>(data[offset + 1]);
    offset += 2;
    return value;
}
std::uint32_t read_u32(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) value = (value << 8U) | data[offset++];
    return value;
}
void append_id(std::vector<std::uint8_t>& out, const std::array<std::uint8_t, 16>& bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

bool supported(const discovery::DeviceInfo& device, const AudioFormat& format) {
    return std::find(device.sample_rates.begin(), device.sample_rates.end(), format.sample_rate) != device.sample_rates.end() &&
           std::find(device.channel_counts.begin(), device.channel_counts.end(), format.channel_count) != device.channel_counts.end() &&
           std::find(device.bits_per_sample.begin(), device.bits_per_sample.end(), format.bits_per_sample) != device.bits_per_sample.end();
}

SessionParseResult failure(SessionParseError error) noexcept { return {std::nullopt, error}; }

}  // namespace

SessionId::SessionId(std::array<std::uint8_t, 16> bytes) noexcept : bytes_(bytes) {}

SessionId SessionId::from_u64(std::uint64_t value) noexcept {
    std::array<std::uint8_t, 16> bytes{};
    bytes[0] = 0x53;
    for (int index = 0; index < 8; ++index) bytes[15 - index] = static_cast<std::uint8_t>(value >> (index * 8));
    return SessionId{bytes};
}

std::optional<SessionId> SessionId::from_string(const std::string& text) {
    if (text.size() != 32) return std::nullopt;
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        unsigned int value = 0;
        std::istringstream stream{text.substr(index * 2, 2)};
        stream >> std::hex >> value;
        if (stream.fail() || value > 0xFFU) return std::nullopt;
        bytes[index] = static_cast<std::uint8_t>(value);
    }
    return SessionId{bytes};
}

bool SessionId::valid() const noexcept {
    return std::any_of(bytes_.begin(), bytes_.end(), [](std::uint8_t byte) { return byte != 0; });
}
std::string SessionId::to_string() const {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto byte : bytes_) stream << std::setw(2) << static_cast<unsigned int>(byte);
    return stream.str();
}
const std::array<std::uint8_t, 16>& SessionId::bytes() const noexcept { return bytes_; }
bool SessionId::operator==(const SessionId& other) const noexcept { return bytes_ == other.bytes_; }
bool SessionId::operator!=(const SessionId& other) const noexcept { return !(*this == other); }
bool SessionId::operator<(const SessionId& other) const noexcept { return bytes_ < other.bytes_; }

bool AudioSessionConfig::is_valid() const noexcept {
    return format.is_valid() && format.bits_per_sample == 16 && samples_per_frame > 0 &&
           samples_per_frame <= 4'096 && sender.valid() && receiver.valid() &&
           sender_audio_port != 0 && receiver_audio_port != 0;
}

std::chrono::nanoseconds AudioSessionConfig::frame_duration() const noexcept {
    if (!is_valid()) return std::chrono::nanoseconds::zero();
    return std::chrono::nanoseconds{
        static_cast<std::int64_t>((static_cast<std::uint64_t>(samples_per_frame) * 1'000'000'000ULL) /
                                  format.sample_rate)};
}

bool AudioSessionConfig::operator==(const AudioSessionConfig& other) const noexcept {
    return format == other.format && samples_per_frame == other.samples_per_frame &&
           sender == other.sender && receiver == other.receiver &&
           sender_audio_port == other.sender_audio_port &&
           receiver_audio_port == other.receiver_audio_port;
}

bool AudioSessionConfig::operator!=(const AudioSessionConfig& other) const noexcept {
    return !(*this == other);
}

std::vector<std::uint8_t> serialize_session_message(const SessionControlMessage& message) {
    const auto has_config = message.configuration.has_value();
    if (!message.session_id.valid() || !message.source.valid() || !message.destination.valid())
        throw std::invalid_argument("invalid session or device identity");
    if ((message.type == SessionMessageType::offer || message.type == SessionMessageType::accept) != has_config)
        throw std::invalid_argument("invalid configuration presence for session message");
    if (message.type == SessionMessageType::reject && message.reason == SessionRejectReason::none)
        throw std::invalid_argument("rejection requires a reason");
    std::vector<std::uint8_t> payload;
    payload.reserve(64);
    append_id(payload, message.session_id.bytes());
    append_id(payload, message.source.bytes());
    append_id(payload, message.destination.bytes());
    append_u8(payload, static_cast<std::uint8_t>(message.reason));
    append_u8(payload, has_config ? 1 : 0);
    if (has_config) {
        const auto& config = *message.configuration;
        if (!config.is_valid()) throw std::invalid_argument("invalid session configuration");
        append_id(payload, config.sender.bytes());
        append_id(payload, config.receiver.bytes());
        append_u32(payload, config.format.sample_rate);
        append_u16(payload, config.format.channel_count);
        append_u16(payload, config.format.bits_per_sample);
        append_u32(payload, config.samples_per_frame);
        append_u16(payload, config.sender_audio_port);
        append_u16(payload, config.receiver_audio_port);
    }
    if (payload.size() + kSessionHeaderSize > kSessionMaxPacketSize)
        throw std::invalid_argument("session control packet too large");
    std::vector<std::uint8_t> packet;
    packet.reserve(kSessionHeaderSize + payload.size());
    append_u32(packet, kSessionMagic);
    append_u8(packet, kSessionProtocolVersion);
    append_u8(packet, static_cast<std::uint8_t>(message.type));
    append_u16(packet, 0);
    append_u32(packet, static_cast<std::uint32_t>(payload.size()));
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

SessionParseResult deserialize_session_message(const std::vector<std::uint8_t>& packet) noexcept {
    if (packet.size() < kSessionHeaderSize) return failure(SessionParseError::truncated);
    if (packet.size() > kSessionMaxPacketSize) return failure(SessionParseError::excessive_packet);
    std::size_t offset = 0;
    const auto magic = read_u32(packet, offset);
    const auto version = read_u8(packet, offset);
    const auto raw_type = read_u8(packet, offset);
    const auto reserved = read_u16(packet, offset);
    const auto payload_size = read_u32(packet, offset);
    if (magic != kSessionMagic) return failure(SessionParseError::invalid_magic);
    if (version != kSessionProtocolVersion) return failure(SessionParseError::unsupported_version);
    if (reserved != 0 || raw_type < 1 || raw_type > 6) return failure(SessionParseError::invalid_type);
    if (payload_size != packet.size() - kSessionHeaderSize) return failure(SessionParseError::invalid_length);
    if (payload_size < 50) return failure(SessionParseError::truncated);
    SessionControlMessage message;
    message.type = static_cast<SessionMessageType>(raw_type);
    std::array<std::uint8_t, 16> session_bytes{};
    std::array<std::uint8_t, 16> source_bytes{};
    std::array<std::uint8_t, 16> destination_bytes{};
    std::copy_n(packet.begin() + static_cast<std::ptrdiff_t>(offset), 16, session_bytes.begin()); offset += 16;
    std::copy_n(packet.begin() + static_cast<std::ptrdiff_t>(offset), 16, source_bytes.begin()); offset += 16;
    std::copy_n(packet.begin() + static_cast<std::ptrdiff_t>(offset), 16, destination_bytes.begin()); offset += 16;
    message.session_id = SessionId{session_bytes};
    message.source = discovery::DeviceId{source_bytes};
    message.destination = discovery::DeviceId{destination_bytes};
    message.reason = static_cast<SessionRejectReason>(read_u8(packet, offset));
    const auto has_config = read_u8(packet, offset);
    if (!message.session_id.valid() || !message.source.valid() || !message.destination.valid())
        return failure(SessionParseError::invalid_session_id);
    if (message.reason > SessionRejectReason::remote_unavailable)
        return failure(SessionParseError::invalid_reason);
    if (has_config > 1) return failure(SessionParseError::invalid_length);
    const auto needs_config = message.type == SessionMessageType::offer || message.type == SessionMessageType::accept;
    if (needs_config != (has_config != 0)) return failure(SessionParseError::invalid_configuration);
    if (has_config != 0) {
        if (packet.size() - offset != 48) return failure(SessionParseError::invalid_configuration);
        AudioSessionConfig config;
        std::array<std::uint8_t, 16> sender_bytes{};
        std::array<std::uint8_t, 16> receiver_bytes{};
        std::copy_n(packet.begin() + static_cast<std::ptrdiff_t>(offset), 16, sender_bytes.begin());
        offset += 16;
        std::copy_n(packet.begin() + static_cast<std::ptrdiff_t>(offset), 16, receiver_bytes.begin());
        offset += 16;
        config.format = AudioFormat{read_u32(packet, offset), read_u16(packet, offset), read_u16(packet, offset)};
        config.samples_per_frame = read_u32(packet, offset);
        config.sender = discovery::DeviceId{sender_bytes};
        config.receiver = discovery::DeviceId{receiver_bytes};
        config.sender_audio_port = read_u16(packet, offset);
        config.receiver_audio_port = read_u16(packet, offset);
        if (!config.is_valid()) return failure(SessionParseError::invalid_configuration);
        message.configuration = config;
    } else if (packet.size() != offset) {
        return failure(SessionParseError::invalid_length);
    }
    return SessionParseResult{message, SessionParseError::none};
}

std::string_view session_parse_error_message(SessionParseError error) noexcept {
    switch (error) {
    case SessionParseError::none: return "none";
    case SessionParseError::truncated: return "truncated";
    case SessionParseError::invalid_magic: return "invalid magic";
    case SessionParseError::unsupported_version: return "unsupported version";
    case SessionParseError::invalid_type: return "invalid type";
    case SessionParseError::invalid_length: return "invalid length";
    case SessionParseError::invalid_session_id: return "invalid identity";
    case SessionParseError::invalid_device_id: return "invalid device identity";
    case SessionParseError::invalid_configuration: return "invalid configuration";
    case SessionParseError::invalid_reason: return "invalid rejection reason";
    case SessionParseError::excessive_packet: return "excessive packet";
    }
    return "unknown";
}

AudioSessionManager::AudioSessionManager(discovery::DeviceInfo local_device,
                                           std::chrono::nanoseconds negotiation_timeout)
    : local_device_(std::move(local_device)), negotiation_timeout_(negotiation_timeout) {
    if (!local_device_.id.valid() || negotiation_timeout_ <= std::chrono::nanoseconds::zero())
        throw std::invalid_argument("invalid session manager configuration");
}

SessionId AudioSessionManager::next_session_id() noexcept {
    return SessionId::from_u64(next_id_++);
}

std::optional<AudioSessionConfig> AudioSessionManager::choose_configuration(
    const discovery::DeviceInfo& remote) const {
    const auto compatibility = discovery::check_compatibility(local_device_, remote);
    if (!compatibility.compatible) return std::nullopt;
    AudioSessionConfig config;
    config.format = AudioFormat{*compatibility.sample_rate, *compatibility.channels, *compatibility.bits_per_sample};
    config.samples_per_frame = 240;
    config.sender = local_device_.id;
    config.receiver = remote.id;
    config.sender_audio_port = local_device_.audio_port;
    config.receiver_audio_port = remote.audio_port;
    return config.is_valid() ? std::optional<AudioSessionConfig>{config} : std::nullopt;
}

std::optional<SessionControlMessage> AudioSessionManager::create_offer(
    const discovery::DeviceInfo& remote, timing::ClockTimePoint now) {
    const auto config = choose_configuration(remote);
    if (!config.has_value()) return std::nullopt;
    std::lock_guard lock(mutex_);
    const auto id = next_session_id();
    SessionInfo info{id, local_device_.id, remote.id, *config, AudioSessionState::negotiating, now, now};
    sessions_.emplace(id, info);
    return SessionControlMessage{SessionMessageType::offer, id, local_device_.id, remote.id, config, SessionRejectReason::none};
}

bool AudioSessionManager::transition(SessionInfo& info, AudioSessionState next) noexcept {
    const auto current = info.state;
    const auto valid = (current == AudioSessionState::negotiating &&
                        (next == AudioSessionState::established || next == AudioSessionState::failed)) ||
                       (current == AudioSessionState::established &&
                        (next == AudioSessionState::streaming || next == AudioSessionState::stopping || next == AudioSessionState::failed)) ||
                       (current == AudioSessionState::streaming &&
                        (next == AudioSessionState::stopping || next == AudioSessionState::failed)) ||
                       (current == AudioSessionState::stopping && next == AudioSessionState::closed) ||
                       (current == AudioSessionState::closed && next == AudioSessionState::closed) ||
                       (current == AudioSessionState::failed && next == AudioSessionState::failed);
    if (valid) info.state = next;
    return valid;
}

std::optional<SessionControlMessage> AudioSessionManager::handle(
    const SessionControlMessage& message, timing::ClockTimePoint now) {
    if (message.destination != local_device_.id || !message.session_id.valid()) return std::nullopt;
    std::lock_guard lock(mutex_);
    const auto found = sessions_.find(message.session_id);
    if (message.type == SessionMessageType::offer) {
        if (!message.configuration.has_value() || message.source == local_device_.id ||
            !supported(local_device_, message.configuration->format) ||
            !message.configuration->is_valid()) {
            return SessionControlMessage{SessionMessageType::reject, message.session_id, local_device_.id,
                                         message.source, std::nullopt, SessionRejectReason::incompatible_format};
        }
        if (found != sessions_.end()) {
            if (found->second.state == AudioSessionState::established || found->second.state == AudioSessionState::streaming)
                return SessionControlMessage{SessionMessageType::accept, message.session_id, local_device_.id,
                                             message.source, found->second.configuration, SessionRejectReason::none};
            return std::nullopt;
        }
        SessionInfo info{message.session_id, local_device_.id, message.source, *message.configuration,
                         AudioSessionState::established, now, now};
        sessions_.emplace(message.session_id, info);
        return SessionControlMessage{SessionMessageType::accept, message.session_id, local_device_.id,
                                     message.source, message.configuration, SessionRejectReason::none};
    }
    if (found == sessions_.end()) return std::nullopt;
    auto& info = found->second;
    info.last_activity = now;
    if (message.type == SessionMessageType::accept && info.state == AudioSessionState::negotiating && message.configuration == info.configuration) {
        const auto transitioned = transition(info, AudioSessionState::established);
        (void)transitioned;
        return std::nullopt;
    }
    if (message.type == SessionMessageType::reject && info.state == AudioSessionState::negotiating) {
        info.failure_reason = message.reason;
        const auto transitioned = transition(info, AudioSessionState::failed);
        (void)transitioned;
        return std::nullopt;
    }
    if (message.type == SessionMessageType::start && (info.state == AudioSessionState::established || info.state == AudioSessionState::streaming)) {
        const auto transitioned = transition(info, AudioSessionState::streaming);
        (void)transitioned;
        return SessionControlMessage{SessionMessageType::ack, info.id, local_device_.id, info.remote_device, std::nullopt, SessionRejectReason::none};
    }
    if (message.type == SessionMessageType::stop && (info.state == AudioSessionState::streaming || info.state == AudioSessionState::established || info.state == AudioSessionState::stopping)) {
        if (info.state == AudioSessionState::streaming || info.state == AudioSessionState::established) {
            const auto stopping = transition(info, AudioSessionState::stopping);
            (void)stopping;
        }
        const auto transitioned = transition(info, AudioSessionState::closed);
        (void)transitioned;
        return SessionControlMessage{SessionMessageType::ack, info.id, local_device_.id, info.remote_device, std::nullopt, SessionRejectReason::none};
    }
    if (message.type == SessionMessageType::ack && info.state == AudioSessionState::stopping) {
        const auto transitioned = transition(info, AudioSessionState::closed);
        (void)transitioned;
        return std::nullopt;
    }
    if (message.type == SessionMessageType::ack) return std::nullopt;
    return std::nullopt;
}

std::optional<SessionControlMessage> AudioSessionManager::start(const SessionId& id, timing::ClockTimePoint now) {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(id);
    if (it == sessions_.end() || !transition(it->second, AudioSessionState::streaming)) return std::nullopt;
    it->second.last_activity = now;
    return SessionControlMessage{SessionMessageType::start, id, local_device_.id, it->second.remote_device, std::nullopt, SessionRejectReason::none};
}

std::optional<SessionControlMessage> AudioSessionManager::stop(const SessionId& id, timing::ClockTimePoint now) {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(id);
    if (it == sessions_.end() || (it->second.state != AudioSessionState::streaming && it->second.state != AudioSessionState::established)) return std::nullopt;
    const auto transitioned = transition(it->second, AudioSessionState::stopping);
    (void)transitioned;
    it->second.last_activity = now;
    return SessionControlMessage{SessionMessageType::stop, id, local_device_.id, it->second.remote_device, std::nullopt, SessionRejectReason::none};
}

std::size_t AudioSessionManager::expire(timing::ClockTimePoint now) {
    std::lock_guard lock(mutex_);
    std::size_t expired = 0;
    for (auto& entry : sessions_) {
        if (entry.second.state == AudioSessionState::negotiating && now - entry.second.last_activity >= negotiation_timeout_) {
            entry.second.failure_reason = SessionRejectReason::timeout;
            const auto transitioned = transition(entry.second, AudioSessionState::failed);
            (void)transitioned;
            ++expired;
        }
    }
    return expired;
}

void AudioSessionManager::device_unavailable(const discovery::DeviceId& id, timing::ClockTimePoint now) {
    std::lock_guard lock(mutex_);
    for (auto& entry : sessions_) if (entry.second.remote_device == id && entry.second.state != AudioSessionState::closed) {
        entry.second.failure_reason = SessionRejectReason::remote_unavailable;
        entry.second.last_activity = now;
        const auto transitioned = transition(entry.second, AudioSessionState::failed);
        (void)transitioned;
    }
}

std::optional<SessionInfo> AudioSessionManager::lookup(const SessionId& id) const {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(id);
    return it == sessions_.end() ? std::nullopt : std::optional<SessionInfo>{it->second};
}
std::vector<SessionInfo> AudioSessionManager::sessions() const {
    std::lock_guard lock(mutex_);
    std::vector<SessionInfo> result;
    for (const auto& entry : sessions_) result.push_back(entry.second);
    return result;
}
const discovery::DeviceInfo& AudioSessionManager::local_device() const noexcept { return local_device_; }

SessionControlService::SessionControlService(AudioSessionManager& manager, std::uint16_t listen_port,
                                             std::chrono::milliseconds receive_timeout)
    : socket_(::socket(AF_INET, SOCK_DGRAM, 0)), manager_(manager), receive_timeout_(receive_timeout) {
    if (!socket_.valid() || receive_timeout_ < std::chrono::milliseconds::zero()) throw std::invalid_argument("invalid session control service configuration");
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(listen_port);
    if (bind(socket_.descriptor(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) throw std::runtime_error("session control bind failed");
    sockaddr_in bound{};
    socklen_t size = sizeof(bound);
    getsockname(socket_.descriptor(), reinterpret_cast<sockaddr*>(&bound), &size);
    port_ = ntohs(bound.sin_port);
}

std::uint16_t SessionControlService::port() const noexcept { return port_; }

bool SessionControlService::send(const SessionControlMessage& message, const std::string& address, std::uint16_t port) const {
    if (stopped_) return false;
    const auto packet = serialize_session_message(message);
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &destination.sin_addr) != 1) return false;
    const auto result = sendto(socket_.descriptor(), packet.data(), packet.size(), 0,
                               reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    return result == static_cast<ssize_t>(packet.size());
}

std::size_t SessionControlService::poll(timing::ClockTimePoint now) {
    if (stopped_) return 0;
    std::size_t processed = 0;
    std::vector<std::uint8_t> packet(kSessionMaxPacketSize);
    while (true) {
        sockaddr_in sender{};
        socklen_t sender_size = sizeof(sender);
        const auto result = recvfrom(socket_.descriptor(), packet.data(), packet.size(), MSG_DONTWAIT,
                                     reinterpret_cast<sockaddr*>(&sender), &sender_size);
        if (result < 0) break;
        if (static_cast<std::size_t>(result) > packet.size()) continue;
        packet.resize(static_cast<std::size_t>(result));
        const auto parsed = deserialize_session_message(packet);
        packet.resize(kSessionMaxPacketSize);
        if (!parsed) continue;
        ++processed;
        const auto response = manager_.handle(*parsed.message, now);
        if (response.has_value()) {
            char address[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &sender.sin_addr, address, sizeof(address));
            const auto sent = send(*response, address, ntohs(sender.sin_port));
            (void)sent;
        }
    }
    const auto expired = manager_.expire(now);
    (void)expired;
    return processed;
}

void SessionControlService::shutdown() noexcept { stopped_ = true; }

}  // namespace distributed_audio::session
