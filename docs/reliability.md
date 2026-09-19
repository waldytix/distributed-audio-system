# Network Reliability and Resilience

Phase 11 adds bounded resilience mechanisms around the existing Phase 2-10 architecture. UDP remains the media transport; the project does not attempt to turn it into a reliable byte stream.

## Statistics

`NetworkStatistics` owns counters behind a mutex and exposes value snapshots. It tracks packet acceptance/rejection categories, bytes, loss, missing/unrecoverable frames, concealment, reconnect/interruption counts, jitter, and buffer depth. Snapshot reads do not expose mutable internal state.

## Adaptive Jitter Policy

`AdaptiveJitterBuffer` wraps the existing `JitterBuffer` and keeps its bounded queue and concealment behavior. A smoothed observed jitter value expands the target depth by one when jitter exceeds 2 ms, and contracts by one after stable observations below 0.5 ms. Target depth is clamped between configured minimum and maximum values and never changes the fixed queue capacity. The existing fixed `JitterBuffer` API remains unchanged.

## Loss and Recovery Policy

The current resilience path chooses bounded concealment rather than retransmission. Missing frames advance after the existing jitter deadline and are counted as unrecoverable/concealed. This avoids indefinite playback stalls and avoids creating a second control protocol. A future short-window NACK can be added above this boundary with an explicit deadline; it is intentionally not implemented here.

## Fault Injection

The existing deterministic impairment plan supplies delay, order, loss, and duplication. Phase 11 scenarios add burst loss by listing a contiguous lost range, plus reordered and duplicated packets. This is application-level simulation, not OS-level network impairment.

## Liveness and Recovery

`SessionLiveness` uses injected monotonic timestamps. Valid traffic resets the liveness timer. After the warning threshold it enters `recovering`; after the disconnect threshold it enters `interrupted`. Retry attempts are bounded by a configured count and interval. A returning peer can restore `streaming`; exhaustion enters `failed`; explicit shutdown enters `stopped`.

This state model is intentionally separate from the Phase 8 session state machine so control-plane ownership is not duplicated. A production integration can notify `AudioSessionManager` through a narrow failure/recovery interface.

## Bounded Behavior

Queues remain bounded, adaptive depth is limited, recovery attempts are finite, and playback never waits forever for a missing packet. No busy-waiting or per-packet console logging is used. The deterministic stress test processes 100 frames with controlled loss and remains bounded.

## Limitations

There is no retransmission/NACK protocol, FEC, congestion control, network path measurement, adaptive bitrate, or production reconnect handshake. UDP still does not guarantee delivery, order, or duplicate protection. Audio concealment is simple silence behavior inherited from Phase 5. The implementation is not hard-real-time safe because standard containers, mutex-protected snapshots, allocations, and exception-capable setup remain present outside the callback-safe design boundary.