#pragma once

#include "distributed_audio/audio_format.hpp"
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

namespace distributed_audio::discovery {

class DeviceId {
public:
    DeviceId() = default;
    explicit DeviceId(std::array<std::uint8_t, 16> bytes) noexcept;

    [[nodiscard]] static DeviceId from_u64(std::uint64_t value) noexcept;
    [[nodiscard]] static std::optional<DeviceId> from_string(const std::string& text);
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] const std::array<std::uint8_t, 16>& bytes() const noexcept;
    [[nodiscard]] bool operator==(const DeviceId& other) const noexcept;
    [[nodiscard]] bool operator!=(const DeviceId& other) const noexcept;
    [[nodiscard]] bool operator<(const DeviceId& other) const noexcept;

private:
    std::array<std::uint8_t, 16> bytes_{};
};

struct DeviceInfo {
    DeviceId id;
    std::string name;
    std::uint16_t protocol_version{1};
    std::string address{"127.0.0.1"};
    std::vector<std::uint32_t> sample_rates;
    std::vector<std::uint16_t> channel_counts;
    std::vector<std::uint16_t> bits_per_sample;
    std::uint16_t audio_port{0};
    std::uint16_t discovery_port{0};
};

enum class MessageType : std::uint8_t {
    announce = 1,
    query = 2,
    response = 3,
    goodbye = 4
};

enum class DiscoveryParseError {
    none,
    truncated,
    invalid_magic,
    unsupported_version,
    invalid_type,
    invalid_length,
    invalid_device_id,
    invalid_string,
    invalid_capabilities,
    excessive_packet
};

struct DiscoveryMessage {
    MessageType type{MessageType::query};
    std::optional<DeviceInfo> device;
};

struct DiscoveryParseResult {
    std::optional<DiscoveryMessage> message;
    DiscoveryParseError error{DiscoveryParseError::none};
    [[nodiscard]] explicit operator bool() const noexcept { return message.has_value(); }
};

constexpr std::uint32_t kDiscoveryMagic = 0x44495331U;
constexpr std::uint8_t kDiscoveryVersion = 1;
constexpr std::size_t kDiscoveryHeaderSize = 12;
constexpr std::size_t kDiscoveryMaxPacketSize = 1'024;
constexpr std::size_t kDiscoveryMaxNameLength = 96;
constexpr std::size_t kDiscoveryMaxAddressLength = 64;
constexpr std::size_t kDiscoveryMaxCapabilities = 16;

[[nodiscard]] std::vector<std::uint8_t> serialize_discovery_message(
    const DiscoveryMessage& message);
[[nodiscard]] DiscoveryParseResult deserialize_discovery_message(
    const std::vector<std::uint8_t>& packet) noexcept;
[[nodiscard]] std::string_view discovery_error_message(DiscoveryParseError error) noexcept;

struct RegistryEntry {
    DeviceInfo info;
    timing::ClockTimePoint last_seen;
    bool active{true};
};

class DeviceRegistry {
public:
    explicit DeviceRegistry(std::chrono::nanoseconds stale_timeout);

    [[nodiscard]] bool observe(const DeviceInfo& info, timing::ClockTimePoint now);
    [[nodiscard]] bool remove(const DeviceId& id);
    [[nodiscard]] std::optional<RegistryEntry> lookup(const DeviceId& id) const;
    [[nodiscard]] std::vector<RegistryEntry> devices() const;
    [[nodiscard]] std::size_t expire(timing::ClockTimePoint now);
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::chrono::nanoseconds stale_timeout() const noexcept;

private:
    const std::chrono::nanoseconds stale_timeout_;
    mutable std::mutex mutex_;
    std::map<DeviceId, RegistryEntry> entries_;
};

struct CompatibilityResult {
    bool compatible{false};
    std::optional<std::uint32_t> sample_rate;
    std::optional<std::uint16_t> channels;
    std::optional<std::uint16_t> bits_per_sample;
    std::string reason;
};

[[nodiscard]] CompatibilityResult check_compatibility(const DeviceInfo& first,
                                                      const DeviceInfo& second);

class DiscoveryService {
public:
    DiscoveryService(DeviceInfo local_info,
                      std::uint16_t listen_port,
                      std::chrono::milliseconds receive_timeout,
                      std::chrono::nanoseconds stale_timeout);

    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] const DeviceInfo& local_info() const noexcept;
    void update_local_info(DeviceInfo info);
    [[nodiscard]] const DeviceRegistry& registry() const noexcept;
    [[nodiscard]] DeviceRegistry& registry() noexcept;
    [[nodiscard]] bool send(MessageType type, const std::string& address,
                             std::uint16_t port) const;
    [[nodiscard]] std::size_t poll(timing::ClockTimePoint now);
    void shutdown() noexcept;

private:
    network::UdpSocket socket_;
    DeviceInfo local_info_;
    DeviceRegistry registry_;
    std::uint16_t port_{0};
    std::chrono::milliseconds receive_timeout_;
    bool stopped_{false};
};

}  // namespace distributed_audio::discovery
