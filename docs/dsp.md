# DSP Foundation

Phase 3 adds a small, testable processing layer over the Phase 2 byte-oriented `AudioFrame` and `AudioBuffer` types. It remains laptop-based and does not access audio hardware.

## PCM Representation

`AudioFrame` continues to own interleaved PCM bytes. The current conversion utility supports signed 16-bit little-endian PCM. Decoding maps samples to normalized `float` values using `sample / 32768.0`, so negative full scale is exactly `-1.0` and positive full scale is `32767 / 32768`.

Encoding uses the inverse signed-16-bit convention. Inputs outside the normalized range are saturated before conversion: values below `-1.0` become negative full scale and values above `1.0` become the largest positive signed 16-bit value. NaN samples are treated as zero. Conversion validates the format and complete sample/channel frames.

## Interleaved Audio

For stereo data, samples are stored as left/right pairs:

```text
L0, R0, L1, R1, L2, R2, ...
```

The DSP layer keeps this interleaved ordering. Channel-aware operations use the format's channel count to identify each frame.

## Gain

Gain can be configured as a linear multiplier or in decibels. The relationship is:

```text
linear = 10^(dB / 20)
```

For example, 0 dB is unity, -6 dB is approximately 0.501, and +6 dB is approximately 1.995. Gain processing is in-place and applies hard saturation to `[-1.0, 1.0]`. Bypass leaves samples unchanged.

## Channel Conversion

Mono-to-stereo duplicates each mono sample into both output channels. Stereo-to-mono uses the explicit average:

```text
mono = (left + right) / 2
```

Only mono and stereo conversions are currently supported. Unsupported layouts and invalid format/sample sizes are rejected rather than silently remapped.

## Mixing

`AudioMixer` accepts multiple normalized streams with identical formats and sample counts. It sums corresponding samples and applies hard saturation once after accumulation. Empty input returns no result, and incompatible streams are rejected.

## Peak Metering

`AudioMeter` calculates absolute peak values independently for each channel and for the complete signal. It does not modify the input samples. The caller supplies a result object that may be reused between calls.

## Pipeline

`DspPipeline` demonstrates the current processing order:

```text
AudioFrame bytes
    -> PCM16 to normalized float conversion
    -> optional mono/stereo conversion
    -> gain and saturation
    -> peak metering
```

Sequence number and timestamp metadata are preserved. The output format is updated when channel conversion occurs.

## Real-Time Considerations

The sample loops do not lock, sleep, perform I/O, or write to the console. Gain operates in place, and the meter can reuse its output vectors. Conversion, channel conversion, mixer result creation, and pipeline output construction currently allocate `std::vector` storage and may throw during setup or processing. A production audio callback would need preallocated working buffers, a no-allocation processing contract, and an explicit error strategy before being considered hard-real-time safe.