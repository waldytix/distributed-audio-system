#pragma once

#include "distributed_audio/audio_frame.hpp"
#include "distributed_audio/udp_socket.hpp"

#include <cstdint>
#include <cstddef>
#include <netinet/in.h>
#include <string>

namespace distributed_audio::network {

class UdpSender {
public:
    UdpSender(const std::string& address, std::uint16_t port);

    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;
    UdpSender(UdpSender&&) noexcept = default;
    UdpSender& operator=(UdpSender&&) noexcept = default;

    [[nodiscard]] std::size_t send(const AudioFrame& frame) const;

private:
    UdpSocket socket_;
    sockaddr_in destination_{};
};

}  // namespace distributed_audio::network
