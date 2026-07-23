# Engine Benchmarks

Inherits `../AGENTS.md`.

## Architecture

Benchmarks are downstream public-API consumers of `MicroWorld::Engine`. They
measure `TEngineHost` frame cost, world/actor/component graph shapes, and
bounded collector slices without becoming production dependencies or making
target-runtime claims.

## Concepts

Equivalent fixed host configurations make per-frame tick cost directly
comparable across host and target builds. Host elapsed time is labeled
host-only; fixed storage, semantic tick counts, and slice bounds are the
portable evidence.

## Verification

Validate each workload's semantic counters before recording costs. Keep host
evidence distinct from authorized target measurements.

`Results/` owns immutable, source- and environment-qualified evidence records.
It does not own live gate state or promotion decisions.
