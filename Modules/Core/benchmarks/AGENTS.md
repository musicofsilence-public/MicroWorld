# Performance Benchmarks

Inherits `../AGENTS.md`.

## Architecture

Benchmarks are downstream public-API consumers. The host executable measures
repeatable relative dispatch cost and allocations; the ESP32-S3 consumer
measures the same scheduling profiles with target counters. Production
MicroWorld code has no dependency on either harness.

## Workload concepts

- Disabled, all-due, mixed-rate, and maximum-capacity profiles cover the
  scheduling branches without changing framework semantics.
- Warm-up, measured updates, and trial counts are fixed so results remain
  comparable across builds.
- Each harness validates expected tick counts so a faster semantic regression
  cannot be accepted as an optimization.
- Host timing is diagnostic; only target map, cycle, heap, and stack evidence
  can approve target-specific changes.

## Documentation and verification

- Document benchmark functions, configuration constants, and accumulated
  measurement state with the reason each is needed.
- Measurements may justify optimization but must never redefine semantics.
- Verify the host benchmark target and compile with
  `pio run -d Modules/Core/tests/consumer -e esp32-s3-benchmark`.
