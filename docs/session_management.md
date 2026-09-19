# Audio Session Negotiation and Lifecycle

Phase 8 adds a control-plane session layer over Phase 7 discovery. It selects a concrete compatible audio configuration, establishes a logical session, starts/stops that session, and keeps session state separate from Phase 4 media packets. No physical audio device or new media transport is introduced.

## Components

`SessionId` is a 128-bit ordered value with deterministic test construction and hexadecimal text conversion. `AudioSessionConfig` reuses `AudioFormat` and carries samples per frame, sender/receiver device IDs, and audio UDP ports. `AudioSessionManager` owns a mutex-protected map of `SessionInfo` values. `SessionControlService` owns a separate POSIX UDP control socket and delegates validated messages to the manager.

Control traffic uses separate ephemeral or configured UDP ports so it remains logically distinct from the Phase 4 audio packet protocol. The session layer associates its negotiated audio ports with the existing media transport; it does not replace or redesign that transport.

## State Machine

```text
Idle --offer--> Negotiating --accept--> Established --start--> Streaming
                  |                         |  \             |
                  | timeout/reject          |   \ stop       | stop
                  v                         v    v            v
                Failed                    Failed Stopping --> Closed
```

Managers do not expose arbitrary state assignment. Valid transitions are controlled internally. Invalid operations such as starting a Negotiating or Closed session, accepting an unknown session, or stopping an unknown session are rejected without corrupting state. Duplicate offers for an established session produce an idempotent ACCEPT; duplicate START/STOP messages do not create new sessions or invalid transitions.

## Configuration Selection and Negotiation

The initiator checks the discovered `DeviceInfo` capabilities before creating an offer. Selection is deterministic: the current compatibility helper chooses the first common values from the ordered local capability lists, whose demo lists prefer 44.1/48 kHz order and stereo where available. The selected configuration is then validated again by the receiver against its local capabilities before ACCEPT.

An OFFER creates a Negotiating session at the initiator. A receiver validates identity, configuration, and destination, creates the same SessionId in Established state, and returns ACCEPT. The initiator transitions to Established only after receiving a matching ACCEPT. Unsupported proposals return structured REJECT reasons such as incompatible format, unsupported rate, unsupported channels, invalid configuration, busy, or unknown device.

## Control Wire Format

All multibyte fields use big-endian network byte order. Native C++ layout is never serialized. Every message starts with a 12-byte header:

| Offset | Size | Field | Rules |
|---:|---:|---|---|
| 0 | 4 | Magic | `0x53455331` (`SES1`) |
| 4 | 1 | Version | `1` |
| 5 | 1 | Message type | 1 OFFER, 2 ACCEPT, 3 REJECT, 4 START, 5 STOP, 6 ACK |
| 6 | 2 | Reserved | Must be zero |
| 8 | 4 | Payload length | Must equal the remaining datagram size |

The payload begins with a 16-byte SessionId, 16-byte source DeviceId, 16-byte destination DeviceId, one-byte rejection reason, and one-byte configuration-present flag. OFFER and ACCEPT then carry a 48-byte configuration extension: 16-byte sender ID, 16-byte receiver ID, 4-byte sample rate, 2-byte channel count, 2-byte bits per sample, 4-byte samples per frame, and 2-byte sender/receiver audio ports. REJECT, START, STOP, and ACK carry no configuration extension.

The maximum control packet is 256 bytes. Parsers validate magic, version, type, lengths, identities, rejection reason, configuration presence, sample limits, PCM format, and ports before accepting a message. Truncated, oversized, malformed, and unsupported packets return explicit parse errors.

## Timeout, Duplicates, and Device Loss

Negotiating sessions have an injected-clock timeout. Expiration transitions them to Failed with a timeout reason rather than leaving them Negotiating forever. `device_unavailable` transitions related Negotiating, Established, or Streaming sessions to Failed with a remote-unavailable reason. The manager supports multiple independent SessionIds and stopping one does not affect another.

Duplicate control messages are not cryptographic replay protection. They are handled for lifecycle safety: duplicate offers for an established session receive the existing configuration, duplicate starts are harmless, duplicate stops are harmless, and duplicate accepts do not create a second session.

## Demonstration and Limitations

The demo first discovers devices using the Phase 7 localhost exchange. It then creates two managers and two real UDP control services, sends an OFFER, receives ACCEPT, sends START, and sends STOP. Both managers converge on the same SessionId and configuration. It also runs an incompatible-format rejection through the manager logic.

The current session layer is a control-plane foundation. It has no authentication, encryption, retransmission protocol, persistent identity, background control worker, physical playback, media-session ownership, or hard-real-time guarantee. Future work can attach an established configuration to Phase 4 packet production and add authentication/session policy without changing the packet codec or state machine foundations.