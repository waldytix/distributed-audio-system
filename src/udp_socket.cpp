#include "distributed_audio/udp_socket.hpp"

#include <unistd.h>

namespace distributed_audio::network {

UdpSocket::UdpSocket() = default;

UdpSocket::UdpSocket(int descriptor) noexcept : descriptor_(descriptor) {}

UdpSocket::~UdpSocket() {
    if (descriptor_ >= 0) {
        close(descriptor_);
    }
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : descriptor_(other.release()) {}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        if (descriptor_ >= 0) {
            close(descriptor_);
        }
        descriptor_ = other.release();
    }
    return *this;
}

int UdpSocket::descriptor() const noexcept { return descriptor_; }
bool UdpSocket::valid() const noexcept { return descriptor_ >= 0; }

int UdpSocket::release() noexcept {
    const auto descriptor = descriptor_;
    descriptor_ = -1;
    return descriptor;
}

}  // namespace distributed_audio::network
