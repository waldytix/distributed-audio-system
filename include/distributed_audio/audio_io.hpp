#pragma once

#include "distributed_audio/audio_frame.hpp"
#include "distributed_audio/clock.hpp"
#include "distributed_audio/pcm_conversion.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace distributed_audio::audio_io {

enum class AudioIoError {
    none,
    backend_unavailable,
    no_device,
    invalid_device,
    unsupported_format,
    device_open_failed,
    stream_start_failed,
    stream_stopped
};

struct AudioDeviceInfo {
    std::string id;
    std::string name;
    std::uint16_t input_channels{0};
    std::uint16_t output_channels{0};
    std::vector<std::uint32_t> sample_rates;
    bool default_input{false};
    bool default_output{false};
};

struct AudioIoResult {
    AudioIoError error{AudioIoError::none};
    std::string message;
    [[nodiscard]] explicit operator bool() const noexcept { return error == AudioIoError::none; }
};

[[nodiscard]] bool supports_format(const AudioDeviceInfo& device,
                                   const AudioFormat& format) noexcept;

class AudioInputDevice {
public:
    virtual ~AudioInputDevice() = default;
    [[nodiscard]] virtual AudioIoResult open(const AudioFormat& format) = 0;
    [[nodiscard]] virtual AudioIoResult start() = 0;
    [[nodiscard]] virtual std::optional<AudioFrame> capture() = 0;
    [[nodiscard]] virtual AudioIoResult stop() noexcept = 0;
    [[nodiscard]] virtual const AudioDeviceInfo& info() const noexcept = 0;
};

class AudioOutputDevice {
public:
    virtual ~AudioOutputDevice() = default;
    [[nodiscard]] virtual AudioIoResult open(const AudioFormat& format) = 0;
    [[nodiscard]] virtual AudioIoResult start() = 0;
    [[nodiscard]] virtual AudioIoResult write(const AudioFrame& frame) = 0;
    [[nodiscard]] virtual AudioIoResult stop() noexcept = 0;
    [[nodiscard]] virtual const AudioDeviceInfo& info() const noexcept = 0;
};

struct CaptureStatistics {
    std::uint64_t captured_frames{0};
    std::uint64_t dropped_frames{0};
    std::uint64_t captured_samples{0};
};

class CaptureBuffer {
public:
    explicit CaptureBuffer(std::size_t capacity);
    [[nodiscard]] bool push(AudioFrame frame);
    [[nodiscard]] std::optional<AudioFrame> pop();
    void close() noexcept;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] const CaptureStatistics& statistics() const noexcept;

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<AudioFrame> frames_;
    CaptureStatistics statistics_;
    bool closed_{false};
};

struct PlaybackStatistics {
    std::uint64_t played_frames{0};
    std::uint64_t played_samples{0};
    std::uint64_t underruns{0};
};

class PlaybackBuffer {
public:
    explicit PlaybackBuffer(std::size_t capacity);
    [[nodiscard]] bool push(AudioFrame frame);
    [[nodiscard]] std::optional<AudioFrame> pop();
    void close() noexcept;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] const PlaybackStatistics& statistics() const noexcept;

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<AudioFrame> frames_;
    PlaybackStatistics statistics_;
    bool closed_{false};
};

class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    [[nodiscard]] virtual AudioIoResult initialize() = 0;
    virtual void shutdown() noexcept = 0;
    [[nodiscard]] virtual std::vector<AudioDeviceInfo> enumerate() const = 0;
    [[nodiscard]] virtual std::optional<AudioDeviceInfo> default_input() const = 0;
    [[nodiscard]] virtual std::optional<AudioDeviceInfo> default_output() const = 0;
    [[nodiscard]] virtual std::unique_ptr<AudioInputDevice> create_input(
        const std::string& id) = 0;
    [[nodiscard]] virtual std::unique_ptr<AudioOutputDevice> create_output(
        const std::string& id) = 0;
};

class MockAudioBackend final : public AudioBackend {
public:
    MockAudioBackend();
    [[nodiscard]] AudioIoResult initialize() override;
    void shutdown() noexcept override;
    [[nodiscard]] std::vector<AudioDeviceInfo> enumerate() const override;
    [[nodiscard]] std::optional<AudioDeviceInfo> default_input() const override;
    [[nodiscard]] std::optional<AudioDeviceInfo> default_output() const override;
    [[nodiscard]] std::unique_ptr<AudioInputDevice> create_input(const std::string& id) override;
    [[nodiscard]] std::unique_ptr<AudioOutputDevice> create_output(const std::string& id) override;

private:
    std::vector<AudioDeviceInfo> devices_;
    bool initialized_{false};
};

[[nodiscard]] std::unique_ptr<AudioBackend> create_default_backend();
[[nodiscard]] bool portaudio_backend_available() noexcept;
[[nodiscard]] std::string audio_io_error_message(AudioIoError error);

}  // namespace distributed_audio::audio_io
