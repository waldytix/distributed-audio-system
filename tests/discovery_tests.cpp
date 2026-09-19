#include "distributed_audio/device_discovery.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using distributed_audio::discovery::CompatibilityResult;
using distributed_audio::discovery::DeviceId;
using distributed_audio::discovery::DeviceInfo;
using distributed_audio::discovery::DeviceRegistry;
using distributed_audio::discovery::DiscoveryMessage;
using distributed_audio::discovery::DiscoveryParseError;
using distributed_audio::discovery::DiscoveryService;
using distributed_audio::discovery::MessageType;
using distributed_audio::timing::ClockTimePoint;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

DeviceInfo make_info(std::uint64_t id, const std::string& name, std::uint16_t port = 0) {
    DeviceInfo info;
    info.id = DeviceId::from_u64(id);
    info.name = name;
    info.sample_rates = {44'100, 48'000};
    info.channel_counts = {1, 2};
    info.bits_per_sample = {16};
    info.audio_port = port;
    return info;
}

void test_device_id_and_codec() {
    const auto first = DeviceId::from_u64(1);
    const auto second = DeviceId::from_u64(2);
    require(first.valid() && first != second && first < second, "DeviceId comparison failed");
    require(DeviceId::from_string(first.to_string()) == first, "DeviceId string round trip failed");
    require(!DeviceId::from_string("invalid").has_value(), "invalid DeviceId was accepted");

    const auto info = make_info(1, "Living Room", 5001);
    for (const auto type : {MessageType::announce, MessageType::response, MessageType::goodbye}) {
        const auto packet = distributed_audio::discovery::serialize_discovery_message({type, info});
        require(packet[0] == 0x44 && packet[1] == 0x49 && packet[2] == 0x53 && packet[3] == 0x31,
                "discovery magic byte order failed");
        require(packet[4] == 1 && packet[5] == static_cast<std::uint8_t>(type),
                "discovery header fields failed");
        const auto parsed = distributed_audio::discovery::deserialize_discovery_message(packet);
        require(parsed && parsed.message->type == type && parsed.message->device->name == info.name,
                "discovery message round trip failed");
    }
    const auto query = distributed_audio::discovery::serialize_discovery_message(
        {MessageType::query, std::nullopt});
    const auto parsed_query = distributed_audio::discovery::deserialize_discovery_message(query);
    require(parsed_query && parsed_query.message->type == MessageType::query &&
                !parsed_query.message->device.has_value(),
            "QUERY round trip failed");
}

void test_malformed_messages() {
    const auto valid = distributed_audio::discovery::serialize_discovery_message(
        {MessageType::announce, make_info(3, "Studio")});
    auto packet = valid;
    packet.clear();
    require(distributed_audio::discovery::deserialize_discovery_message(packet).error ==
                DiscoveryParseError::truncated,
            "empty discovery packet was accepted");
    packet = valid;
    packet[0] = 0;
    require(distributed_audio::discovery::deserialize_discovery_message(packet).error ==
                DiscoveryParseError::invalid_magic,
            "invalid discovery magic was accepted");
    packet = valid;
    packet[4] = 2;
    require(distributed_audio::discovery::deserialize_discovery_message(packet).error ==
                DiscoveryParseError::unsupported_version,
            "unsupported discovery version was accepted");
    packet = valid;
    packet[5] = 99;
    require(distributed_audio::discovery::deserialize_discovery_message(packet).error ==
                DiscoveryParseError::invalid_type,
            "invalid discovery type was accepted");
    packet = valid;
    packet[8] = 0x7F;
    require(distributed_audio::discovery::deserialize_discovery_message(packet).error ==
                DiscoveryParseError::invalid_length,
            "invalid discovery payload length was accepted");
    packet = valid;
    packet.resize(2);
    require(distributed_audio::discovery::deserialize_discovery_message(packet).error ==
                DiscoveryParseError::truncated,
            "truncated discovery packet was accepted");
}

void test_registry_and_compatibility() {
    DeviceRegistry registry{100ms};
    const auto now = ClockTimePoint{};
    auto info = make_info(10, "Desktop");
    require(registry.observe(info, now), "registry insertion failed");
    require(!registry.observe(info, now + 10ms), "repeated announcement duplicated entry");
    info.name = "Updated Desktop";
    require(!registry.observe(info, now + 20ms), "registry update reported insertion");
    require(registry.lookup(info.id)->info.name == "Updated Desktop", "registry update failed");
    require(registry.expire(now + 119ms) == 0, "device expired too early");
    require(registry.expire(now + 120ms) == 1 && registry.size() == 0,
            "stale device expiration failed");

    const auto compatible = distributed_audio::discovery::check_compatibility(
        make_info(1, "A"), make_info(2, "B"));
    require(compatible.compatible && compatible.sample_rate == 44'100,
            "compatible capabilities were rejected");
    auto incompatible_info = make_info(3, "C");
    incompatible_info.sample_rates = {96'000};
    require(!distributed_audio::discovery::check_compatibility(make_info(1, "A"), incompatible_info).compatible,
            "incompatible capabilities were accepted");
    require(registry.observe(make_info(11, "Remove"), now), "registry removal setup failed");
    require(registry.remove(DeviceId::from_u64(11)), "registry removal failed");
}

void test_localhost_discovery() {
    auto a = make_info(21, "Living Room");
    auto b = make_info(22, "Studio");
    auto c = make_info(23, "Desktop");
    DiscoveryService service_a{a, 0, 10ms, 500ms};
    DiscoveryService service_b{b, 0, 10ms, 500ms};
    DiscoveryService service_c{c, 0, 10ms, 500ms};
    const auto now = ClockTimePoint{};
    require(service_a.send(MessageType::announce, "127.0.0.1", service_b.port()), "announce A->B failed");
    require(service_b.send(MessageType::announce, "127.0.0.1", service_a.port()), "announce B->A failed");
    require(service_c.send(MessageType::announce, "127.0.0.1", service_a.port()), "announce C->A failed");
    const auto polled_a = service_a.poll(now);
    const auto polled_b = service_b.poll(now);
    const auto polled_c = service_c.poll(now);
    (void)polled_a;
    (void)polled_b;
    (void)polled_c;
    require(service_a.registry().size() == 2 && service_b.registry().size() == 1,
            "three-device discovery did not populate registries");

    auto late = make_info(24, "Late Joiner");
    DiscoveryService service_late{late, 0, 10ms, 500ms};
    require(service_late.send(MessageType::announce, "127.0.0.1", service_a.port()),
            "late join announce failed");
    const auto polled_join = service_a.poll(now + 1ms);
    (void)polled_join;
    require(service_a.registry().size() == 3, "late join was not detected");

        a.name = "Living Room Updated";
        service_a.update_local_info(a);
        require(service_a.send(MessageType::announce, "127.0.0.1", service_b.port()),
            "update announce failed");
        const auto polled_update = service_b.poll(now + 2ms);
        (void)polled_update;
        require(service_b.registry().lookup(a.id)->info.name == "Living Room Updated",
            "service did not receive updated local information");

    require(service_a.send(MessageType::query, "127.0.0.1", service_b.port()), "query failed");
    const auto polled_query_target = service_b.poll(now + 3ms);
    const auto polled_query_source = service_a.poll(now + 3ms);
    (void)polled_query_target;
    (void)polled_query_source;
        require(service_a.registry().lookup(service_b.local_info().id).has_value(),
            "query response was not received");

    require(service_late.send(MessageType::goodbye, "127.0.0.1", service_a.port()),
            "goodbye send failed");
    const auto polled_goodbye = service_a.poll(now + 4ms);
    (void)polled_goodbye;
    require(!service_a.registry().lookup(late.id).has_value(), "goodbye did not remove device");
    const auto observed_stale = service_a.registry().observe(make_info(25, "Stale"), now);
    (void)observed_stale;
    require(service_a.registry().expire(now + 501ms) >= 1, "stale service device did not expire");
    service_a.shutdown();
    require(!service_a.send(MessageType::announce, "127.0.0.1", service_b.port()),
            "shutdown service sent a packet");
}

}  // namespace

int main() {
    try {
        test_device_id_and_codec();
        test_malformed_messages();
        test_registry_and_compatibility();
        test_localhost_discovery();
    } catch (const std::exception& error) {
        std::cerr << "Discovery test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All discovery tests passed.\n";
    return 0;
}
