#include "distributed_audio/audio_io.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace distributed_audio::audio_io {
namespace {

class MockInput final : public AudioInputDevice {
public:
    explicit MockInput(AudioDeviceInfo info) : info_(std::move(info)) {}

    AudioIoResult open(const AudioFormat& format) override {
        if (!supports_format(info_, format)) return {AudioIoError::unsupported_format, "mock input does not support format"};
        format_ = format;
        opened_ = true;
        return {};
    }
    AudioIoResult start() override {
        if (!opened_) return {AudioIoError::device_open_failed, "mock input is not open"};
        running_ = true;
        return {};
    }
    std::optional<AudioFrame> capture() override {
        if (!running_) return std::nullopt;
        AudioFrame::Payload payload(static_cast<std::size_t>(format_.channel_count) * 2U * 240U, 0);
        return AudioFrame{format_, sequence_, AudioFrame::Timestamp{static_cast<std::int64_t>(sequence_) * 5'000'000}, std::move(payload)};
    }
    AudioIoResult stop() noexcept override {
        running_ = false;
        opened_ = false;
        return {};
    }
    const AudioDeviceInfo& info() const noexcept override { return info_; }

private:
    AudioDeviceInfo info_;
    AudioFormat format_{};
    std::uint64_t sequence_{0};
    bool opened_{false};
    bool running_{false};
};

class MockOutput final : public AudioOutputDevice {
public:
    explicit MockOutput(AudioDeviceInfo info) : info_(std::move(info)) {}

    AudioIoResult open(const AudioFormat& format) override {
        if (!supports_format(info_, format)) return {AudioIoError::unsupported_format, "mock output does not support format"};
        format_ = format;
        opened_ = true;
        return {};
    }
    AudioIoResult start() override {
        if (!opened_) return {AudioIoError::device_open_failed, "mock output is not open"};
        running_ = true;
        return {};
    }
    AudioIoResult write(const AudioFrame& frame) override {
        if (!running_) return {AudioIoError::stream_stopped, "mock output is stopped"};
        if (frame.format() != format_) return {AudioIoError::unsupported_format, "output frame format mismatch"};
        ++played_frames_;
        return {};
    }
    AudioIoResult stop() noexcept override {
        running_ = false;
        opened_ = false;
        return {};
    }
    const AudioDeviceInfo& info() const noexcept override { return info_; }

private:
    AudioDeviceInfo info_;
    AudioFormat format_{};
    std::uint64_t played_frames_{0};
    bool opened_{false};
    bool running_{false};
};

}  // namespace

bool supports_format(const AudioDeviceInfo& device, const AudioFormat& format) noexcept {
    return format.is_valid() && format.channel_count <= std::max(device.input_channels, device.output_channels) &&
           std::find(device.sample_rates.begin(), device.sample_rates.end(), format.sample_rate) != device.sample_rates.end() &&
           format.bits_per_sample == 16;
}

CaptureBuffer::CaptureBuffer(std::size_t capacity) : capacity_(capacity) {
    if (capacity_ == 0) throw std::invalid_argument("capture capacity must be greater than zero");
}

bool CaptureBuffer::push(AudioFrame frame) {
    std::lock_guard lock(mutex_);
    if (closed_ || frames_.size() >= capacity_) {
        ++statistics_.dropped_frames;
        return false;
    }
    statistics_.captured_samples += frame.payload().size() /
        (static_cast<std::size_t>(frame.format().channel_count) * (frame.format().bits_per_sample / 8));
    ++statistics_.captured_frames;
    frames_.push_back(std::move(frame));
    return true;
}

std::optional<AudioFrame> CaptureBuffer::pop() {
    std::lock_guard lock(mutex_);
    if (frames_.empty()) return std::nullopt;
    auto frame = std::move(frames_.front());
    frames_.pop_front();
    return frame;
}
void CaptureBuffer::close() noexcept { std::lock_guard lock(mutex_); closed_ = true; frames_.clear(); }
std::size_t CaptureBuffer::size() const { std::lock_guard lock(mutex_); return frames_.size(); }
const CaptureStatistics& CaptureBuffer::statistics() const noexcept { return statistics_; }

PlaybackBuffer::PlaybackBuffer(std::size_t capacity) : capacity_(capacity) {
    if (capacity_ == 0) throw std::invalid_argument("playback capacity must be greater than zero");
}

bool PlaybackBuffer::push(AudioFrame frame) {
    std::lock_guard lock(mutex_);
    if (closed_ || frames_.size() >= capacity_) return false;
    frames_.push_back(std::move(frame));
    return true;
}
std::optional<AudioFrame> PlaybackBuffer::pop() {
    std::lock_guard lock(mutex_);
    if (frames_.empty()) { ++statistics_.underruns; return std::nullopt; }
    auto frame = std::move(frames_.front());
    frames_.pop_front();
    ++statistics_.played_frames;
    statistics_.played_samples += frame.payload().size() /
        (static_cast<std::size_t>(frame.format().channel_count) * (frame.format().bits_per_sample / 8));
    return frame;
}
void PlaybackBuffer::close() noexcept { std::lock_guard lock(mutex_); closed_ = true; frames_.clear(); }
std::size_t PlaybackBuffer::size() const { std::lock_guard lock(mutex_); return frames_.size(); }
const PlaybackStatistics& PlaybackBuffer::statistics() const noexcept { return statistics_; }

MockAudioBackend::MockAudioBackend()
    : devices_{{"mock-input", "Deterministic Mock Input", 2, 0, {44'100, 48'000}, true, false},
               {"mock-output", "Deterministic Mock Output", 0, 2, {44'100, 48'000}, false, true}} {}

AudioIoResult MockAudioBackend::initialize() { initialized_ = true; return {}; }
void MockAudioBackend::shutdown() noexcept { initialized_ = false; }
std::vector<AudioDeviceInfo> MockAudioBackend::enumerate() const { return initialized_ ? devices_ : std::vector<AudioDeviceInfo>{}; }
std::optional<AudioDeviceInfo> MockAudioBackend::default_input() const { return initialized_ ? std::optional<AudioDeviceInfo>{devices_[0]} : std::nullopt; }
std::optional<AudioDeviceInfo> MockAudioBackend::default_output() const { return initialized_ ? std::optional<AudioDeviceInfo>{devices_[1]} : std::nullopt; }

std::unique_ptr<AudioInputDevice> MockAudioBackend::create_input(const std::string& id) {
    if (!initialized_ || id != devices_[0].id) return nullptr;
    return std::make_unique<MockInput>(devices_[0]);
}
std::unique_ptr<AudioOutputDevice> MockAudioBackend::create_output(const std::string& id) {
    if (!initialized_ || id != devices_[1].id) return nullptr;
    return std::make_unique<MockOutput>(devices_[1]);
}

std::unique_ptr<AudioBackend> create_default_backend() { return std::make_unique<MockAudioBackend>(); }
bool portaudio_backend_available() noexcept {
#if DISTRIBUTED_AUDIO_HAS_PORTAUDIO
    return DISTRIBUTED_AUDIO_HAS_PORTAUDIO != 0;
#else
    return false;
#endif
}

std::string audio_io_error_message(AudioIoError error) {
    switch (error) {
    case AudioIoError::none: return "none";
    case AudioIoError::backend_unavailable: return "audio backend unavailable";
    case AudioIoError::no_device: return "no audio device";
    case AudioIoError::invalid_device: return "invalid device";
    case AudioIoError::unsupported_format: return "unsupported audio format";
    case AudioIoError::device_open_failed: return "device open failed";
    case AudioIoError::stream_start_failed: return "stream start failed";
    case AudioIoError::stream_stopped: return "stream stopped";
    }
    return "unknown audio I/O error";
}

}  // namespace distributed_audio::audio_io
