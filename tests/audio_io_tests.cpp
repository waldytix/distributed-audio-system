#include "distributed_audio/audio_io.hpp"
#include "distributed_audio/portaudio_backend.hpp"

#include <iostream>
#include <stdexcept>

namespace {

using distributed_audio::AudioFormat;
using namespace distributed_audio::audio_io;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_mock_backend() {
    MockAudioBackend backend;
    require(backend.enumerate().empty(), "uninitialized mock backend enumerated devices");
    require(static_cast<bool>(backend.initialize()), "mock backend initialization failed");
    const auto devices = backend.enumerate();
    require(devices.size() == 2, "mock device enumeration failed");
    require(backend.default_input()->id == "mock-input" && backend.default_output()->id == "mock-output",
            "mock defaults were incorrect");
    auto input = backend.create_input("mock-input");
    auto output = backend.create_output("mock-output");
    const AudioFormat format{48'000, 2, 16};
    require(input->open(format) && input->start(), "mock input lifecycle failed");
    const auto frame = input->capture();
    require(frame.has_value() && frame->format() == format, "mock capture failed");
    require(output->open(format) && output->start() && output->write(*frame),
            "mock output lifecycle failed");
    require(output->stop() && input->stop(), "mock stop failed");
    backend.shutdown();
}

void test_bounded_buffers() {
    CaptureBuffer capture{1};
    PlaybackBuffer playback{1};
    const AudioFormat format{48'000, 2, 16};
    const auto make_frame = [](std::uint64_t sequence, AudioFormat value) {
        return distributed_audio::AudioFrame{value, sequence, {},
                                             distributed_audio::AudioFrame::Payload(960, 0)};
    };
    require(capture.push(make_frame(1, format)), "capture push failed");
    require(!capture.push(make_frame(2, format)), "capture overflow was not reported");
    require(capture.statistics().dropped_frames == 1, "capture overflow statistic failed");
    require(!playback.pop().has_value() && playback.statistics().underruns == 1,
            "playback underrun was not reported");
    require(playback.push(make_frame(1, format)), "playback push failed");
    require(playback.pop().has_value() && playback.statistics().played_samples == 240,
            "playback buffer statistics failed");
    capture.close();
    playback.close();
}

void test_format_rejection() {
    AudioDeviceInfo device{"mock", "Mock", 2, 2, {48'000}, false, false};
    require(supports_format(device, AudioFormat{48'000, 2, 16}), "supported format rejected");
    require(!supports_format(device, AudioFormat{44'100, 2, 16}), "unsupported rate accepted");
    require(!portaudio_backend_available(), "PortAudio unexpectedly reported available in this environment");
}

}  // namespace

int main() {
    try {
        test_mock_backend();
        test_bounded_buffers();
        test_format_rejection();
    } catch (const std::exception& error) {
        std::cerr << "Audio I/O test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All audio I/O tests passed.\n";
    return 0;
}
