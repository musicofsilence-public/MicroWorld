# Object Benchmarks

Inherits `../AGENTS.md`.

## Architecture

Benchmarks are downstream public-API consumers of `MicroWorld::Object`. They
measure fixed-object layout, graph shape, and bounded collector slices without
becoming production dependencies or making target-runtime claims.

## Concepts

Equivalent fixed graphs make full and incremental collection results directly
comparable. Host elapsed time is labeled host-only; fixed storage, semantic
operations, and slice bounds are the portable evidence.

## Verification

Validate each workload's semantic counters before recording costs. Keep host
evidence distinct from authorized target measurements.

`Results/` owns immutable, source- and environment-qualified evidence records.
It does not own live gate state or promotion decisions.
