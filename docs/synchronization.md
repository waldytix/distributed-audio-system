# Receiver Timing and Synchronization Foundation

Phase 5 models the receiver-side timing problem after UDP transport. It does not provide physical playback or sample-accurate multi-device synchronization.

## Arrival Order Versus Playback Order

UDP arrival order is not audio playback order. Packets can be delayed, reordered, duplicated, or lost. `JitterBuffer` admits validated `AudioFrame` objects into a bounded sequence-aware queue and emits them by sequence number instead of insertion order.

The active sequence window assumes modular unsigned sequence arithmetic and remains below half of the `uint64_t` sequence space. A forward modular distance below $2^{63}$ is newer; zero is a duplicate; larger distances represent an older or late packet. This also handles rollover from `UINT64_MAX` to zero.

## Jitter Buffer

The buffer has `buffering`, `ready`, `draining`, and `stopped` states. It collects a configurable prebuffer target before normal extraction. Capacity is fixed; a new non-duplicate frame is rejected when the queue is full. Frames with a sequence number behind the playback cursor are rejected as late.

When the next expected sequence is absent, the consumer first receives `not_ready`. After the configured missing-frame wait expires, it receives an explicit `missing` result and the cursor advances. The default concealment policy creates a frame with the previous format and payload size filled with zero bytes. An optional repeat-previous policy reuses the previous payload. Both results are marked `concealed` so synthetic audio is not confused with received audio.

## Jitter Measurement

`JitterEstimator` tracks the variation of transit time, where transit is local monotonic arrival time minus the sender timestamp. It applies a simple one-sixteenth smoothing step to the absolute difference between successive transit values. The result is an estimate in nanoseconds, not an RTP claim or a network guarantee.

## Clock Offset and Drift

`ClockEstimator` fits the understandable model:

```text
local_time ~= offset + rate * remote_time
```

It subtracts the first sample from both timelines before regression to avoid fitting large absolute nanosecond values. At least two distinct samples are required. Non-monotonic samples and invalid numeric results are rejected.

Drift is reported as:

```text
drift_ppm = (rate - 1) * 1,000,000
```

Positive drift means the estimated local clock advances faster than the remote clock. Offset is reported at the reference sample in nanoseconds. This is an educational linear estimate, not PTP/NTP or a production synchronization algorithm.

## Playback Scheduling

`PlaybackScheduler` maps a remote frame timestamp through the clock estimator and adds a fixed playout delay:

```text
local_play_time = mapped_remote_time + playout_delay
```

Before the estimator is ready, scheduling returns no decision. At the exact scheduled instant the decision is `ready`; before it is `too_early`; after it is `late`. Waiting and sleeping are deliberately outside this core decision API.

Larger prebuffers and playout delays absorb more network jitter but add latency. Smaller values reduce latency but make reordering and loss deadlines less forgiving.

## Deterministic Simulation

The impairment utility applies explicit arrival order, loss, duplication, and delay plans. The demo and tests use a fixed scenario with reordered packets, a duplicate, and one lost frame. `SimulatedPlayback` records real versus concealed frames and the resulting logical playback sequence without requiring ALSA, PulseAudio, PipeWire, PortAudio, or hardware.

## Real-Time Boundary and Limitations

The intended future flow is:

```text
network thread
    -> jitter/timing layer
    -> playback queue
    -> real-time audio callback
```

The current jitter and timing implementation is not hard-real-time safe. It still uses `std::map`, `std::vector`, optional values, copying/moving frame payloads, and exception-capable setup/configuration. A production callback would need preallocated storage, bounded lock-free or carefully synchronized handoff, allocation-free processing, and a stronger error policy. Phase 6 is where physical speaker synchronization and production-grade distributed clock synchronization may be considered.