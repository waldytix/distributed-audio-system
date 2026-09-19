# Device Discovery and Node Management

Phase 7 adds a laptop-only discovery layer for identifying distributed-audio endpoints, advertising capabilities, maintaining a registry, and detecting departure or staleness. It does not create audio sessions, authenticate peers, encrypt traffic, or discover physical hardware.

## Architecture

```text
DiscoveryService
      |
      v
UDP discovery socket
      |
      v
Explicit message codec
      |
      v
DeviceRegistry ----> capability compatibility
      |
      v
Application/node management
```

`DeviceInfo` is intentionally independent of DSP, playback, and audio-session classes. It contains a stable `DeviceId`, display name, protocol version, IPv4 address, UDP ports, and advertised sample-rate, channel-count, and PCM-width capabilities.

## Device Identity

`DeviceId` is a 128-bit value represented as 32 hexadecimal characters. The implementation provides deterministic `from_u64` construction for tests and simulations, string conversion, equality, and ordering for registry keys. The all-zero identifier and malformed strings are invalid.

## Discovery Wire Format

All multibyte fields use big-endian network byte order. Native C++ object layout is never serialized.

### Header

| Offset | Size | Field | Rules |
|---:|---:|---|---|
| 0 | 4 | Magic | `0x44495331` (`DIS1`) |
| 4 | 1 | Version | Must be `1` |
| 5 | 1 | Message type | `1` ANNOUNCE, `2` QUERY, `3` RESPONSE, `4` GOODBYE |
| 6 | 2 | Reserved | Must be zero |
| 8 | 4 | Payload length | Must equal remaining datagram bytes |

The header is 12 bytes. QUERY has a zero-length payload. ANNOUNCE, RESPONSE, and GOODBYE carry the following payload:

| Field | Size | Encoding |
|---|---:|---|
| Device ID | 16 | Raw identifier bytes |
| Name length + name | 2 + variable | Big-endian byte length, maximum 96 bytes |
| Address length + address | 2 + variable | Big-endian byte length, maximum 64 bytes |
| Device protocol version | 2 | Big-endian |
| Audio port | 2 | Big-endian UDP port |
| Discovery port | 2 | Big-endian UDP port |
| Sample-rate count + values | 2 + 4 each | Maximum 16 rates |
| Channel-count count + values | 2 + 2 each | Maximum 16 values |
| Bits-per-sample count + values | 2 + 2 each | Maximum 16 values |

The maximum complete discovery datagram is 1,024 bytes. Parsers reject truncated headers, invalid magic/version/type, reserved bits, payload-length mismatches, zero IDs, invalid string lengths, excessive capability counts, and oversized packets before unchecked reads.

## Messages and Service

ANNOUNCE advertises a device. QUERY requests a response from the sender's source address and port. RESPONSE carries the responding device information. GOODBYE carries the departing device information and removes that ID from registries receiving it. `DiscoveryService` owns a POSIX UDP socket with deterministic nonblocking polling, validates every packet, updates its registry, responds to queries, and expires stale entries.

The demo creates three services bound to independent ephemeral localhost ports and exchanges real UDP ANNOUNCE traffic. It then demonstrates a late join, a changed announcement without a duplicate entry, a graceful GOODBYE, and deterministic stale expiration.

## Registry and Liveness

`DeviceRegistry` is protected by a mutex and returns copies of entries rather than exposing mutable internal state. Repeated announcements update the existing ID and last-seen timestamp. `expire(now)` removes entries whose last observation is at least the configured stale timeout. Tests use explicit `ClockTimePoint` values, so no long sleeps are needed.

The service's receive path does not hold the registry mutex during socket I/O. A future worker thread can poll discovery while application code reads snapshots or performs lookups. The current service itself is deliberately a small polling abstraction; lifecycle ownership remains explicit and `shutdown()` prevents further sends or processing.

## Capability Compatibility

`check_compatibility` returns a structured result containing a common sample rate, channel count, and bits-per-sample value when all three exist. It reports a reason string and does not negotiate or establish an audio session. Session setup belongs to a future phase.

## Limitations

Discovery currently targets IPv4 localhost use, does not authenticate announcements, does not encrypt messages, and has no multicast/broadcast management policy. Device IDs are supplied by the application rather than persisted by hardware. Announcement scheduling is represented by the service API and registry timeout, but a long-running background announcer is intentionally deferred. Future session management can use the registry and compatibility result to select endpoints before audio transport begins.