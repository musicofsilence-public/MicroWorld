# Explicit Memory Resources

Inherits `../AGENTS.md`.

## Architecture

`Memory/` owns the allocation boundary and fixed caller-owned resources used by
later explicit ownership types. Allocation policy stays separate from managed
object identity and collection.

## Concepts and boundaries

- Failure is returned as `EMemoryResult`; allocation never throws or silently
  falls back.
- `FMemoryBlock` preserves the exact address and size attributed to one
  resource.
- Fixed resources validate alignment, bounds, ownership, and active-allocation
  markers before changing usage.
- Resource state is bounded and stored in the resource object; no operation
  performs dynamic allocation.

## Verification

Compile the contract and fixed arena independently under C++17, strict
warnings, no exceptions, and no RTTI. Keep behavior tests for alignment,
exhaustion, reuse, malformed blocks, double free, and counters public and
focused.
