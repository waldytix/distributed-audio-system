#include "distributed_audio/udp_sender.hpp"

#include "distributed_audio/packet_serializer.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>

namespace distributed_audio::network {

UdpSender::UdpSender(const std::string& address, std::uint16_t port)
    : socket_(::socket(AF_INET, SOCK_DGRAM, 0)) {
    if (!socket_.valid()) {
        throw std::runtime_error(std::string("socket creation failed: ") + std::strerror(errno));
    }
    destination_.sin_family = AF_INET;
    destination_.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &destination_.sin_addr) != 1) {
        throw std::invalid_argument("invalid IPv4 destination address");
    }
}

std::size_t UdpSender::send(const AudioFrame& frame) const {
    const auto packet = serialize_audio_frame(frame);
    const auto result = sendto(socket_.descriptor(), packet.data(), packet.size(), 0,
                               reinterpret_cast<const sockaddr*>(&destination_),
                               sizeof(destination_));
    if (result < 0) {
        throw std::runtime_error(std::string("UDP send failed: ") + std::strerror(errno));
    }
    if (static_cast<std::size_t>(result) != packet.size()) {
        throw std::runtime_error("UDP send was unexpectedly partial");
    }
    return static_cast<std::size_t>(result);
}

}  // namespace distributed_audio::network
