# ADR 0002: Optional Bounded Managed Memory

- **Status:** Accepted
- **Date:** 2026-07-19
- **Decision owner:** Project owner

## Context

The engine vision requires GC and familiar managed Actor/Component behavior,
but supported microcontrollers vary widely in memory and timing capacity.
Hardware, interrupts, watchdog paths, and safety state machines also require
deterministic lifetimes that must not wait for collection.

## Decision

- GC is a first-class capability of the optional Managed ownership tier, not a
  mandatory Core cost.
- The application supplies a fixed-capacity non-moving object store and all GC
  bookkeeping.
- The first reference store uses equal-size slots because it is the simplest
  safe design. Object size/alignment and capacity failures are explicit.
- Local object identity is a slot index plus generation. Reusing storage cannot
  make an old handle identify a new object.
- `TObjectPtr` represents a traced managed reference only when visited through a
  managed object's descriptor/reference visitor.
- `TWeakObjectPtr` observes without keeping an object reachable.
- `TStrongObjectPtr` registers an explicit bounded root and reports root-
  capacity failure.
- World owns Actors strongly and Actors own Components strongly. Child-to-parent
  relationships are weak to avoid ownership cycles.
- Collection is non-moving, iterative, explicit-root mark/sweep. Each
  incremental call is bounded by caller-provided root, mark, and sweep operation
  counts.
- Allocation failure never invokes a hidden full collection. A full collection
  is an explicit safe-point operation.
- Destruction and structural unlinking occur at documented mutation barriers.
  Pending objects cannot be resurrected.
- GC never runs from an ISR, reads a hidden clock, starts a background thread,
  or owns hardware/safety services.

`UObject` is implemented by the Object candidate. This decision required the
later `UWorld`, `AActor`, and `UActorComponent` implementation to preserve
these identity, tracing, lifecycle, and bounded-work rules; the accepted Engine
candidate now does so.

## Implementation evidence

On 2026-07-19, the Object candidate at `e1e7b75` recorded evidence for handles,
descriptors, roots, object storage, and bounded incremental GC. That evidence
did not establish the Engine behavior implemented later. See
[ModulePackaging.md](../ModulePackaging.md); current implementation state and
Engine evidence belong in [PROGRESS.md](../../PROGRESS.md).

## Consequences

- Core remains suitable for the smallest and safety-sensitive applications.
- Managed resource use is exact and reviewable but requires capacity
  configuration.
- Equal slots may waste RAM; measurement precedes allocator sophistication.
- Manual reference visitation is simple but requires a behavior test for every
  managed class that stores traced references.
- External code must distinguish a traced pointer, weak observation, and
  explicit root.

## Alternatives considered

- **Mandatory GC:** rejected because it taxes applications that have no managed
  objects and weakens deterministic lifetime reasoning.
- **Conservative stack scanning:** rejected because roots and worst-case work
  would be hidden and toolchain-dependent.
- **Moving/compacting collector:** deferred because pointer updating and pause
  complexity are unjustified without a failed memory budget.
- **Reference counting as the object system:** rejected because unreachable
  cycles would leak and Actor graphs need explicit managed reachability.

## Revisit triggers

- Equal-slot internal fragmentation fails an accepted target budget.
- Manual tracing repeatedly causes defects across real managed classes.
- A target cannot store the generation width or worklist required by its
  accepted object count.
- A real concurrent managed application needs cross-task access.

A superseding decision must preserve explicit capacity, bounded work, stale-
handle safety, and deterministic non-GC hardware ownership.
