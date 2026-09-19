#include "distributed_audio/device_discovery.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>

namespace distributed_audio::discovery {
namespace {

void append_u8(std::vector<std::uint8_t>& out, std::uint8_t value) { out.push_back(value); }
void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}
void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}
void append_bytes(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}
std::uint8_t read_u8(const std::vector<std::uint8_t>& packet, std::size_t& offset) { return packet[offset++]; }
std::uint16_t read_u16(const std::vector<std::uint8_t>& packet, std::size_t& offset) {
    const auto value = static_cast<std::uint16_t>(packet[offset]) << 8U |
                       static_cast<std::uint16_t>(packet[offset + 1]);
    offset += 2;
    return value;
}
std::uint32_t read_u32(const std::vector<std::uint8_t>& packet, std::size_t& offset) {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) value = (value << 8U) | packet[offset++];
    return value;
}

void append_string(std::vector<std::uint8_t>& out, const std::string& value, std::size_t limit) {
    if (value.size() > limit || value.size() > 0xFFFFU) throw std::invalid_argument("discovery string too long");
    append_u16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

bool contains_rate(const std::vector<std::uint32_t>& values, std::uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
bool contains_channel(const std::vector<std::uint16_t>& values, std::uint16_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

DiscoveryParseResult failure(DiscoveryParseError error) noexcept { return {std::nullopt, error}; }

}  // namespace

DeviceId::DeviceId(std::array<std::uint8_t, 16> bytes) noexcept : bytes_(bytes) {}

DeviceId DeviceId::from_u64(std::uint64_t value) noexcept {
    std::array<std::uint8_t, 16> bytes{};
    for (int index = 0; index < 8; ++index) {
        bytes[15 - index] = static_cast<std::uint8_t>(value >> (index * 8));
    }
    bytes[0] = 0xDA;
    return DeviceId{bytes};
}

std::optional<DeviceId> DeviceId::from_string(const std::string& text) {
    if (text.size() != 32) return std::nullopt;
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        unsigned int value = 0;
        std::istringstream stream{text.substr(index * 2, 2)};
        stream >> std::hex >> value;
        if (stream.fail() || value > 0xFFU) return std::nullopt;
        bytes[index] = static_cast<std::uint8_t>(value);
    }
    return DeviceId{bytes};
}

bool DeviceId::valid() const noexcept {
    return std::any_of(bytes_.begin(), bytes_.end(), [](std::uint8_t byte) { return byte != 0; });
}

std::string DeviceId::to_string() const {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto byte : bytes_) stream << std::setw(2) << static_cast<unsigned int>(byte);
    return stream.str();
}

const std::array<std::uint8_t, 16>& DeviceId::bytes() const noexcept { return bytes_; }
bool DeviceId::operator==(const DeviceId& other) const noexcept { return bytes_ == other.bytes_; }
bool DeviceId::operator!=(const DeviceId& other) const noexcept { return !(*this == other); }
bool DeviceId::operator<(const DeviceId& other) const noexcept { return bytes_ < other.bytes_; }

std::vector<std::uint8_t> serialize_discovery_message(const DiscoveryMessage& message) {
    if (message.type != MessageType::query && !message.device.has_value()) {
        throw std::invalid_argument("discovery message requires device information");
    }
    std::vector<std::uint8_t> payload;
    if (message.device.has_value()) {
        const auto& info = *message.device;
        if (!info.id.valid() || info.name.empty() || info.name.size() > kDiscoveryMaxNameLength ||
            info.address.size() > kDiscoveryMaxAddressLength || info.sample_rates.empty() ||
            info.sample_rates.size() > kDiscoveryMaxCapabilities ||
            info.channel_counts.empty() || info.channel_counts.size() > kDiscoveryMaxCapabilities ||
            info.bits_per_sample.empty() || info.bits_per_sample.size() > kDiscoveryMaxCapabilities) {
            throw std::invalid_argument("invalid discovery device information");
        }
        payload.insert(payload.end(), info.id.bytes().begin(), info.id.bytes().end());
        append_string(payload, info.name, kDiscoveryMaxNameLength);
        append_string(payload, info.address, kDiscoveryMaxAddressLength);
        append_u16(payload, info.protocol_version);
        append_u16(payload, info.audio_port);
        append_u16(payload, info.discovery_port);
        append_u16(payload, static_cast<std::uint16_t>(info.sample_rates.size()));
        for (const auto value : info.sample_rates) append_u32(payload, value);
        append_u16(payload, static_cast<std::uint16_t>(info.channel_counts.size()));
        for (const auto value : info.channel_counts) append_u16(payload, value);
        append_u16(payload, static_cast<std::uint16_t>(info.bits_per_sample.size()));
        for (const auto value : info.bits_per_sample) append_u16(payload, value);
    }
    if (payload.size() > kDiscoveryMaxPacketSize - kDiscoveryHeaderSize) {
        throw std::invalid_argument("discovery payload is too large");
    }
    std::vector<std::uint8_t> packet;
    packet.reserve(kDiscoveryHeaderSize + payload.size());
    append_u32(packet, kDiscoveryMagic);
    append_u8(packet, kDiscoveryVersion);
    append_u8(packet, static_cast<std::uint8_t>(message.type));
    append_u16(packet, 0);
    append_u32(packet, static_cast<std::uint32_t>(payload.size()));
    append_bytes(packet, payload);
    return packet;
}

DiscoveryParseResult deserialize_discovery_message(const std::vector<std::uint8_t>& packet) noexcept {
    if (packet.size() < kDiscoveryHeaderSize) return failure(DiscoveryParseError::truncated);
    if (packet.size() > kDiscoveryMaxPacketSize) return failure(DiscoveryParseError::excessive_packet);
    std::size_t offset = 0;
    const auto magic = read_u32(packet, offset);
    const auto version = read_u8(packet, offset);
    const auto raw_type = read_u8(packet, offset);
    const auto reserved = read_u16(packet, offset);
    const auto payload_length = read_u32(packet, offset);
    if (magic != kDiscoveryMagic) return failure(DiscoveryParseError::invalid_magic);
    if (version != kDiscoveryVersion) return failure(DiscoveryParseError::unsupported_version);
    if (reserved != 0 || raw_type < 1 || raw_type > 4) return failure(raw_type < 1 || raw_type > 4
        ? DiscoveryParseError::invalid_type : DiscoveryParseError::invalid_length);
    if (payload_length != packet.size() - kDiscoveryHeaderSize) return failure(DiscoveryParseError::invalid_length);
    const auto type = static_cast<MessageType>(raw_type);
    if (type == MessageType::query && payload_length != 0) return failure(DiscoveryParseError::invalid_length);
    if (type == MessageType::query) return DiscoveryParseResult{DiscoveryMessage{type, std::nullopt}, DiscoveryParseError::none};
    const auto payload_end = packet.size();
    if (payload_end - offset < 16) return failure(DiscoveryParseError::truncated);
    DeviceInfo info;
    std::array<std::uint8_t, 16> id_bytes{};
    std::copy_n(packet.begin() + static_cast<std::ptrdiff_t>(offset), 16, id_bytes.begin());
    offset += 16;
    info.id = DeviceId{id_bytes};
    if (!info.id.valid()) return failure(DiscoveryParseError::invalid_device_id);
    auto read_string = [&](std::string& target, std::size_t limit) -> bool {
        if (payload_end - offset < 2) return false;
        const auto length = read_u16(packet, offset);
        if (length > limit || length > payload_end - offset) return false;
        target.assign(reinterpret_cast<const char*>(packet.data() + offset), length);
        offset += length;
        return true;
    };
    if (!read_string(info.name, kDiscoveryMaxNameLength) || info.name.empty() ||
        !read_string(info.address, kDiscoveryMaxAddressLength) || payload_end - offset < 8) {
        return failure(DiscoveryParseError::invalid_string);
    }
    info.protocol_version = read_u16(packet, offset);
    info.audio_port = read_u16(packet, offset);
    info.discovery_port = read_u16(packet, offset);
    auto read_capabilities = [&](auto& output, auto reader) -> bool {
        if (payload_end - offset < 2) return false;
        const auto count = read_u16(packet, offset);
        if (count == 0 || count > kDiscoveryMaxCapabilities) return false;
        for (std::uint16_t index = 0; index < count; ++index) {
            if (payload_end - offset < sizeof(typename std::decay_t<decltype(output)>::value_type)) return false;
            output.push_back(reader());
        }
        return true;
    };
    if (!read_capabilities(info.sample_rates, [&] { return read_u32(packet, offset); }) ||
        !read_capabilities(info.channel_counts, [&] { return read_u16(packet, offset); }) ||
        !read_capabilities(info.bits_per_sample, [&] { return read_u16(packet, offset); }) ||
        offset != payload_end) {
        return failure(DiscoveryParseError::invalid_capabilities);
    }
    return DiscoveryParseResult{DiscoveryMessage{type, std::move(info)}, DiscoveryParseError::none};
}

std::string_view discovery_error_message(DiscoveryParseError error) noexcept {
    switch (error) {
    case DiscoveryParseError::none: return "none";
    case DiscoveryParseError::truncated: return "truncated";
    case DiscoveryParseError::invalid_magic: return "invalid magic";
    case DiscoveryParseError::unsupported_version: return "unsupported version";
    case DiscoveryParseError::invalid_type: return "invalid type";
    case DiscoveryParseError::invalid_length: return "invalid length";
    case DiscoveryParseError::invalid_device_id: return "invalid device id";
    case DiscoveryParseError::invalid_string: return "invalid string";
    case DiscoveryParseError::invalid_capabilities: return "invalid capabilities";
    case DiscoveryParseError::excessive_packet: return "excessive packet";
    }
    return "unknown";
}

DeviceRegistry::DeviceRegistry(std::chrono::nanoseconds stale_timeout) : stale_timeout_(stale_timeout) {
    if (stale_timeout_ <= std::chrono::nanoseconds::zero()) throw std::invalid_argument("stale timeout must be positive");
}

bool DeviceRegistry::observe(const DeviceInfo& info, timing::ClockTimePoint now) {
    if (!info.id.valid()) return false;
    std::lock_guard lock(mutex_);
    const auto [it, inserted] = entries_.emplace(info.id, RegistryEntry{info, now, true});
    if (!inserted) it->second = RegistryEntry{info, now, true};
    return inserted;
}

bool DeviceRegistry::remove(const DeviceId& id) {
    std::lock_guard lock(mutex_);
    return entries_.erase(id) != 0;
}

std::optional<RegistryEntry> DeviceRegistry::lookup(const DeviceId& id) const {
    std::lock_guard lock(mutex_);
    const auto it = entries_.find(id);
    return it == entries_.end() ? std::nullopt : std::optional<RegistryEntry>{it->second};
}

std::vector<RegistryEntry> DeviceRegistry::devices() const {
    std::lock_guard lock(mutex_);
    std::vector<RegistryEntry> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) result.push_back(entry.second);
    return result;
}

std::size_t DeviceRegistry::expire(timing::ClockTimePoint now) {
    std::lock_guard lock(mutex_);
    std::size_t expired = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (now - it->second.last_seen >= stale_timeout_) {
            it = entries_.erase(it);
            ++expired;
        } else {
            ++it;
        }
    }
    return expired;
}

std::size_t DeviceRegistry::size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}
std::chrono::nanoseconds DeviceRegistry::stale_timeout() const noexcept { return stale_timeout_; }

CompatibilityResult check_compatibility(const DeviceInfo& first, const DeviceInfo& second) {
    CompatibilityResult result;
    for (const auto rate : first.sample_rates) if (contains_rate(second.sample_rates, rate)) { result.sample_rate = rate; break; }
    for (const auto channels : first.channel_counts) if (contains_channel(second.channel_counts, channels)) { result.channels = channels; break; }
    for (const auto bits : first.bits_per_sample) if (contains_channel(second.bits_per_sample, bits)) { result.bits_per_sample = bits; break; }
    result.compatible = result.sample_rate.has_value() && result.channels.has_value() && result.bits_per_sample.has_value();
    result.reason = result.compatible ? "compatible audio configuration found" : "no common audio configuration";
    return result;
}

DiscoveryService::DiscoveryService(DeviceInfo local_info, std::uint16_t listen_port,
                                   std::chrono::milliseconds receive_timeout,
                                   std::chrono::nanoseconds stale_timeout)
    : socket_(::socket(AF_INET, SOCK_DGRAM, 0)), local_info_(std::move(local_info)),
      registry_(stale_timeout), receive_timeout_(receive_timeout) {
    if (!socket_.valid() || local_info_.name.empty() || receive_timeout_ < std::chrono::milliseconds::zero())
        throw std::invalid_argument("invalid discovery service configuration");
    int reuse = 1;
    setsockopt(socket_.descriptor(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    timeval timeout{};
    timeout.tv_sec = receive_timeout_.count() / 1000;
    timeout.tv_usec = (receive_timeout_.count() % 1000) * 1000;
    if (setsockopt(socket_.descriptor(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
        throw std::runtime_error("discovery receive timeout setup failed");
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(listen_port);
    if (bind(socket_.descriptor(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0)
        throw std::runtime_error(std::string("discovery bind failed: ") + std::strerror(errno));
    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    getsockname(socket_.descriptor(), reinterpret_cast<sockaddr*>(&bound), &length);
    port_ = ntohs(bound.sin_port);
    local_info_.discovery_port = port_;
}

std::uint16_t DiscoveryService::port() const noexcept { return port_; }
const DeviceInfo& DiscoveryService::local_info() const noexcept { return local_info_; }
void DiscoveryService::update_local_info(DeviceInfo info) {
    if (!info.id.valid() || info.name.empty()) throw std::invalid_argument("invalid local device information");
    info.discovery_port = port_;
    local_info_ = std::move(info);
}
const DeviceRegistry& DiscoveryService::registry() const noexcept { return registry_; }
DeviceRegistry& DiscoveryService::registry() noexcept { return registry_; }

bool DiscoveryService::send(MessageType type, const std::string& address, std::uint16_t port) const {
    if (stopped_) return false;
    const auto packet = serialize_discovery_message(DiscoveryMessage{type, type == MessageType::query
        ? std::nullopt : std::optional<DeviceInfo>{local_info_}});
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &destination.sin_addr) != 1) return false;
    const auto result = sendto(socket_.descriptor(), packet.data(), packet.size(), 0,
                               reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    return result == static_cast<ssize_t>(packet.size());
}

std::size_t DiscoveryService::poll(timing::ClockTimePoint now) {
    if (stopped_) return 0;
    std::size_t processed = 0;
    std::vector<std::uint8_t> packet(kDiscoveryMaxPacketSize);
    while (true) {
        sockaddr_in sender{};
        socklen_t sender_length = sizeof(sender);
        const auto result = recvfrom(socket_.descriptor(), packet.data(), packet.size(), MSG_DONTWAIT,
                         reinterpret_cast<sockaddr*>(&sender), &sender_length);
        if (result < 0) break;
        if (static_cast<std::size_t>(result) > packet.size()) continue;
        packet.resize(static_cast<std::size_t>(result));
        const auto parsed = deserialize_discovery_message(packet);
        packet.resize(kDiscoveryMaxPacketSize);
        if (!parsed) continue;
        ++processed;
        if (parsed.message->type == MessageType::query) {
            char address[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &sender.sin_addr, address, sizeof(address));
            const auto responded = send(MessageType::response, address, ntohs(sender.sin_port));
            (void)responded;
        } else if (parsed.message->type == MessageType::goodbye) {
            if (parsed.message->device.has_value()) {
                const auto removed = registry_.remove(parsed.message->device->id);
                (void)removed;
            }
        } else if (parsed.message->device.has_value()) {
            const auto observed = registry_.observe(*parsed.message->device, now);
            (void)observed;
        }
    }
    const auto expired = registry_.expire(now);
    (void)expired;
    return processed;
}

void DiscoveryService::shutdown() noexcept { stopped_ = true; }

}  // namespace distributed_audio::discovery
