# Platform Benchmarks

Inherits `../../AGENTS.md`.

## Architecture

Platform benchmark evidence. The Esp32 family's `Results/` holds the ESP32-S3
compile margins for the lwIP/wired/E32 transports.

## Concepts

Compile margins are a compile-only proof; runtime readiness requires the
per-driver hardware checkpoint.

## Verification

See `Esp32/Results/` for the recorded evidence anchored to its source commit.
