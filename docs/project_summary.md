# Project Summary

Distributed Audio System is a C++17 portfolio project exploring the architecture of a networked audio system without requiring physical audio hardware.

## Architecture

The project contains validated PCM data types, DSP utilities, bounded producer/consumer buffers, explicit binary UDP protocols, discovery and session control, deterministic timing/synchronization, session-driven media streaming, optional audio I/O boundaries, and resilience simulation.

The media path is synthetic or device-backed source -> DSP -> AudioFrame -> session envelope -> UDP -> validation -> sequence tracking -> jitter buffer -> scheduling -> simulated or optional output sink. Control-plane discovery and session negotiation remain separate from media transport.

## Engineering Areas

- Modern C++17, RAII, CMake, and target-local warnings.
- Explicit binary serialization with fixed widths, byte order, limits, and malformed-input rejection.
- Thread-safe bounded buffers and deterministic shutdown behavior.
- PCM conversion, gain, channel processing, mixing, metering, and pipeline tests.
- UDP loopback transport with ordering/loss awareness.
- Manual-clock timing, jitter buffering, clock estimation, and bounded multi-device correction.
- Discovery, capability matching, session negotiation, lifecycle control, and structured failure states.
- Synthetic/mock hardware-independent validation plus optional PortAudio detection.
- Deterministic network impairment, adaptive jitter policy, liveness monitoring, and resilience metrics.

## Validation

The repository uses focused internal test targets with CTest. Tests cover protocol round trips, malformed input, concurrency, DSP values, UDP loopback, lifecycle state machines, deterministic impairment, multi-device convergence, mock audio I/O, resilience accounting, and final integration workloads.

## Limitations

The project does not claim production readiness, hard-real-time safety, guaranteed lossless UDP delivery, sample-accurate physical synchronization, authentication, encryption, adaptive bitrate, or physical audio testing in WSL. Those are explicit future improvements rather than hidden assumptions.
