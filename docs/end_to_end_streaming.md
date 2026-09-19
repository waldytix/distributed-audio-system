# End-to-End Session-Driven Audio Streaming

Phase 9 connects the Phase 8 control plane to the existing Phase 2-5 media and timing components. It remains laptop-only and uses simulated playback rather than a physical audio device.

## Data Flow

```text
AudioSessionManager
        |
        v
SyntheticAudioSource
        |
        v
    DSP Pipeline
        |
        v
    AudioFrame
        |
        v
 SessionId envelope + existing packet serializer
        |
        v
    UDP Sender
        |
     localhost
        |
        v
    UDP Receiver
        |
        v
 Session and format validation
        |
        v
  SequenceTracker
        |
        v
    JitterBuffer
        |
        v
 PlaybackScheduler
        |
        v
 SimulatedPlayback
```

Phase 7 discovery supplies endpoint information. Phase 8 negotiates the `AudioSessionConfig` and controls Established, Streaming, Stopping, and Closed states. Phase 9 creates a media stream only from that negotiated session; the Phase 4 audio packet format remains the inner media format.

## Stream Lifecycle

`AudioStreamSender` and `AudioStreamReceiver` use the explicit states `Created`, `Ready`, `Running`, `Draining`, `Stopped`, and `Failed`. Preparation and start must occur in order. Running streams send or accept work. Stop transitions through Draining and releases the socket/jitter work deterministically. Invalid starts, sends, and receives are rejected without arbitrary state assignment.

The session lifecycle drives the stream boundary in the demo:

```text
Session Established -> stream Ready
Session Streaming   -> stream Running
Session Stopping    -> stream Draining/Stopped
Session Closed      -> stream Stopped
Session Failed      -> stream Failed
```

The stream classes remain separate from `AudioSessionManager`; the application coordinates the lifecycle so control state and media state do not form a circular ownership graph.

## Source and DSP

`SyntheticAudioSource` generates deterministic interleaved sine-wave samples using the negotiated sample rate, channel count, and samples per frame. It assigns monotonically increasing sequence numbers and nanosecond timestamps derived from the negotiated frame duration. Samples pass through the existing `GainProcessor`, are peak-metered, and are converted through the existing PCM16 utility before transmission.

## Media Association and Validation

The Phase 4 serialized audio packet is wrapped with a 16-byte SessionId envelope. This is the smallest backward-compatible association layer: the inner packet header and payload are unchanged, while the receiver can reject datagrams for an unknown session before inserting them into timing buffers.

The receiver also enforces the negotiated `AudioFormat`, channel count, PCM width, and exact payload dimensions for `samples_per_frame`. Invalid session IDs, malformed packets, incompatible formats, and wrong frame sizes are counted and discarded without failing the whole stream.

## Receive and Playback Path

Accepted datagrams are observed by the existing `SequenceTracker`, inserted into the existing bounded `JitterBuffer`, and consumed through `PlaybackScheduler` and `SimulatedPlayback`. Missing frames use the existing configured concealment policy and are counted separately from received and played real frames. The simulated sink consumes actual PCM payloads and records samples played, first/last sequence, and peak level.

The primary demo uses clean localhost UDP traffic. Deterministic impairment remains available through the Phase 5 impairment utilities and the stream layer can be tested with dropped, reordered, duplicated, or wrong-session datagrams without pretending the operating system network was impaired.

## Statistics and Shutdown

`StreamStatistics` exposes generated, transmitted, received, accepted, inserted, played, concealed, malformed, incompatible, duplicate, out-of-order, missing, byte, buffer-depth, and peak-level measurements. Sender and receiver stop operations are idempotent after a valid start/prepare sequence. No detached worker threads are used; the demo performs bounded synchronous polling and releases sockets through RAII.

## Multi-Stream and Synchronization Boundaries

Each stream owns a SessionId and its own statistics, socket, jitter buffer, and playback sink. Wrong-session media is rejected, so independent sessions cannot cross-contaminate one another. Phase 6 multi-device synchronization remains a separate simulated timing layer; this phase preserves that architecture rather than claiming physical speaker synchronization.

## Real-Time Limitations

The pipeline still allocates vectors for source generation, packet wrapping, UDP receive buffers, jitter buffering, and PCM metering. It also uses maps, optional values, exceptions during setup, and synchronous socket polling. These choices are appropriate for a deterministic portfolio simulation but are not hard-real-time safe. A production callback would require preallocated buffers, allocation-free processing, explicit lock-free or bounded handoff, and a stronger failure policy.