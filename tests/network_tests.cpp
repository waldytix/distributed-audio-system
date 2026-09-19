#include "distributed_audio/network_protocol.hpp"
#include "distributed_audio/packet_serializer.hpp"
#include "distributed_audio/sequence_tracker.hpp"
#include "distributed_audio/udp_receiver.hpp"
#include "distributed_audio/udp_sender.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using distributed_audio::AudioFormat;
using distributed_audio::AudioFrame;
using distributed_audio::network::ParseError;
using distributed_audio::network::SequenceDisposition;
using distributed_audio::network::SequenceTracker;
using distributed_audio::network::UdpReceiver;
using distributed_audio::network::UdpSender;
using distributed_audio::network::deserialize_audio_frame;
using distributed_audio::network::serialize_audio_frame;
using distributed_audio::network::kHeaderSize;
using distributed_audio::network::kMagic;
using distributed_audio::network::kMaxPacketSize;
using distributed_audio::network::kProtocolVersion;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void set_u16(std::vector<std::uint8_t>& packet, std::size_t offset, std::uint16_t value) {
    packet[offset] = static_cast<std::uint8_t>(value >> 8U);
    packet[offset + 1] = static_cast<std::uint8_t>(value);
}

void set_u32(std::vector<std::uint8_t>& packet, std::size_t offset, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        packet[offset++] = static_cast<std::uint8_t>(value >> shift);
    }
}

AudioFrame make_frame(std::uint64_t sequence) {
    return AudioFrame{AudioFormat{48'000, 2, 16}, sequence,
                      AudioFrame::Timestamp{static_cast<std::int64_t>(sequence * 1000)},
                      AudioFrame::Payload{0, 1, 2, 3, 4, 5, 6, 7}};
}

void require_error(const std::vector<std::uint8_t>& packet,
                   ParseError expected,
                   const char* message) {
    const auto result = deserialize_audio_frame(packet);
    require(!result && result.error == expected, message);
}

void test_serialization_round_trip() {
    const auto frame = make_frame(0x0102030405060708ULL);
    const auto packet = serialize_audio_frame(frame);
    require(packet.size() == kHeaderSize + frame.payload().size(), "unexpected packet size");
    require(packet[0] == 0x44 && packet[1] == 0x41 && packet[2] == 0x53 && packet[3] == 0x31,
            "magic was not serialized in network byte order");
    require(packet[4] == kProtocolVersion && packet[5] == 0 && packet[6] == 0 && packet[7] == 0,
            "header version or flags are incorrect");
    require(packet[8] == 0x01 && packet[9] == 0x02 && packet[10] == 0x03 && packet[15] == 0x08,
            "sequence was not serialized in network byte order");
    require(packet[24] == 0x00 && packet[25] == 0x00 && packet[26] == 0xBB && packet[27] == 0x80,
            "sample rate was not serialized in network byte order");

    const auto parsed = deserialize_audio_frame(packet);
    require(static_cast<bool>(parsed), "valid packet was rejected");
    require(parsed.frame->format() == frame.format(), "format was not preserved");
    require(parsed.frame->sequence_number() == frame.sequence_number(), "sequence was not preserved");
    require(parsed.frame->timestamp() == frame.timestamp(), "timestamp was not preserved");
    require(parsed.frame->payload() == frame.payload(), "payload was not preserved");
}

void test_malformed_packets() {
    require_error({}, ParseError::truncated_header, "empty packet was accepted");
    require_error({0}, ParseError::truncated_header, "one-byte packet was accepted");
    auto packet = serialize_audio_frame(make_frame(1));
    packet[0] = 0;
    require_error(packet, ParseError::invalid_magic, "invalid magic was accepted");
    packet = serialize_audio_frame(make_frame(1));
    packet[4] = 2;
    require_error(packet, ParseError::unsupported_version, "unsupported version was accepted");
    packet = serialize_audio_frame(make_frame(1));
    set_u32(packet, 24, 0);
    require_error(packet, ParseError::invalid_sample_rate, "invalid sample rate was accepted");
    packet = serialize_audio_frame(make_frame(1));
    set_u16(packet, 28, 0);
    require_error(packet, ParseError::invalid_channels, "zero channels were accepted");
    packet = serialize_audio_frame(make_frame(1));
    packet[30] = 99;
    require_error(packet, ParseError::unsupported_sample_format, "unsupported format was accepted");
    packet = serialize_audio_frame(make_frame(1));
    set_u32(packet, 36, 2);
    require_error(packet, ParseError::invalid_payload_size, "payload mismatch was accepted");
    packet = serialize_audio_frame(make_frame(1));
    packet.pop_back();
    require_error(packet, ParseError::truncated_payload, "truncated payload was accepted");
    packet = serialize_audio_frame(make_frame(1));
    packet.resize(kMaxPacketSize + 1);
    require_error(packet, ParseError::excessive_packet_size, "oversized packet was accepted");

    auto impossible = serialize_audio_frame(make_frame(1));
    set_u32(impossible, 32, 0);
    require_error(impossible, ParseError::invalid_sample_count, "zero sample count was accepted");
}

void test_sequence_tracking() {
    SequenceTracker tracker;
    require(tracker.observe(0) == SequenceDisposition::first, "first sequence failed");
    require(tracker.observe(1) == SequenceDisposition::expected, "expected sequence failed");
    require(tracker.observe(1) == SequenceDisposition::duplicate, "duplicate sequence failed");
    require(tracker.observe(3) == SequenceDisposition::gap, "gap sequence failed");
    require(tracker.observe(2) == SequenceDisposition::out_of_order, "out of order failed");

    SequenceTracker wrap_tracker;
    require(wrap_tracker.observe(UINT64_MAX - 1) == SequenceDisposition::first,
            "wrap first failed");
    require(wrap_tracker.observe(UINT64_MAX) == SequenceDisposition::expected,
            "wrap expected failed");
    require(wrap_tracker.observe(0) == SequenceDisposition::expected, "wrap rollover failed");
    const auto& stats = tracker.statistics();
    require(stats.duplicate_packets == 1 && stats.estimated_missing_packets == 1 &&
                stats.out_of_order_packets == 1,
            "sequence statistics were incorrect");
}

void test_udp_loopback() {
    UdpReceiver receiver{"127.0.0.1", 0, 1000};
    UdpSender sender{"127.0.0.1", receiver.port()};
    const auto frame = make_frame(42);
    require(sender.send(frame) == kHeaderSize + frame.payload().size(), "UDP send failed");
    const auto received = receiver.receive();
    require(received.has_value(), "UDP receiver did not return a frame");
    require(received->sequence_number() == frame.sequence_number(), "UDP sequence mismatch");
    require(received->timestamp() == frame.timestamp(), "UDP timestamp mismatch");
    require(received->payload() == frame.payload(), "UDP payload mismatch");
    const auto& stats = receiver.statistics();
    require(stats.packets_received == 1 && stats.packets_accepted == 1 &&
                stats.malformed_packets == 0, "UDP statistics were incorrect");
}

}  // namespace

int main() {
    try {
        test_serialization_round_trip();
        test_malformed_packets();
        test_sequence_tracking();
        test_udp_loopback();
    } catch (const std::exception& error) {
        std::cerr << "Network test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All network tests passed.\n";
    return 0;
}
