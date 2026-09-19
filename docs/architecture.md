# System Architecture

The project is a layered C++17 simulation and localhost implementation of a distributed audio system. Physical audio is optional; synthetic and mock paths are the deterministic baseline.

## Data Flow

```text
Source / capture
       |
       v
AudioFrame -> DSP -> session control
       |                    |
       v                    v
   media UDP          discovery/control UDP
       |
       v
validation -> sequence tracking -> jitter buffer
       |
       v
scheduler -> simulated/mock/optional physical sink
```

## Components

### AudioFormat

`AudioFormat` describes the PCM representation: sample rate, channel count, and bits per sample. Its validation catches zero-valued fields and sample widths that do not align to whole bytes.

### AudioFrame

`AudioFrame` is an owning value type for one interleaved PCM block. It combines an `AudioFormat`, sequence number, presentation timestamp, and byte-oriented payload. The constructor validates that the payload contains a whole number of PCM sample frames and uses move construction for the payload.

### AudioBuffer

`AudioBuffer` is a bounded FIFO queue. A blocking producer waits for capacity, while a blocking consumer waits for data. `try_push()` and `try_pop()` provide non-blocking alternatives. `close()` wakes all waiters, prevents new frames from being queued, and permits consumers to drain frames already in the queue before reporting that no more data is available.

The capacity provides backpressure and prevents an unbounded queue from consuming memory if production temporarily exceeds consumption. This gives future network receivers and playback workers a clear, thread-safe handoff point.

## Layers

- Data model: `AudioFormat`, `AudioFrame`, bounded `AudioBuffer`.
- DSP: normalized float conversion, gain, channel conversion, mixing, metering, pipeline.
- Media plane: explicit PCM16 packet protocol, UDP transport, sequence tracking, jitter, scheduling, resilience.
- Control plane: discovery, capability matching, session negotiation, lifecycle, liveness.
- Synchronization: deterministic endpoint clocks, drift/offset estimation, bounded correction.
- Audio I/O: backend-neutral devices, deterministic mock backend, optional PortAudio feature detection.

## Ownership and Threads

Owning classes use RAII. Sockets own file descriptors; managers own registries; stream endpoints own their jitter/timing state. Current demos use bounded synchronous polling. The architecture leaves clear handoff boundaries for future network, DSP, and audio callback threads. Shutdown is explicit and queues are bounded; the implementation is not hard-real-time safe.

## Control and Media Separation

Discovery and session messages negotiate identity, capabilities, configuration, and lifecycle. Media packets carry PCM payloads and a session envelope. Neither plane serializes native C++ structs, and both validate lengths and identities before use.