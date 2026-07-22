# MicroWorld Fixed Delegates

Inherits `../AGENTS.md`.

## Architecture

`Delegates/` owns allocation-free callable erasure and fixed-capacity multicast
dispatch for Memory and inward-dependent modules. It depends only on the C++17
standard library and introduces no platform, engine, or product policy.

## Concepts and invariants

- A delegate stores its callable entirely inside caller-selected inline
  capacity and rejects unsupported size or alignment before construction.
- Multicast storage and dispatch work are bounded by compile-time capacity.
- Handles use slot index plus generation; exhausted generations retire their
  slot so an old handle can never become valid again.
- Broadcast order is insertion order. Mutation and nested broadcast are
  rejected while iteration is active.
- Multicast delegates support `void` signatures and preserve arguments for
  every binding. Value arguments must be nothrow copy constructible because
  each binding receives a copy inside `Broadcast`'s noexcept boundary;
  rvalue-reference signatures are unsupported.

## Verification

Compile `Delegate.h` independently under C++17 with strict warnings, exceptions
disabled, and RTTI disabled. Probe callable lifetime, layout rejection,
capacity, deterministic order, stale handles, and active-broadcast mutation.
