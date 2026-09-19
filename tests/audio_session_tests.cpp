#include "distributed_audio/audio_session.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using distributed_audio::AudioFormat;
using distributed_audio::discovery::DeviceId;
using distributed_audio::discovery::DeviceInfo;
using distributed_audio::session::AudioSessionConfig;
using distributed_audio::session::AudioSessionManager;
using distributed_audio::session::AudioSessionState;
using distributed_audio::session::SessionControlMessage;
using distributed_audio::session::SessionId;
using distributed_audio::session::SessionMessageType;
using distributed_audio::session::SessionRejectReason;
using distributed_audio::session::SessionControlService;
using distributed_audio::timing::ClockTimePoint;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

DeviceInfo make_device(std::uint64_t id, const std::string& name, std::uint16_t audio_port) {
    DeviceInfo info;
    info.id = DeviceId::from_u64(id);
    info.name = name;
    info.sample_rates = {44'100, 48'000};
    info.channel_counts = {1, 2};
    info.bits_per_sample = {16};
    info.audio_port = audio_port;
    return info;
}

AudioSessionConfig make_config(const DeviceInfo& sender, const DeviceInfo& receiver) {
    return AudioSessionConfig{AudioFormat{48'000, 2, 16}, 240, sender.id, receiver.id,
                              sender.audio_port, receiver.audio_port};
}

void test_identity_and_config() {
    const auto first = SessionId::from_u64(1);
    const auto second = SessionId::from_u64(2);
    require(first.valid() && first != second && first < second, "SessionId comparison failed");
    require(SessionId::from_string(first.to_string()) == first, "SessionId string round trip failed");
    require(!SessionId::from_string("invalid").has_value(), "invalid SessionId was accepted");
    const auto a = make_device(1, "A", 5001);
    const auto b = make_device(2, "B", 5002);
    const auto config = make_config(a, b);
    require(config.is_valid() && config.frame_duration() == 5ms, "session config validation failed");
    auto invalid = config;
    invalid.samples_per_frame = 0;
    require(!invalid.is_valid(), "invalid session config was accepted");
}

void test_control_codec() {
    const auto a = make_device(10, "A", 5001);
    const auto b = make_device(11, "B", 5002);
    const auto config = make_config(a, b);
    for (const auto type : {SessionMessageType::offer, SessionMessageType::accept}) {
        const SessionControlMessage message{type, SessionId::from_u64(7), a.id, b.id, config,
                                            SessionRejectReason::none};
        const auto packet = distributed_audio::session::serialize_session_message(message);
        require(packet[0] == 0x53 && packet[1] == 0x45 && packet[2] == 0x53 && packet[3] == 0x31,
                "session magic byte order failed");
        const auto parsed = distributed_audio::session::deserialize_session_message(packet);
        require(parsed && parsed.message->type == type &&
                    parsed.message->configuration == config,
                "session configuration round trip failed");
    }
    for (const auto type : {SessionMessageType::reject, SessionMessageType::start,
                            SessionMessageType::stop, SessionMessageType::ack}) {
        const SessionControlMessage message{type, SessionId::from_u64(8), a.id, b.id,
                                            std::nullopt,
                                            type == SessionMessageType::reject
                                                ? SessionRejectReason::busy
                                                : SessionRejectReason::none};
        const auto parsed = distributed_audio::session::deserialize_session_message(
            distributed_audio::session::serialize_session_message(message));
        require(parsed && parsed.message->type == type, "session control round trip failed");
    }
    auto malformed = distributed_audio::session::serialize_session_message(
        {SessionMessageType::start, SessionId::from_u64(9), a.id, b.id, std::nullopt,
         SessionRejectReason::none});
    malformed[0] = 0;
    require(distributed_audio::session::deserialize_session_message(malformed).error ==
                distributed_audio::session::SessionParseError::invalid_magic,
            "invalid session magic was accepted");
    malformed.resize(2);
    require(distributed_audio::session::deserialize_session_message(malformed).error ==
                distributed_audio::session::SessionParseError::truncated,
            "truncated session packet was accepted");
}

void test_manager_states_and_timeout() {
    const auto a = make_device(20, "A", 5020);
    const auto b = make_device(21, "B", 5021);
    AudioSessionManager manager_a{a, 50ms};
    const auto offer = manager_a.create_offer(b, ClockTimePoint{});
    require(offer.has_value(), "offer creation failed");
    require(!manager_a.start(offer->session_id, ClockTimePoint{}).has_value(),
            "session started before acceptance");
    require(manager_a.expire(ClockTimePoint{51ms}) == 1, "negotiation timeout failed");
    require(manager_a.lookup(offer->session_id)->state == AudioSessionState::failed,
            "timeout did not fail session");

    auto incompatible = make_device(22, "Incompatible", 5022);
    incompatible.sample_rates = {96'000};
    require(!manager_a.create_offer(incompatible, ClockTimePoint{}).has_value(),
            "incompatible offer was created");
    manager_a.device_unavailable(b.id, ClockTimePoint{});
}

void test_real_localhost_lifecycle() {
    const auto a = make_device(30, "Initiator", 5030);
    const auto b = make_device(31, "Receiver", 5031);
    AudioSessionManager manager_a{a, 100ms};
    AudioSessionManager manager_b{b, 100ms};
    SessionControlService service_a{manager_a, 0, 10ms};
    SessionControlService service_b{manager_b, 0, 10ms};

    const auto offer = manager_a.create_offer(b, ClockTimePoint{});
    require(offer.has_value(), "localhost offer creation failed");
    require(service_a.send(*offer, "127.0.0.1", service_b.port()), "offer send failed");
    const auto polled_b = service_b.poll(ClockTimePoint{});
    const auto polled_a = service_a.poll(ClockTimePoint{});
    (void)polled_b;
    (void)polled_a;
    require(manager_a.lookup(offer->session_id)->state == AudioSessionState::established,
            "initiator did not establish session");
    require(manager_b.lookup(offer->session_id)->state == AudioSessionState::established,
            "receiver did not establish session");

    const auto start = manager_a.start(offer->session_id, ClockTimePoint{1ms});
    require(start.has_value(), "session start creation failed");
    require(service_a.send(*start, "127.0.0.1", service_b.port()), "start send failed");
    const auto start_b = service_b.poll(ClockTimePoint{1ms});
    const auto start_a = service_a.poll(ClockTimePoint{1ms});
    (void)start_b;
    (void)start_a;
    require(manager_a.lookup(offer->session_id)->state == AudioSessionState::streaming &&
                manager_b.lookup(offer->session_id)->state == AudioSessionState::streaming,
            "session did not enter streaming state");

    const auto stop = manager_a.stop(offer->session_id, ClockTimePoint{2ms});
    require(stop.has_value(), "session stop creation failed");
    require(service_a.send(*stop, "127.0.0.1", service_b.port()), "stop send failed");
    const auto stop_b = service_b.poll(ClockTimePoint{2ms});
    const auto stop_a = service_a.poll(ClockTimePoint{2ms});
    (void)stop_b;
    (void)stop_a;
    require(manager_a.lookup(offer->session_id)->state == AudioSessionState::closed &&
                manager_b.lookup(offer->session_id)->state == AudioSessionState::closed,
            "session did not close cleanly");
    require(!manager_a.start(offer->session_id, ClockTimePoint{3ms}).has_value(),
            "closed session restarted");
    service_a.shutdown();
    service_b.shutdown();
}

void test_rejection_and_multiple_sessions() {
    const auto a = make_device(40, "A", 5040);
    auto b = make_device(41, "B", 5041);
    AudioSessionManager manager_b{b, 100ms};
    auto bad = make_config(a, b);
    bad.format.sample_rate = 96'000;
    const auto response = manager_b.handle(
        {SessionMessageType::offer, SessionId::from_u64(40), a.id, b.id, bad,
         SessionRejectReason::none}, ClockTimePoint{});
    require(response.has_value() && response->type == SessionMessageType::reject &&
                response->reason == SessionRejectReason::incompatible_format,
            "invalid offer was not rejected structurally");

    AudioSessionManager manager_a{a, 100ms};
    const auto first = manager_a.create_offer(b, ClockTimePoint{});
    const auto second = manager_a.create_offer(b, ClockTimePoint{1ms});
    require(first.has_value() && second.has_value() && first->session_id != second->session_id,
            "multiple sessions were not independent");
    require(manager_a.sessions().size() == 2, "session registry size was incorrect");
    manager_a.device_unavailable(b.id, ClockTimePoint{2ms});
    require(manager_a.lookup(first->session_id)->state == AudioSessionState::failed &&
                manager_a.lookup(second->session_id)->state == AudioSessionState::failed,
            "device disappearance did not affect all related sessions");
}

void test_duplicate_control_messages_and_selection() {
    auto a = make_device(50, "A", 5050);
    auto b = make_device(51, "B", 5051);
    a.sample_rates = {44'100, 48'000};
    b.sample_rates = {48'000};
    AudioSessionManager manager_a{a, 100ms};
    AudioSessionManager manager_b{b, 100ms};
    const auto offer = manager_a.create_offer(b, ClockTimePoint{});
    require(offer.has_value() && offer->configuration->format.sample_rate == 48'000,
            "session configuration selection was not deterministic");
    const auto first_accept = manager_b.handle(*offer, ClockTimePoint{});
    const auto duplicate_accept = manager_b.handle(*offer, ClockTimePoint{1ms});
    require(first_accept.has_value() && duplicate_accept.has_value() &&
                first_accept->session_id == duplicate_accept->session_id,
            "duplicate OFFER was not idempotent");
    const auto accepted = manager_a.handle(*first_accept, ClockTimePoint{});
    (void)accepted;
    const auto start = manager_a.start(offer->session_id, ClockTimePoint{});
    require(start.has_value(), "duplicate control setup start failed");
    const auto start_ack = manager_b.handle(*start, ClockTimePoint{});
    const auto duplicate_start_ack = manager_b.handle(*start, ClockTimePoint{1ms});
    require(start_ack.has_value() && duplicate_start_ack.has_value() &&
                manager_b.lookup(offer->session_id)->state == AudioSessionState::streaming,
            "duplicate START was not idempotent");
    const auto stop = manager_a.stop(offer->session_id, ClockTimePoint{});
    require(stop.has_value(), "duplicate control setup stop failed");
    const auto stop_ack = manager_b.handle(*stop, ClockTimePoint{});
    const auto duplicate_stop_ack = manager_b.handle(*stop, ClockTimePoint{1ms});
    (void)stop_ack;
    require(!duplicate_stop_ack.has_value() &&
                manager_b.lookup(offer->session_id)->state == AudioSessionState::closed,
            "duplicate STOP corrupted lifecycle state");
}

}  // namespace

int main() {
    try {
        test_identity_and_config();
        test_control_codec();
        test_manager_states_and_timeout();
        test_real_localhost_lifecycle();
        test_rejection_and_multiple_sessions();
        test_duplicate_control_messages_and_selection();
    } catch (const std::exception& error) {
        std::cerr << "Session test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All audio session tests passed.\n";
    return 0;
}
