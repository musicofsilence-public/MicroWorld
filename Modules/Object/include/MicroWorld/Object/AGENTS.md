# Managed Object Contracts

Inherits `../../AGENTS.md`.

## Architecture

`Object/` owns no-RTTI class descriptors, generation-checked handles, traced
and weak references, explicit roots, fixed storage, and incremental GC
contracts. Handle resolution and collection must use caller-owned bounded
storage and explicit results.

## Concepts

Stable handles identify local object lifetimes, roots retain reachability, and
descriptor-visible references drive explicit iterative tracing.

## Invariants

- Managed references never imply an implicit root.
- Collection work is iterative and bounded by caller-provided budgets.
- A stale generation never resolves to a reused object slot.
- Object lifetime does not include ISR, hardware-driver, watchdog, key-store,
  or fail-closed policy lifetimes.

## Verification

Compile headers without exceptions or RTTI. Behavior tests cover roots, cycles,
stale handles, bounded work, and deferred destruction through public contracts.
