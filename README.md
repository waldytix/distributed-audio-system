# Distributed Audio System

Distributed Audio System is a professional C++ project that will evolve into a distributed network audio system. It is written in modern C++ with a focus on embedded and software engineering fundamentals.

## Planned Features

- Network audio streaming
- Virtual speaker nodes
- Audio synchronization across nodes
- Buffering and latency management
- Concurrent processing
- System diagnostics
- Automated testing

## Current Status

**Phase 1: Foundation**

The current phase establishes the project structure, CMake build system, C++17 configuration, compiler warnings, and a minimal command-line application. Networking, audio playback, DSP, and distributed synchronization are intentionally not implemented yet.

## Build and Run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/distributed_audio_system
```

A Release build can be configured with `-DCMAKE_BUILD_TYPE=Release`.
