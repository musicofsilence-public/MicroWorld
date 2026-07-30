# Engine Benchmarks

Inherits `../../AGENTS.md`.

## Architecture

Engine benchmark source (`GarbageCollectorBenchmark.cpp`, inherited from the
folded Object package) plus the Engine and Object measured-margin records. The
Object results are preserved under `Results/Object/` because they measure the
GC/store independently of the World/Actor runtime.

## Concepts

The garbage-collector benchmark measures bounded incremental collection over
fixed caller-owned storage. Measured margins live in `Results/Host.md`,
`Results/Esp32S3N16R8.md`, and `Results/Object/`.

## Verification

Build with `-DMICROWORLD_BUILD_BENCHMARKS=ON` (`microworld_engine_benchmark`
target). See `Results/` for recorded evidence anchored to its source commit.
