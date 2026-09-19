# Performance

The project includes a lightweight standard-library benchmark mode:

```bash
./build-phase12-debug/distributed_audio_system --benchmark
```

It measures PCM16/float conversion, gain processing, packet serialization, and packet deserialization. Each result reports iterations, total milliseconds, average microseconds, and approximate operations per second.

Results depend on CPU, compiler, build type, operating-system scheduling, and current system load. They are engineering observations, not pass/fail thresholds or universal claims. The benchmark intentionally avoids a third-party framework.

## Latency and Bounds

The architecture keeps major queues bounded: Phase 2 audio buffers are capacity-configured, Phase 4 packets have explicit maximum sizes, Phase 5 jitter buffers have fixed capacity and concealment deadlines, Phase 7 registries have bounded packet/string/capability inputs, Phase 8 control packets are capped, and Phase 11 recovery attempts are finite. Capture/playback buffers are also capacity-configured.

Measured simulated latency should be interpreted as a model of processing, transit, jitter-buffer, and scheduling behavior. It is not physical microphone-to-speaker latency. Physical PortAudio was unavailable in the WSL environment used for verification.

The current implementation still allocates in several processing and networking paths and uses mutex-protected snapshots and standard containers. It is not hard-real-time safe.
