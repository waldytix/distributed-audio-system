# Demo Commands

From a configured build directory:

```bash
./distributed_audio_system --help
./distributed_audio_system --version
./distributed_audio_system --list-devices
./distributed_audio_system --synthetic
./distributed_audio_system --resilience-demo
./distributed_audio_system --full-demo
./distributed_audio_system --benchmark
./distributed_audio_system --input-device mock-input --output-device mock-output
```

The default and `--synthetic` modes run the deterministic end-to-end localhost pipeline. `--resilience-demo` adds deterministic loss, reorder, duplicate, concealment, adaptive-buffer, and recovery statistics. `--full-demo` identifies the complete integration run. `--list-devices` reports the active backend; in the verified WSL environment that is the deterministic mock backend because PortAudio was unavailable.

No command requires physical audio hardware. Real-device mode is only meaningful when an optional backend and usable devices are available; the application does not silently claim or substitute physical devices.
