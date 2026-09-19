# Audio I/O Abstraction

Phase 10 adds an audio-device boundary without coupling the distributed core to PortAudio or another device library. The existing synthetic and simulated paths remain the deterministic default for CI and WSL environments.

## Architecture

```text
SyntheticAudioSource OR physical AudioInputDevice
        |
        v
 bounded capture handoff
        |
        v
 DSP / AudioFrame / session media sender
        |
        v
 UDP receiver -> jitter buffer -> scheduler
        |
        v
 bounded playback handoff
        |
        +--> simulated/test AudioOutputDevice
        |
        +--> optional physical audio callback
```

`AudioBackend` owns initialization, shutdown, enumeration, defaults, and device creation. `AudioInputDevice` and `AudioOutputDevice` expose lifecycle operations without exposing PortAudio types. Device capability checks require a valid `AudioFormat`, a supported sample rate, sufficient channels, and the current PCM16 representation.

## Mock Backend

`MockAudioBackend` provides deterministic input and output devices named `mock-input` and `mock-output`. The mock input emits valid silent PCM16 `AudioFrame` objects with monotonically increasing sequence numbers. The mock output validates frame formats and accepts frames without hardware. This backend is used by tests and makes lifecycle, format rejection, capture, and playback behavior testable on WSL.

`CaptureBuffer` and `PlaybackBuffer` are bounded mutex-protected handoff queues. Capture overflow increments `dropped_frames`; playback empty reads increment `underruns` and represent silence at the callback boundary. Both buffers support deterministic close and clear their pending work.

## Optional PortAudio

CMake searches for `portaudio.h` and a PortAudio library with `find_path`/`find_library`. When found, `DISTRIBUTED_AUDIO_HAS_PORTAUDIO=1` is exported and the optional backend can initialize PortAudio and enumerate its devices. When unavailable, the project still builds with `DISTRIBUTED_AUDIO_HAS_PORTAUDIO=0`; no package installation or fake hardware result is attempted.

The current WSL environment did not provide PortAudio discovery tools or headers/libraries, so physical input/output was not exercised. The optional implementation intentionally reports no physical stream objects yet; device enumeration support is isolated behind the backend boundary for a future environment with the dependency available.

## Callback and Thread Boundaries

An eventual physical callback must only move bounded audio data through the capture or playback handoff. It must not perform UDP I/O, wait on long-held locks, sleep, print, allocate unpredictably, or throw. Network, DSP setup, session control, and device management remain outside the callback.

The current mock buffers use mutexes and standard containers for clarity and testing. They are not hard-real-time safe. A production callback would need preallocated storage, a bounded lock-free or carefully bounded queue, allocation-free conversion, and an explicit non-throwing error policy.

## CLI and Troubleshooting

```text
./build/distributed_audio_system --list-devices
./build/distributed_audio_system --synthetic
./build/distributed_audio_system --input-device mock-input --output-device mock-output
```

`--list-devices` reports the active backend's actual enumeration. Invalid device IDs fail instead of silently falling back. The existing demo remains synthetic/session-driven by default; selecting a mock device validates the requested IDs but does not claim physical audio.

On WSL, no usable physical audio device may be exposed even when a backend library is installed. In that case use `--synthetic` or the mock backend tests. No physical playback, microphone capture, or hard-real-time guarantee is claimed by this phase.