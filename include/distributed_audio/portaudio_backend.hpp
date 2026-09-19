#pragma once

#include "distributed_audio/audio_io.hpp"

namespace distributed_audio::audio_io {

[[nodiscard]] std::unique_ptr<AudioBackend> create_portaudio_backend();

}  // namespace distributed_audio::audio_io
