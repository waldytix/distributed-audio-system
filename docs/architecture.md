# Current Architecture

Phase 2 defines the in-memory core that future network and audio components can use. It does not perform networking, audio playback, DSP, device discovery, or distributed synchronization.

## Data Flow

```text
Producer
   |
   v
AudioFrame
   |
   v
AudioBuffer
   |
   v
Consumer
```

## Components

### AudioFormat

`AudioFormat` describes the PCM representation: sample rate, channel count, and bits per sample. Its validation catches zero-valued fields and sample widths that do not align to whole bytes.

### AudioFrame

`AudioFrame` is an owning value type for one interleaved PCM block. It combines an `AudioFormat`, sequence number, presentation timestamp, and byte-oriented payload. The constructor validates that the payload contains a whole number of PCM sample frames and uses move construction for the payload.

### AudioBuffer

`AudioBuffer` is a bounded FIFO queue. A blocking producer waits for capacity, while a blocking consumer waits for data. `try_push()` and `try_pop()` provide non-blocking alternatives. `close()` wakes all waiters, prevents new frames from being queued, and permits consumers to drain frames already in the queue before reporting that no more data is available.

The capacity provides backpressure and prevents an unbounded queue from consuming memory if production temporarily exceeds consumption. This gives future network receivers and playback workers a clear, thread-safe handoff point.

## Future Integration Boundary

Future phases may connect a network receiver or another producer to `AudioBuffer`, and a playback pipeline or another consumer on the other side. Those components are deliberately outside the current implementation so the core data ownership, validation, and synchronization behavior can be tested independently.