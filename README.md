# Distributed Audio System

Distributed Audio System is a professional C++ project that will evolve into a distributed network audio system. It is written in modern C++17 with a focus on embedded and software engineering fundamentals.

## Planned Features

- Network audio streaming
- Virtual speaker nodes
- Audio synchronization across nodes
- Buffering and latency management
- Concurrent processing
- System diagnostics
- Automated testing

## Current Status

**Phase 7: Device Discovery and Node Management**

Phase 1 established the project structure and build system. Phase 2 added the foundational in-memory audio data model and producer/consumer buffer. Phase 3 added laptop-based PCM conversion and a small DSP layer. Phase 4 added localhost UDP transport for validated PCM16 `AudioFrame` packets. Phase 5 added receiver-side jitter buffering, deterministic packet impairment simulation, monotonic clock estimation, and simulated playback scheduling. Phase 6 added three independent simulated playback endpoints and bounded synchronization control. Phase 7 adds explicit device discovery, a thread-safe node registry, liveness tracking, capability compatibility, and real localhost discovery traffic. Physical playback, authentication, encryption, session management, and production-grade distributed synchronization remain future work.

## Phase 2 Architecture

### AudioFormat

`AudioFormat` describes PCM data using a sample rate, channel count, and bits per sample. It validates basic format constraints without limiting the project to one sample rate or channel layout.

### AudioFrame

`AudioFrame` owns one block of interleaved PCM payload bytes together with its format, monotonically increasing sequence number, and `std::chrono` presentation timestamp. Construction rejects payloads that do not contain complete sample frames.

### AudioBuffer

`AudioBuffer` is a bounded, thread-safe FIFO of `AudioFrame` objects. It uses mutexes and condition variables to coordinate producers and consumers, applies backpressure when full, and supports clean shutdown through `close()` while allowing queued frames to drain.

See [docs/architecture.md](docs/architecture.md) for the current data flow and design rationale.

## Phase 3 DSP Capabilities

- Signed little-endian 16-bit PCM to normalized floating-point conversion and back
- Linear and decibel gain with bypass support
- Hard saturation to the normalized range `[-1.0, 1.0]`
- Mono-to-stereo duplication and stereo-to-mono averaging
- Deterministic compatible-stream mixing
- Per-channel and overall absolute peak metering
- A small pipeline that performs PCM conversion, channel conversion, gain, and metering

The demo generates a short in-memory 440 Hz stereo sine wave, sends it through the bounded buffer, processes it as mono at -6 dB, and prints the measured peak. It does not require audio hardware.

## Phase 4 Network Transport

- Explicit 40-byte big-endian packet header and PCM16 payload serialization
- Checked packet parsing with centralized size and format limits
- RAII POSIX UDP sender and receiver for configurable IPv4 endpoints
- Sequence tracking for duplicates, gaps, out-of-order packets, and wraparound
- Deterministic malformed-packet and localhost loopback tests

The demo also sends ten real 5 ms stereo PCM16 frames over `127.0.0.1` and reports runtime receive statistics. See [docs/network_protocol.md](docs/network_protocol.md) for the exact wire format and transport limitations.

## Phase 5 Receiver Timing

- Bounded reorder buffer with configurable prebuffering and missing-frame deadlines
- Modular sequence handling across `uint64_t` rollover
- Duplicate, late, incompatible, and overflow rejection statistics
- Silence concealment by default, with repeat-previous support
- Monotonic-clock jitter estimation and linear remote/local clock regression
- Configurable playout delay and deterministic playback decisions
- Deterministic impairment simulation and simulated playback sink

The demo retains the real UDP loopback exchange and adds a separate deterministic timing scenario with reordering, duplication, and one lost frame. See [docs/synchronization.md](docs/synchronization.md) for the receiver timing model and its limitations.

## Phase 6 Multi-Device Synchronization

- Three reusable independent playback endpoints
- Reference timeline separated from endpoint clock models
- Per-endpoint initial offset and configured drift
- Bounded proportional synchronization corrections
- Configurable tolerance, correction limit, convergence rate, and update interval
- Per-endpoint synchronization metrics and inter-device skew measurement
- Shared deterministic stream with endpoint-specific impairment conditions

The demo reports measured initial/final skew, endpoint errors, corrections, states, drift, and concealment. It demonstrates gradual convergence in simulation; it does not claim sample-accurate synchronization or production-grade distributed clock discipline.

## Phase 7 Device Discovery

- Stable 128-bit `DeviceId` values with text conversion and ordering
- `DeviceInfo` capability and port advertisements
- Explicit binary ANNOUNCE, QUERY, RESPONSE, and GOODBYE messages
- Big-endian header and capability fields with bounded validation
- Thread-safe registry updates, lookup, removal, and deterministic stale expiration
- Sample-rate, channel-count, and PCM-width compatibility results
- Real localhost discovery among three services, including join, update, goodbye, and expiration

See [docs/device_discovery.md](docs/device_discovery.md) for the protocol and registry model.

## Testing

The project uses small internal test executables built from standard C++17 facilities. CTest covers Phase 2 buffer behavior, Phase 3 DSP behavior, Phase 4 packet/UDP behavior, Phase 5 jitter, clock, scheduler, impairment, concealment, and simulated playback behavior, Phase 6 multi-endpoint convergence, and Phase 7 discovery, registry, liveness, compatibility, and localhost service behavior.

## Build and Run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/distributed_audio_system
ctest --test-dir build --output-on-failure
```

A Release build can be configured with `-DCMAKE_BUILD_TYPE=Release`. No external libraries are required.
