# ADR 0009: Application Drives a Narrow Engine Runtime

Status: Accepted

Date: 2026-08-02

## Context

An application must drive an engine without naming the compile-time capacities of
its concrete engine type. The existing `IEngine` contract achieved that type
erasure, but it also exposed the world, object store, and messaging system. Those
facilities configure or inspect the engine; they are not part of starting, advancing,
or stopping it.

The broad surface made the application dependency look like a substitute for the
complete engine even though typed creation, registration, timers, and capacity-bound
operations remained concrete. It also required runtime-turn substitutes to provide
unrelated engine services.

## Decision

- **The type-erased boundary is named `IEngineRuntime`.** Runtime means the live
  execution turns an application drives, not the complete engine surface.
- **The contract contains only `BeginPlay`, `Tick`, and `EndPlay`.** Their result,
  timing, ordering, and shutdown semantics remain the engine's authoritative runtime
  contract.
- **`TEngine<TTraits>` realises the runtime contract.** Compile-time capacities and
  concrete engine facilities remain hidden from application lifecycle code.
- **The runtime contract stands independently from the concrete engine definition.**
  Depending on the narrow boundary must not also expose the full engine surface.
- **Configuration stays concrete and explicit.** World, storage, messaging, timers,
  registration, and typed creation do not enter the runtime interface. An application
  that needs them retains the concrete engine or the specific collaborators it uses.
- **The application configure hook does not receive `IEngineRuntime`.** The hook
  preserves configure-before-begin ordering without turning the runtime contract into
  a service locator.

## Consequences

- One application base can drive different engine capacity profiles without becoming
  a template.
- Runtime substitutes implement three operations and no longer fabricate unrelated
  engine state.
- The rename and contraction are source-breaking for consumers that named `IEngine`
  or used its configuration methods polymorphically.
- The breaking public change advances the pre-1.0 package to the next minor version.
- Concrete application types may retain both their `FApplication` base and a typed
  engine reference when they configure engine-specific facilities. That duplication
  is accepted because it keeps the boundary honest and the dependency visible.
- Virtual dispatch remains once per application runtime turn; no new runtime storage,
  allocation, or scheduling behavior is introduced.

## Alternatives considered

- **Keep the broad `IEngine`.** Rejected: it preserves a service-locator-shaped
  surface that the runtime consumer does not need.
- **Remove the interface and template `FApplication` on the concrete engine.**
  Rejected: capacity traits would spread through application types and the compiled
  application implementation would move into headers for no demonstrated runtime
  benefit.
- **Split lifecycle and configuration into two abstract interfaces.** Rejected: no
  consumer needs polymorphic configuration, so the second abstraction would be
  speculative.

## Revisit triggers

- A measured target shows the runtime virtual dispatch or vtable is material to its
  budget.
- More than one production consumer needs the same polymorphic configuration
  contract, with a stable surface that is not the concrete engine API.
- `FApplication` no longer needs to hide engine capacity traits, removing the reason
  for runtime type erasure.
