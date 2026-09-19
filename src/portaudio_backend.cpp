#include "distributed_audio/portaudio_backend.hpp"

#if DISTRIBUTED_AUDIO_HAS_PORTAUDIO
#include <portaudio.h>

#include <memory>
#include <stdexcept>

namespace distributed_audio::audio_io {
namespace {

class PortAudioBackend final : public AudioBackend {
public:
    AudioIoResult initialize() override {
        const auto result = Pa_Initialize();
        initialized_ = result == paNoError;
        return initialized_ ? AudioIoResult{} : AudioIoResult{AudioIoError::backend_unavailable, Pa_GetErrorText(result)};
    }
    void shutdown() noexcept override {
        if (initialized_) Pa_Terminate();
        initialized_ = false;
    }
    std::vector<AudioDeviceInfo> enumerate() const override {
        std::vector<AudioDeviceInfo> devices;
        if (!initialized_) return devices;
        const auto count = Pa_GetDeviceCount();
        for (int index = 0; index < count; ++index) {
            const auto* info = Pa_GetDeviceInfo(index);
            if (info == nullptr) continue;
            devices.push_back(AudioDeviceInfo{std::to_string(index), info->name,
                                              static_cast<std::uint16_t>(info->maxInputChannels),
                                              static_cast<std::uint16_t>(info->maxOutputChannels),
                                              {44'100, 48'000},
                                              index == Pa_GetDefaultInputDevice(),
                                              index == Pa_GetDefaultOutputDevice()});
        }
        return devices;
    }
    std::optional<AudioDeviceInfo> default_input() const override {
        return find(Pa_GetDefaultInputDevice(), true);
    }
    std::optional<AudioDeviceInfo> default_output() const override {
        return find(Pa_GetDefaultOutputDevice(), false);
    }
    std::unique_ptr<AudioInputDevice> create_input(const std::string&) override { return nullptr; }
    std::unique_ptr<AudioOutputDevice> create_output(const std::string&) override { return nullptr; }

private:
    std::optional<AudioDeviceInfo> find(PaDeviceIndex index, bool input) const {
        if (!initialized_ || index < 0) return std::nullopt;
        const auto* info = Pa_GetDeviceInfo(index);
        if (info == nullptr) return std::nullopt;
        return AudioDeviceInfo{std::to_string(index), info->name,
                               static_cast<std::uint16_t>(info->maxInputChannels),
                               static_cast<std::uint16_t>(info->maxOutputChannels),
                               {44'100, 48'000}, input, !input};
    }
    bool initialized_{false};
};

}  // namespace

std::unique_ptr<AudioBackend> create_portaudio_backend() {
    return std::make_unique<PortAudioBackend>();
}

}  // namespace distributed_audio::audio_io
#else
namespace distributed_audio::audio_io {
std::unique_ptr<AudioBackend> create_portaudio_backend() { return nullptr; }
}
#endif
