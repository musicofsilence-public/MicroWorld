# MicroWorld Integration Package

Inherits `../AGENTS.md`.

## Architecture

`microworld-integration` is the one package allowed to see both Engine and
Net. Its dependency direction is `Core <- Object <- Engine` plus `Core <-
Net`, joined at `Integration`: it depends on Core, Object, Engine, and Net,
plus the C++17 standard library. No other package may reach both Engine and
Net — the boundary this shape protects.

The package owns `TNetSystem<TTraits>`: one object that turns one or more net
drivers into a working networked engine by owning the net hosts, the shared
message router, the per-channel bindings, the guaranteed channels, and the
direct lifecycle pumping that preserves their required order, all behind the
`IEngineSystem` interface a `TEngine` drives. It is header-only
(`TNetSystem` is a template instantiated by the caller) and performs no
allocation.

## Concepts and boundaries

- `TNetSystem<TTraits>` derives `IEngineSystem`; a caller hands it to a
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
executable links Engine and Net), compile its public header under C++17 with
strict warnings, exceptions disabled, and RTTI disabled, run the
dependency-boundary checker with an Integration package root, and run the
package tests required by the current package scope. Live status and evidence
belong only in `../../PROGRESS.md`.
