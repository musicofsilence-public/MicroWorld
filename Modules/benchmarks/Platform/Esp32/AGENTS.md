# PlatformEsp32 Benchmarks

Inherits `../AGENTS.md`.

## Architecture

Benchmarks are downstream public-API consumers of `MicroWorld::Platform::Esp32`
adapters. They measure real ESP32-S3 resource usage (flash, RAM, stack
headroom) for the UDP device, LoRa device, time source, and output device without
becoming production dependencies.

## Concepts

Measurements here are the authoritative closure of the compile-only proof:
`docs/Porting.md` requires a real-target smoke run before any adapter is
called runtime-ready, since compile success alone hid a lwIP-uninitialized
defect and a main-task stack overflow in Phase 6.2.

## Verification

Record toolchain, board, and build-flag identity alongside every measurement.
Keep host-only figures out of this directory; only real ESP32-S3 evidence
belongs here.

`Results/` owns immutable, source- and environment-qualified evidence
records. It does not own live gate state or promotion decisions.
