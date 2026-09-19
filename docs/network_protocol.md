# Network Protocol

Phase 4 transports existing `AudioFrame` payloads over localhost UDP. The network payload is signed 16-bit little-endian PCM, while all multibyte protocol header fields use big-endian network byte order.

## Packet Layout

Every datagram contains this 40-byte header followed by the payload:

| Field | Offset | Size | Byte order | Meaning and validation |
|---|---:|---:|---|---|
| Magic | 0 | 4 | Big-endian | `0x44415331` (`DAS1`) |
| Version | 4 | 1 | N/A | Protocol version `1` |
| Flags | 5 | 1 | N/A | Must be zero in Phase 4 |
| Reserved | 6 | 2 | Big-endian | Must be zero |
| Sequence | 8 | 8 | Big-endian | Audio frame sequence number |
| Timestamp | 16 | 8 | Big-endian | Signed nanoseconds represented as two's-complement bits |
| Sample rate | 24 | 4 | Big-endian | Non-zero and at most 384,000 Hz |
| Channels | 28 | 2 | Big-endian | 1 through 8 |
| Sample format | 30 | 1 | N/A | `1`: signed PCM16 little-endian |
| Sample reserved | 31 | 1 | N/A | Must be zero |
| Samples/channel | 32 | 4 | Big-endian | Non-zero and at most 4,096 |
| Payload size | 36 | 4 | Big-endian | Must equal samples/channel x channels x 2 |
| PCM payload | 40 | variable | Little-endian samples | At most 1,200 bytes and must fill the datagram |

The protocol limits payloads to 1,200 bytes. A 5 ms, 48 kHz stereo PCM16 frame contains 240 samples per channel and 960 payload bytes, leaving room for the header while avoiding typical Ethernet fragmentation. The protocol does not implement IP fragmentation.

## Serialization and Validation

Serialization validates the source `AudioFrame`, format, payload alignment, sample count, and maximum payload before allocating the packet. Fields are appended individually; C++ object layout and host endianness are never used as a wire format.

Deserialization checks datagram size before reading fields, validates the header and arithmetic before constructing a payload, and returns a `ParseError` for malformed input. Invalid magic, version, flags, format, dimensions, sample count, payload size, truncated data, and excessive packets are rejected without crashing.

## UDP Transport

UDP is used because it provides a small datagram transport that maps naturally to packetized audio and is available through the Linux/POSIX socket API without external dependencies. UDP does not guarantee delivery, ordering, or duplicate protection. The receiver therefore tracks sequence numbers and reports accepted packets, malformed packets, duplicates, gaps, out-of-order packets, and estimated missing packets.

Sequence tracking uses unsigned modular subtraction. A new sequence is forward progress when its delta from the last accepted sequence is between 1 and $2^{63}-1$. Delta 1 is expected; a larger delta records the intervening values as estimated missing packets. Delta 0 is a duplicate. Larger modular deltas are treated as out-of-order. This handles rollover but is not a jitter buffer.

## Architecture Boundary

```text
AudioFrame
    |
    v
Packet serializer -> UDP sender -> localhost network
                                      |
                                      v
Packet parser <- UDP receiver <- validated datagram
    |
    v
AudioFrame -> AudioBuffer / DSP pipeline
```

Socket I/O remains outside the sample-processing loops. Phase 5 can add a jitter buffer, playout scheduling, synchronization, and recovery policy above this transport layer. Phase 4 does not claim reliable streaming, playback, or hard-real-time behavior.