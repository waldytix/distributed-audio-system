# Testing

The project uses standard C++17 test executables registered with CTest. Tests are hardware-independent and use synthetic frames, mock audio devices, localhost UDP, manual clocks, and deterministic impairment plans.

## Build and Test

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure
```

Release verification uses the same commands with `-DCMAKE_BUILD_TYPE=Release` and a separate build directory.

## Test Organization

- `audio_tests`: Phase 2 data model and bounded buffer.
- `dsp_tests`: PCM conversion, gain, mixing, metering, and pipeline.
- `network_tests`: packet protocol, malformed input, sequence tracking, UDP loopback.
- `timing_tests`: jitter buffer, clocks, scheduler, impairment, simulated playback.
- `multi_device_tests`: independent endpoint synchronization.
- `discovery_tests`: device identity, protocol, registry, liveness, localhost discovery.
- `audio_session_tests`: control protocol, negotiation, lifecycle, timeout, and recovery boundaries.
- `audio_stream_tests`: real media UDP, session association, PCM checksum, DSP path, and playback.
- `audio_io_tests`: deterministic mock backend, buffers, formats, and device lifecycle.
- `resilience_tests`: statistics, adaptive limits, loss, liveness, and bounded stress.
- `final_integration_tests`: larger complete resilience-path integration.

## Optional Sanitizers

ASan and UBSan switches are available but disabled by default:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DDISTRIBUTED_AUDIO_ENABLE_ASAN=ON \
  -DDISTRIBUTED_AUDIO_ENABLE_UBSAN=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

They depend on the installed compiler/runtime and are not required for normal builds.

## Deterministic Strategy

Tests do not require Internet access, physical audio devices, arbitrary long sleeps, or multiple machines. Manual timestamps drive timing decisions; network impairment is an explicit plan; the mock audio backend supplies deterministic capture/output behavior.
