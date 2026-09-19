#include "distributed_audio/udp_receiver.hpp"

#include "distributed_audio/network_protocol.hpp"
#include "distributed_audio/packet_serializer.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <vector>

namespace distributed_audio::network {

UdpReceiver::UdpReceiver(const std::string& address, std::uint16_t requested_port,
                         std::int32_t timeout_ms)
    : socket_(::socket(AF_INET, SOCK_DGRAM, 0)) {
    if (!socket_.valid()) {
        throw std::runtime_error(std::string("socket creation failed: ") + std::strerror(errno));
    }
    if (timeout_ms < 0) {
        throw std::invalid_argument("receiver timeout must not be negative");
    }
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(socket_.descriptor(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        throw std::runtime_error(std::string("receive timeout setup failed: ") + std::strerror(errno));
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(requested_port);
    if (inet_pton(AF_INET, address.c_str(), &local.sin_addr) != 1) {
        throw std::invalid_argument("invalid IPv4 bind address");
    }
    if (bind(socket_.descriptor(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        throw std::runtime_error(std::string("UDP bind failed: ") + std::strerror(errno));
    }

    sockaddr_in bound{};
    socklen_t bound_size = sizeof(bound);
    if (getsockname(socket_.descriptor(), reinterpret_cast<sockaddr*>(&bound), &bound_size) != 0) {
        throw std::runtime_error(std::string("getsockname failed: ") + std::strerror(errno));
    }
    port_ = ntohs(bound.sin_port);
}

std::optional<AudioFrame> UdpReceiver::receive() {
    std::vector<std::uint8_t> packet(kMaxPacketSize);
    const auto result = recvfrom(socket_.descriptor(), packet.data(), packet.size(), MSG_TRUNC,
                                 nullptr, nullptr);
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return std::nullopt;
        }
        throw std::runtime_error(std::string("UDP receive failed: ") + std::strerror(errno));
    }
    if (static_cast<std::size_t>(result) > packet.size()) {
        tracker_.record_received();
        tracker_.record_malformed();
        return std::nullopt;
    }
    packet.resize(static_cast<std::size_t>(result));
    tracker_.record_received();
    const auto parsed = deserialize_audio_frame(packet);
    if (!parsed) {
        tracker_.record_malformed();
        return std::nullopt;
    }
    const auto disposition = tracker_.observe(parsed.frame->sequence_number());
    (void)disposition;
    return parsed.frame;
}

std::uint16_t UdpReceiver::port() const noexcept { return port_; }
const SequenceStatistics& UdpReceiver::statistics() const noexcept { return tracker_.statistics(); }

}  // namespace distributed_audio::network
