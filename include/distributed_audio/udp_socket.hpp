#pragma once

namespace distributed_audio::network {

class UdpSocket {
public:
    UdpSocket();
    explicit UdpSocket(int descriptor) noexcept;
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    [[nodiscard]] int descriptor() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    int release() noexcept;

private:
    int descriptor_{-1};
};

}  // namespace distributed_audio::network
