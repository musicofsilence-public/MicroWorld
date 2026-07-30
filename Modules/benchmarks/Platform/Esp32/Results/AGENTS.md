# PlatformEsp32 Benchmark Evidence

Inherits `../AGENTS.md` and `../../AGENTS.md`.

## Architecture

This directory records immutable observations for an exact ESP32-S3 board,
PlatformIO/ESP-IDF toolchain version, build flags, and adapter workload (for
example `Esp32S3N16R8.md`). It does not own live status, target acceptance, or
release promotion.

## Concepts

Source anchors (toolchain version, board, flags) and evidence boundaries keep
compile and resource observations reproducible without turning them into
current-state runtime claims.

## Verification

Verify every value against the named source and retained build output. Never
infer runtime timing, heap, stack, or radio behavior from a compile-only
result; label compile-only evidence as such.
