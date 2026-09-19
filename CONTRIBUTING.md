# Contributing

Configure a clean Debug build, compile with the existing warning policy, and run the complete CTest suite before submitting changes. Release verification is expected for changes affecting shared architecture.

Keep public interfaces clear and RAII-oriented. Preserve C++17 compatibility, bounded queues, structured validation, and deterministic tests. Do not make physical audio hardware a mandatory test dependency. Add focused tests for new behavior and avoid weakening existing assertions.

Keep changes scoped, document architectural decisions, and include relevant CLI or documentation updates. Pull requests should explain behavior, verification commands, known limitations, and any optional dependency assumptions.
