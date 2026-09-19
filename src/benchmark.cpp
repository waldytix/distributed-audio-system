#include "distributed_audio/benchmark.hpp"

#include "distributed_audio/audio_frame.hpp"
#include "distributed_audio/gain_processor.hpp"
#include "distributed_audio/packet_serializer.hpp"
#include "distributed_audio/pcm_conversion.hpp"
#include "distributed_audio/timing_utils.hpp"

#include <chrono>
#include <iostream>
#include <utility>
#include <vector>

namespace distributed_audio::benchmark {
namespace {

using Clock = std::chrono::steady_clock;

AudioFrame make_frame() {
    return AudioFrame{AudioFormat{48'000, 2, 16}, 1, {}, AudioFrame::Payload(960, 7)};
}

template <typename Function>
Result measure(const char* name, std::size_t iterations, Function function) {
    const auto start = Clock::now();
    for (std::size_t index = 0; index < iterations; ++index) function();
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    const auto average = elapsed * 1000.0 / static_cast<double>(iterations);
    return {name, iterations, elapsed, average, 1000.0 / average};
}

}  // namespace

std::vector<Result> run(std::size_t iterations) {
    const auto frame = make_frame();
    const auto packet = network::serialize_audio_frame(frame);
    NormalizedSamples samples(960, 0.25F);
    GainProcessor gain{0.5F};
    return {
        measure("PCM16 -> float", iterations, [&] { static_cast<void>(pcm16_to_float(frame)); }),
        measure("float -> PCM16", iterations, [&] { static_cast<void>(float_to_pcm16(samples, frame.format())); }),
        measure("gain processing", iterations, [&] { auto copy = samples; gain.process(copy); }),
        measure("packet serialization", iterations, [&] { static_cast<void>(network::serialize_audio_frame(frame)); }),
        measure("packet deserialization", iterations, [&] { static_cast<void>(network::deserialize_audio_frame(packet)); }),
    };
}

void print(const std::vector<Result>& results) {
    std::cout << "Benchmark measurements depend on hardware, build mode, and system load.\n";
    for (const auto& result : results) {
        std::cout << result.operation << ": iterations=" << result.iterations
                  << ", total_ms=" << result.total_milliseconds
                  << ", average_us=" << result.average_microseconds
                  << ", throughput/s=" << result.throughput_per_second << '\n';
    }
}

}  // namespace distributed_audio::benchmark
