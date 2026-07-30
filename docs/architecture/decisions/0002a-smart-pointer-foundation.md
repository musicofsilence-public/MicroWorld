# ADR 0002a: Smart-Pointer Foundations

- **Status:** Accepted pointer foundation; Memory target runtime margins remain unmeasured
- **Date:** 2026-07-19
- **Decision owner:** Project owner

## Context

MicroWorld needs UE-familiar unique, shared, and weak pointer vocabulary for
non-managed objects. Reimplementing standard ownership machinery unnecessarily
would create memory-safety risk, but the portable contract also requires
injected memory resources, explicit out-of-memory results, exceptions-off
compilation, and reviewable control-block cost.

## Decision

### Unique ownership

Use `std::unique_ptr` move/destruction semantics as the preferred foundation
with a MicroWorld resource-aware deleter. A `MakeUnique`-style factory:

1. asks the injected resource for aligned storage;
2. placement-constructs only after success;
3. returns a typed pointer/OOM result; and
4. returns the exact block to the same resource after one destruction.

`TUniquePtr` remains a thin wrapper over `std::unique_ptr` plus the
resource-aware deleter. It must not duplicate a working standard ownership
implementation merely for naming.

### Shared ownership

Use a MicroWorld single-threaded strong/weak control block unless a prototype
proves a standard C++17 facility satisfies all required semantics.
`std::allocate_shared` normally reports allocation failure through the standard
exception contract and does not provide the required typed OOM behavior for an
exceptions-disabled portable baseline. The initial custom design therefore
owns object/control-block allocation through one injected resource and exposes
strong/weak counter limits explicitly.

Do not publish a thread-safe pointer mode until a real concurrent consumer,
atomic/toolchain compile probe, and target benchmark justify it.

### Managed references

`TObjectPtr`, `TWeakObjectPtr`, and `TStrongObjectPtr` are Object-module handle
types, not aliases for unique/shared ownership. `UObject` allocation through
`TUniquePtr` or `TSharedPtr` is rejected.

## Comparison criteria

| Candidate | Verify |
| --- | --- |
| `std::unique_ptr` plus resource deleter | object size, deleter size, move/destruction, exceptions-off compile, typed factory OOM |
| Thin `TUniquePtr` wrapper | same behavior plus any measurable overhead or API value |
| `std::allocate_shared` with custom allocator | allocation-failure behavior, control-block attribution, exceptions-off compile, object size |
| Custom single-threaded shared control block | strong/weak overflow, one allocation, OOM, destruction, weak expiry, size |

Select the smallest clear implementation that passes. Record rejected
candidates and target measurements before releasing the API.

## Recorded evidence

The 2026-07-19 MSVC x64 public-API benchmark recorded:

| Candidate | Handle size | Allocation evidence | Exceptions-off result |
| --- | ---: | --- | --- |
| Thin `TUniquePtr<FBenchmarkValue>` | 32 bytes | one injected 16-byte allocation; zero global allocations | Passed in standalone consumer |
| Equivalent `std::unique_ptr` plus resource deleter | 32 bytes | same injected allocation/deallocation; zero global allocations | Foundation compiles through thin wrapper |
| Custom `TSharedPtr<FBenchmarkValue>` | 8 bytes | one injected 56-byte combined allocation; zero global allocations | Passed in standalone consumer |
| Custom `TWeakPtr<FBenchmarkValue>` | 8 bytes | same 56-byte block retained after value expiry | Passed in standalone consumer |
| `std::shared_ptr<FBenchmarkValue>` | 16 bytes | successful attributed `allocate_shared` used one 40-byte global block | Does not expose typed OOM in C++17 |
| `std::weak_ptr<FBenchmarkValue>` | 16 bytes | shared standard control block | Same limitation |

The standard shared prototype ran only with exceptions enabled and successful
allocation. No deliberate standard-library OOM was attempted. C++17
`allocate_shared` communicates allocation failure through `std::bad_alloc`;
disabling exceptions does not turn that failure into the required
`ESharedPointerResult::OutOfMemory`.

The standalone host Core+Memory consumer compiled and ran with exceptions and
RTTI disabled. The ESP32-S3 Memory profile compiled with the representative
consumer translation unit under `gnu++17`, strict warnings, exceptions
disabled, and RTTI disabled. Its map proved that Core and Memory archives were
selected without later modules. These are compile/layout facts, not target
runtime or accepted-budget evidence.

Detailed measurements are recorded in
[`microworld-memory/benchmarks/Results`](../../../microworld-memory/benchmarks/Results).

## Accepted decision

The project owner accepted the following direction on 2026-07-19:

1. retain the std-backed thin `TUniquePtr`; the measured direct standard
   equivalent has identical size, and the wrapper adds the typed factory and
   prevents unsafe raw adoption;
2. keep raw `Release()` omitted because ownership transfer must retain the
   resource and exact allocation block;
3. provisionally retain the custom move-only, single-threaded
   `TSharedPtr`/`TWeakPtr` because it provides one attributed allocation,
   explicit counter overflow, weak expiry, and typed OOM under exceptions-off;
4. reject `std::shared_ptr`/`std::allocate_shared` as the portable foundation
   for this release because its C++17 allocation-failure contract is
   exception-based, not because the successful allocation layout is larger;
5. keep Memory experimental until target margins are measured and accepted;
   this decision does not invent an absolute budget or establish target runtime
   support.

## Consequences

- Unique ownership reuses proven standard semantics where they fit.
- Shared ownership accepts more custom safety work only because the explicit
  resource/OOM contract differs materially.
- Pointer vocabulary remains familiar while lifetime categories stay distinct.
- Thread safety and ISR safety are not implied.

## Revisit triggers

- A supported standard-library/toolchain combination provides a smaller
  exceptions-off, resource-attributed, typed-OOM shared implementation.
- A real concurrent consumer justifies a thread-safe specialization.
- Pointer/control-block size fails an accepted target budget.

Any revision requires ownership tests, no-exceptions/no-RTTI compile evidence,
and before/after size measurements.
