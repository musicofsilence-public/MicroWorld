# MicroWorld Messaging Package

Inherits `../AGENTS.md`.

## Architecture

`microworld-messaging` is the portable actor-messaging package. Its dependency
direction is `Core <- Messaging`: it may depend only on Core and the C++17
standard library. Engine, Net, Integration, platform, SDK, and transport-driver
headers must not appear here.

The package owns message vocabulary and codecs, bounded routing, channel
interfaces and bindings, and bounded reliable delivery. Its router and reliable
channel participate in caller-owned frame ordering without owning a world or
engine. It is transport-agnostic: callers provide a channel or a duck-typed
network facade at the edge rather than giving Messaging a Net dependency.

There is no production translation unit and no archive: every primitive is a
template or an inline codec whose caller-selected capacities must stay visible at
instantiation, so `microworld_messaging` is an INTERFACE target. Any source file
added later may depend only on Messaging's public headers and Core, and must
introduce no hidden transport, engine, clock, heap, or SDK coupling.

## Concepts and boundaries

- All routing, handler, queue, retry, and frame-set capacities are caller
  selected and fixed at compile time; steady-state message work allocates
  nothing and reads no hidden clock.
- Message delivery remains queued and deterministic. Time arrives from the
  caller, and physical transport policies stay outside this package.
- Public symbols live in the flat `MicroWorld` namespace below the
  `Messaging/` include layout. The package never owns worlds, actors, engines,
  network hosts, drivers, or platform resources.

## Verification

Configure this package independently with CMake, compile public headers under
C++17 with strict warnings, exceptions disabled, and RTTI disabled, run the
dependency-boundary checker with a Messaging package root, and run the package
tests required by the current package scope. This guide owns durable boundaries;
the package's headers and tests define its current behavior.
