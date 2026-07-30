# MicroWorld Resource Rules

MicroWorld uses explicit resource limits. A host benchmark or target compile
does not create a target runtime budget.

## Permanent rules

- Storage, queues, object stores, and work per update are bounded.
- Capacity and invalid-operation failures are explicit.
- Portable code never silently falls back to a heap, performs an emergency full
  collection, or recurses through a runtime graph.
- Caller-supplied time is the only scheduling clock.
- Hardware, ISR, watchdog, transport, and safety lifetimes stay deterministic
  and outside GC.
- A feature may spend resources only after a representative application shows
  why it is needed.

## Evidence status

| Evidence | What it proves |
| --- | --- |
| Host measurement | The named host configuration and workload only |
| Target compile | Build, link, and complete-image static size only |
| Target runtime measurement | The named board, workload, and observed runtime fact |

Core has a zero framework-allocation lifecycle/tick invariant. Memory and
Object use caller-selected fixed resources in their tested paths. Exact sizes,
operation counts, toolchains, and qualifications belong in the immutable result
records, not this summary:

- [Core host](../Modules/benchmarks/Core/Results/Host.md) and
  [ESP32-S3 compile evidence](../Modules/benchmarks/Core/Results/Esp32S3N16R8.md)
- [Memory host](../Modules/Memory/benchmarks/Results/Host.md) and
  [ESP32-S3 compile evidence](../Modules/Memory/benchmarks/Results/Esp32S3N16R8.md)
- [Object host](../Modules/benchmarks/Engine/Results/Host.md) and
  [ESP32-S3 compile evidence](../Modules/benchmarks/Engine/Results/Esp32S3N16R8.md)

Target runtime margins for Memory and Object remain unmeasured. This is a
qualification for hardware support, not a reason to add speculative allocator
or engine complexity.
