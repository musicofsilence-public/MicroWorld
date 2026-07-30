# MicroWorld Integration Package

Inherits `../AGENTS.md`.

## Architecture

`microworld-integration` depends on Core, Messaging, and Net, plus the C++17
standard library. Engine is deliberately absent: `TNetSystem` reaches the engine
only through Core's `IPlaySystem`, so no package sees both Engine and Net — only
a composition root does. That is the boundary this shape protects.

The package owns `TNetSystem<TTraits>`: one object that turns one or more net
drivers into a working networked stack by owning the net hosts, the shared
message router, the per-channel bindings, the guaranteed channels, and the
direct lifecycle pumping that preserves their required order, all behind the
`IPlaySystem` interface a `TEngine` drives. It is header-only
(`TNetSystem` is a template instantiated by the caller) and performs no
allocation.

## Concepts and boundaries

- `TNetSystem<TTraits>` derives `IPlaySystem`; a caller hands it to a
  `TEngine` at construction and its lifecycle/frame turns drive the whole
  networked stack as one bound system.
- `AddNetDriver` configures a `TNetHost` over the driver but never starts it.
  `BeginPlay` freezes composition and starts live hosts in driver add order at
  the engine's canonical timestamp.
- `AddChannel` enforces the two ordering rules a caller would otherwise have to
  know: a guaranteed channel's `SetInnerChannel` runs before its router
  `AddChannel`. Its direct pump dispatches hosts before the router, then flushes
  the router, reliable channels, and hosts in the required order.
- `FNetDriverHandle` and `FChannelHandle` are generation-checked (index plus
  generation plus `IsValid()`), following `FPeerId` and `FTimerHandle`, so a
  rejected or stale handle cannot address a live slot.
- One shared router serves every driver, demultiplexing by channel id.
- Portable code uses fixed-width/value types, fixed-capacity in-place storage,
  deterministic lifetimes, and no RTTI, exceptions, logging, threads, clocks,
  heap containers, SDK calls, or global mutable state.

## Verification

Configure and build this package with CMake (it is header-only; the test
executable also links Engine, because `EngineNetHostTests` plays the composition
root the package itself must not be), compile its public header under C++17 with
strict warnings, exceptions disabled, and RTTI disabled, run the
dependency-boundary checker with an Integration package root, and run the
package tests required by the current package scope. This guide owns durable
boundaries; the package's headers and tests define its current behavior.
