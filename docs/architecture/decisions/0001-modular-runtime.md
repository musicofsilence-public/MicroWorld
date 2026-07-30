# ADR 0001: An Application Pays Only For The Layers It Uses

- **Status:** Accepted in principle; the specific layer topology it named is
  superseded by [ADR 0004](0004-module-tree-mirrors-architecture.md) and the
  current model
- **Date:** 2026-07-19
- **Decision owner:** Project owner

## Context

The foundation is useful on its own — lifecycle, tick scheduling, time. Managed
objects and the engine built on them need further ownership contracts, and those
contracts cost memory and compile time that a small application has no reason to
pay.

## Decision

- **Portable layers stay separate, and a consumer selects what it uses.** The
  foundation remains independently usable, and building on it must not change how
  it behaves for someone who does not.
- **Composition roots own concrete resources and adapters.** No global runtime
  registry is introduced, so what an application depends on is visible where it is
  assembled rather than discovered at run time.

## Consequences

- The smallest applications keep the foundation without managed-object cost.
- Layer boundaries stay inspectable, which is what later allowed them to be
  machine-enforced rather than reviewed.
- Selecting layers is a real decision a consumer must make, not a default.

## Historical scope

The original record named a specific layer chain and several packages that were
never built — serialization, an engine-network bridge, platform-port packages.
That topology is not current work, and it is speculative complexity of the kind
this project now rejects on sight. What survives is the principle above.

## Revisit triggers

- A real application shows a layer boundary creates more complexity than it
  removes.
