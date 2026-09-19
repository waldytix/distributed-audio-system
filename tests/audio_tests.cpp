#include "distributed_audio/audio_buffer.hpp"
#include "distributed_audio/audio_frame.hpp"
#include "distributed_audio/audio_format.hpp"

#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using distributed_audio::AudioBuffer;
using distributed_audio::AudioFormat;
using distributed_audio::AudioFrame;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

AudioFrame make_frame(std::uint64_t sequence_number) {
    return AudioFrame{
        AudioFormat{},
        sequence_number,
        AudioFrame::Timestamp{sequence_number * 1'000},
        AudioFrame::Payload{0, 1, 2, 3}};
}

void test_audio_format() {
    const AudioFormat valid_format{};
    require(valid_format.is_valid(), "default audio format should be valid");
    require(valid_format == AudioFormat{44'100, 2, 16}, "format equality failed");

    require(!AudioFormat{0, 2, 16}.is_valid(), "zero sample rate should be invalid");
    require(!AudioFormat{44'100, 0, 16}.is_valid(), "zero channels should be invalid");
    require(!AudioFormat{44'100, 2, 0}.is_valid(), "zero bits per sample should be invalid");
    require(!AudioFormat{44'100, 2, 12}.is_valid(),
            "non-byte-aligned bits per sample should be invalid");
}

void test_audio_frame() {
    const AudioFormat format{48'000, 2, 16};
    const auto timestamp = AudioFrame::Timestamp{123'456};
    AudioFrame::Payload payload{10, 20, 30, 40, 50, 60, 70, 80};
    const AudioFrame frame{format, 42, timestamp, payload};

    require(frame.format() == format, "frame format was not preserved");
    require(frame.sequence_number() == 42, "sequence number was not preserved");
    require(frame.timestamp() == timestamp, "timestamp was not preserved");
    require(frame.payload() == payload, "payload was not preserved");

    bool rejected_unaligned_payload = false;
    try {
        AudioFrame{format, 1, timestamp, AudioFrame::Payload{1, 2, 3}};
    } catch (const std::invalid_argument&) {
        rejected_unaligned_payload = true;
    }
    require(rejected_unaligned_payload, "unaligned payload should be rejected");
}

void test_audio_buffer_basic_behavior() {
    bool rejected_zero_capacity = false;
    try {
        AudioBuffer{0};
    } catch (const std::invalid_argument&) {
        rejected_zero_capacity = true;
    }
    require(rejected_zero_capacity, "zero capacity should be rejected");

    AudioBuffer buffer{2};
    require(buffer.capacity() == 2, "capacity was not preserved");
    require(buffer.empty(), "new buffer should be empty");
    require(!buffer.full(), "new buffer should not be full");
    require(buffer.try_push(make_frame(1)), "first try_push should succeed");
    require(buffer.push(make_frame(2)), "second push should succeed");
    require(buffer.full(), "buffer should be full");
    require(!buffer.try_push(make_frame(3)), "try_push should fail when full");

    const auto first = buffer.try_pop();
    const auto second = buffer.try_pop();
    require(first.has_value() && first->sequence_number() == 1,
            "FIFO order was not preserved for first frame");
    require(second.has_value() && second->sequence_number() == 2,
            "FIFO order was not preserved for second frame");
    require(!buffer.try_pop().has_value(), "try_pop should fail when empty");
}

void test_audio_buffer_close_behavior() {
    AudioBuffer buffer{2};
    require(buffer.push(make_frame(1)), "first push should succeed");
    require(buffer.push(make_frame(2)), "second push should succeed");
    buffer.close();
    buffer.close();

    require(buffer.closed(), "buffer should be closed");
    require(buffer.pop()->sequence_number() == 1, "closed buffer did not drain first frame");
    require(buffer.pop()->sequence_number() == 2, "closed buffer did not drain second frame");
    require(!buffer.pop().has_value(), "closed and empty buffer should return no frame");
    require(!buffer.try_push(make_frame(3)), "closed buffer accepted a frame");
}

void test_producer_blocks_and_wakes() {
    AudioBuffer buffer{1};
    require(buffer.push(make_frame(1)), "first push should succeed");
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    auto producer = std::async(std::launch::async, [&buffer, &entered] {
        entered.set_value();
        return buffer.push(make_frame(2));
    });

    require(entered_future.wait_for(1s) == std::future_status::ready,
            "producer did not start deterministically");
    require(producer.wait_for(20ms) == std::future_status::timeout,
            "producer should block while buffer is full");
    require(buffer.pop()->sequence_number() == 1, "unexpected frame while waking producer");
    require(producer.get(), "producer did not wake after space became available");
    require(buffer.pop()->sequence_number() == 2, "woken producer frame was not queued");
}

void test_consumer_blocks_and_wakes() {
    AudioBuffer buffer{1};
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    auto consumer = std::async(std::launch::async, [&buffer, &entered] {
        entered.set_value();
        return buffer.pop();
    });

    require(entered_future.wait_for(1s) == std::future_status::ready,
            "consumer did not start deterministically");
    require(consumer.wait_for(20ms) == std::future_status::timeout,
            "consumer should block while buffer is empty");
    require(buffer.push(make_frame(7)), "push should wake consumer");
    const auto frame = consumer.get();
    require(frame.has_value() && frame->sequence_number() == 7,
            "consumer did not wake after a frame became available");
}

void test_close_wakes_waiters() {
    {
        AudioBuffer buffer{1};
        require(buffer.push(make_frame(1)), "initial push should succeed");
        std::promise<void> entered;
        auto entered_future = entered.get_future();
        auto producer = std::async(std::launch::async, [&buffer, &entered] {
            entered.set_value();
            return buffer.push(make_frame(2));
        });
        require(entered_future.wait_for(1s) == std::future_status::ready,
                "blocked producer did not start deterministically");
        buffer.close();
        require(!producer.get(), "closed buffer did not reject blocked producer");
    }

    {
        AudioBuffer buffer{1};
        std::promise<void> entered;
        auto entered_future = entered.get_future();
        auto consumer = std::async(std::launch::async, [&buffer, &entered] {
            entered.set_value();
            return buffer.pop();
        });
        require(entered_future.wait_for(1s) == std::future_status::ready,
                "blocked consumer did not start deterministically");
        buffer.close();
        require(!consumer.get().has_value(), "closed empty buffer returned a frame");
    }
}

}  // namespace

int main() {
    try {
        test_audio_format();
        test_audio_frame();
        test_audio_buffer_basic_behavior();
        test_audio_buffer_close_behavior();
        test_producer_blocks_and_wakes();
        test_consumer_blocks_and_wakes();
        test_close_wakes_waiters();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All audio architecture tests passed.\n";
    return 0;
}
