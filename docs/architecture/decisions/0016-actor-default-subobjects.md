# ADR 0016: Actor Default Subobjects

Status: Accepted

Date: 2026-08-06

Decision owner: Project owner

## Context

An actor needs to own components that participate in the ordinary World and
Actor lifecycle without requiring application composition to attach each
component after spawning. Public object-store mutation is prohibited while a
managed object is under construction, so a general constructor-time creation
exception would break the Engine's ownership and rollback guarantees.

## Decision

- The actor factory creates a private construction transaction and a
  construction-only `FObjectInitializer` bound to the actor's provisional
  handle.
- An actor constructor may call `CreateDefaultSubobject<T>(Args...)` only
  through that initializer. The call creates one actor-owned component in the
  same transaction and returns a pointer that does not resolve until commit.
- Constructors cannot resolve their owner or World. The default component is
  privately attached and published before its actor is published.
- The first descriptor, capacity, layout, or construction failure is sticky;
  later default-subobject requests do nothing.
- Failure destroys the completed actor first, then constructed components in
  reverse order, and releases every reserved object slot and lock. Class
  descriptors registered before later object construction failure remain
  registered.
- This is not automatic subsystem construction, a public Engine attachment API,
  or a general relaxation of public object-store mutation rules.

## Consequences

- Actor types can declare their default component ownership in their
  constructors while retaining one transaction and one rollback path.
- Applications continue to spawn actors only; they do not register or attach an
  actor's default components.
- A construction failure is observable before actor publication and leaves no
  partially published actor/component graph.
- World subsystems remain explicitly registered application services under ADR
  0014.

## Alternatives considered

- Add a public Engine component-attachment API: rejected because it leaks
  actor/component assembly into applications and creates a second ownership
  path.
- Permit unrestricted constructor-time managed creation: rejected because it
  would bypass transaction ownership, publication, and rollback rules.
- Automatically discover or construct subsystems: rejected because default
  actor components do not justify implicit World service composition.

## Revisit triggers

- A supported actor needs a construction-time relationship that cannot be
  represented by an actor-owned default component.
- Measurements show the transaction's fixed storage or rollback work exceeds a
  supported target budget.
- A concrete application requirement demonstrates a need for another managed
  construction boundary.
