# MicroWorld Memory Package

Inherits `../AGENTS.md`.

## Architecture

`microworld-memory` is the adjacent portable ownership package above Core.
Its dependency direction is `Core <- Memory`: higher packages may depend on
Memory, while Memory may depend only on Core and the C++17 standard library.

The package owns explicit memory-resource contracts and caller-supplied bounded
storage. It does not own a platform heap, managed objects, engine policy, or
vendor SDK code.

## Concepts and boundaries

- `IMemoryResource` makes allocation failure, deallocation, capacity, and usage
  observable without exceptions or a hidden fallback resource.
- `TFixedArena<Bytes, Alignment>` keeps storage and bookkeeping inside the
  caller-owned arena object and supports deterministic bounded reuse.
- Every successful allocation retains its originating resource and exact
  `FMemoryBlock`; later ownership types must return that same block to that same
  resource.
- Portable code uses fixed-width/value types, bounded work, deterministic
  lifetimes, and no RTTI, exceptions, logging, threads, clocks, or SDK APIs.

## Verification

Configure and build this package independently with CMake, compile its public
headers under C++17 with strict warnings, exceptions disabled, and RTTI
disabled, run the Core dependency-boundary checker with explicit Core and
Memory package roots, and run the package tests and benchmarks required by the
current package scope. Live status belongs in `../../PROGRESS.md`.
