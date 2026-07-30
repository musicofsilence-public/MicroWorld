# ADR 0002a: Smart-Pointer Foundations

- **Status:** Accepted pointer foundation; target runtime margins remain unmeasured
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

`TObjectPtr`, `TWeakObjectPtr`, and `TStrongObjectPtr` are managed-object handles,
not aliases for unique/shared ownership. `UObject` allocation through `TUniquePtr`
or `TSharedPtr` is rejected. Two lifetime categories that look alike must not be
spelled alike.

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

| Candidate | Handle size | Allocation | Exceptions-off result |
| --- | ---: | --- | --- |
| Thin `TUniquePtr` | 32 bytes | one injected 16-byte allocation; zero global | Passed |
| `std::unique_ptr` plus resource deleter | 32 bytes | identical | Passed |
| Custom `TSharedPtr` | 8 bytes | one injected 56-byte combined allocation; zero global | Passed |
| Custom `TWeakPtr` | 8 bytes | same block retained after value expiry | Passed |
| `std::shared_ptr` | 16 bytes | one 40-byte global block | No typed OOM in C++17 |
| `std::weak_ptr` | 16 bytes | shares the standard control block | Same limitation |

The standard shared prototype ran only with exceptions enabled and successful
allocation; no deliberate out-of-memory case was attempted. C++17
`allocate_shared` reports allocation failure by throwing, and disabling exceptions
does not convert that into the typed failure result this project requires. **That
contract mismatch is the finding — not the size difference**, which would not on
its own have justified a custom implementation.

Measured on a desktop host with exceptions and RTTI disabled, and compiled for the
target under the same settings. These are size and compile facts; target runtime
margins were not measured and are not claimed.

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
5. treat the shared pointers as provisional until target margins are measured and
   accepted; this decision does not invent an absolute budget or establish target
   runtime support.

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
