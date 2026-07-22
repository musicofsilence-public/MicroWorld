# Memory Benchmarks

Inherits `../AGENTS.md`.

## Architecture

Benchmarks are downstream public-API consumers of `MicroWorld::Memory`. They
measure retained utility layout, injected-resource attribution, global
allocation deltas, and bounded operation counts without becoming production
dependencies.

## Evidence concepts

- Every workload validates a semantic counter before reporting cost.
- Custom pointer and container workloads use only caller-owned fixed resources
  and must report zero global allocation delta.
- The standard shared-pointer comparison is a successful exceptions-enabled
  size/attribution prototype; it never induces allocation failure.
- Host measurements are relative development evidence. Target runtime claims
  require separately authorized hardware execution.

## Verification

Build and run `microworld_memory_benchmark` with strict warnings. Record the
exact compiler, configuration, output, and source state under `Results/`.
