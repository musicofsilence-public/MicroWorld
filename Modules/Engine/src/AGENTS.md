# Engine Runtime Implementations

Inherits `../AGENTS.md`.

## Architecture

`src/` implements non-template `UWorld`, `AActor`, and `UActorComponent`
behavior using only Engine, Object, Memory, and Core public contracts. It must
preserve caller-owned registration storage and never introduce a hidden
allocator, SDK, logger, clock, thread, or product policy.

## Concepts

Runtime implementations mutate only caller-owned registries and the world's
spawn/destroy barrier; public typed results expose every registration,
lifecycle, and capacity failure.

## Verification

Build `MicroWorld::Engine` with warnings as errors, exceptions disabled, and
RTTI disabled. Comments explain only non-obvious ownership, tick order, or
lifecycle invariants.
