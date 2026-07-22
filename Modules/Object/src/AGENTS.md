# Object Runtime Implementations

Inherits `../AGENTS.md`.

## Architecture

`src/` implements non-template Object storage and collection behavior using
only Object, Memory, and Core public contracts. It must preserve caller-owned
capacity and never introduce a hidden allocator, SDK, logger, clock, thread,
or product policy.

## Concepts

Runtime implementations mutate only caller-owned stores and collector state;
public typed results expose every capacity and lifecycle failure.

## Verification

Build `MicroWorld::Object` with warnings as errors, exceptions disabled, and
RTTI disabled. Comments explain only non-obvious ownership or collection
invariants.
