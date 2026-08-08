# MicroWorld Engine Internal Details

Inherits `../AGENTS.md`.

## Architecture

This directory holds Engine-private implementation details for managed-object
construction. Its transaction machinery is not a supported application or
actor API.

## Concepts and boundaries

- A construction transaction is created only by the actor factory and is bound
  to its provisional actor handle.
- `FObjectInitializer::CreateDefaultSubobject` is the only nested managed-object
  creation path. It may create actor-owned components during constructor time,
  but cannot publish or resolve them until the transaction commits.
- The first descriptor, capacity, layout, or construction failure is sticky.
  Rollback destroys the actor first, then constructed components in reverse
  order, and releases every reserved object slot and lock.
- Descriptor registration is monotonic. A descriptor that registered before a
  later construction failure remains registered.
- Internal code may depend on Engine's public contracts, Core, Messaging,
  Networking, and C++17 only. It never exposes structural object-store mutation,
  platform APIs, threads, clocks, exceptions, RTTI, or heap allocation.

## Verification

Verify internals through Engine factory-selection, descriptor, capacity, and
rollback tests. The required observable contract is that failed construction
restores object slots and object counts without relaxing public mutation rules.
