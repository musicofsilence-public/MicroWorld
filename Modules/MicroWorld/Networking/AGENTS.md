# MicroWorld Networking System

Inherits `../../AGENTS.md`.

## Architecture

Networking depends on Core, Messaging, and Transport, plus the C++17 standard
library. Engine is deliberately absent: `TNetworking` reaches the engine only
through Core's `IPlaySystem`, so no portable system sees both Engine and
Transport — only a composition root does. That is the boundary this shape
protects.

The system owns `TNetworking<TTraits>`: one object that turns one or more
devices into a working networked stack by owning the transport hosts, the shared
message router, the per-channel bindings, the guaranteed channels, and the
direct lifecycle pumping that preserves their required order, all behind the
`IPlaySystem` interface a `TEngine` drives. It is header-only (`TNetworking` is a
template instantiated by the caller) and performs no allocation.

This was the Integration package; it became the Networking system so the folder
tree names the architecture directly.

## Concepts and boundaries

- `TNetworking<TTraits>` derives `IPlaySystem`; a caller hands it to a `TEngine`
  at construction and its lifecycle/frame turns drive the whole networked stack
  as one bound system.
- `AddDevice` configures a `TTransportHost` over the driver but never starts it.
  `BeginPlay` freezes composition and starts live hosts in driver add order at
  the engine's canonical timestamp.
- `AddChannel` enforces the two ordering rules a caller would otherwise have to
  know: a guaranteed channel's `SetInnerChannel` runs before its router
  `AddChannel`. Its direct pump dispatches hosts before the router, then flushes
  the router, reliable channels, and hosts in the required order.
- `FDeviceHandle` and `FChannelHandle` are generation-checked (index plus
  generation plus `IsValid()`), following `FPeerId` and `FTimerHandle`, so a
  rejected or stale handle cannot address a live slot.
- One shared router serves every driver, demultiplexing by channel id.
- Portable code uses fixed-width/value types, fixed-capacity in-place storage,
  deterministic lifetimes, and no RTTI, exceptions, logging, threads, clocks,
  heap containers, SDK calls, or global mutable state.

## Verification

Build the engine from the repo root; Networking is the `microworld_networking`
INTERFACE target (with `MicroWorld::Networking` and `MicroWorld::Networking`
aliases). Run the dependency-boundary checker with the Networking system root
and the Networking behavior tests after changes. This guide owns durable
boundaries; the system's headers and tests define its current behavior.
