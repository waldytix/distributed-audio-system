# Interview Notes

## 30-Second Explanation

This is a C++17 distributed audio architecture built incrementally from PCM data and bounded buffers through DSP, UDP protocols, discovery, session negotiation, jitter handling, synchronization, streaming, mock audio I/O, and deterministic resilience testing. It runs entirely on localhost with synthetic/mock inputs when hardware is unavailable.

## 2-Minute Technical Explanation

The system separates the control plane from the media plane. Discovery advertises device identity and capabilities; session control negotiates a concrete AudioFormat and owns lifecycle transitions. The media path generates or captures frames, applies DSP, wraps existing PCM16 packets with a SessionId, sends them over UDP, validates and sequence-tracks them, inserts them into a bounded jitter buffer, schedules playback, and consumes them through a simulated or optional output abstraction.

## Design Talking Points

- UDP keeps audio datagrams bounded and low overhead; loss, ordering, and duplicates are handled explicitly rather than hidden.
- Bounded buffers provide backpressure and prevent latency/memory growth from becoming unbounded.
- Manual clocks and deterministic impairment plans make timing and failure behavior testable without sleeps or hardware.
- Session and stream state machines reject invalid transitions and make shutdown/failure behavior visible.
- Snapshot statistics avoid exposing mutable counters across threads.
- The mock backend lets CI validate audio lifecycle and format handling without microphones or speakers.
- Phase 6 synchronization is a bounded simulation, not a claim of hardware clock discipline.

## Production Work Remaining

A production system would need allocation-free callback paths, a carefully designed lock-free or bounded handoff, authentication/encryption, retransmission or FEC policy, adaptive bitrate, real device stream implementation, robust reconnect handshakes, and hardware-clock/resampling strategy.
