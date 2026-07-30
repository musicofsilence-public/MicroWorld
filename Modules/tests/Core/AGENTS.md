# Core Host Behavior Tests

Inherits `../../AGENTS.md`.

## Architecture

The Core test executable uses a static-registration harness and deterministic
fixed-capacity fakes. Tick tests own scheduling behavior; Memory and Delegate
tests own resource and callback contracts (they deliberately use over-aligned
fixtures). This directory also owns the shared harness (`TestMain.cpp`,
`TestSupport.h`, allocation counters) that every other system's tests include.

## Concepts

- Tests act only through public APIs and assert observable outcomes.
- Explicit millisecond values replace wall-clock time and sleeps.
- Fakes record only the state required to prove behavior.

## Verification

Run `ctest --test-dir build -C Release` (the `microworld_tests` target). The
`consumer/` subdirectory is the PlatformIO/Pico consumer harness, built
separately and not part of the ctest gate.
