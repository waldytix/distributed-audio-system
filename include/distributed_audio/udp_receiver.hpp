#pragma once

#include "distributed_audio/audio_frame.hpp"
#include "distributed_audio/sequence_tracker.hpp"
#include "distributed_audio/udp_socket.hpp"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>

namespace distributed_audio::network {

class UdpReceiver {
public:
    UdpReceiver(const std::string& address, std::uint16_t port,
                std::int32_t timeout_ms = 500);

    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;
    UdpReceiver(UdpReceiver&&) noexcept = default;
    UdpReceiver& operator=(UdpReceiver&&) noexcept = default;

    [[nodiscard]] std::optional<AudioFrame> receive();
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] const SequenceStatistics& statistics() const noexcept;

private:
    UdpSocket socket_;
    std::uint16_t port_{0};
    SequenceTracker tracker_;
};

}  // namespace distributed_audio::network
