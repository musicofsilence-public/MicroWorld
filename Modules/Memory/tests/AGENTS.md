# Memory Package Behavior Tests

Inherits `../AGENTS.md` and `../../Core/tests/AGENTS.md`.

## Architecture

These host tests consume only the public Memory package headers and the shared
Core `TestSupport.h` harness. `MemoryTests.cpp` owns allocation, explicit
ownership, bounded-container, and span behavior. `DelegateTests.cpp` owns
single-cast and multicast delegate behavior. Production code never depends on
this directory.

## Concepts and boundaries

- Tests use fresh caller-owned storage, local observation state, and no heap
  fixture whose behavior could hide a bounded-resource failure.
- Resource fakes record only public allocation/deallocation requests and exact
  returned blocks; they do not expose arena or pointer internals.
- Reference-count limits are reached through `TryShare`, `TryAcquireWeak`, and
  related public diagnostics rather than private control-block access.
- Delegate tests observe binding lifetime, result codes, callback order, and
  handle validity without inspecting slots or erased callable storage.

## Verification

Compile with C++17, strict warnings as errors, exceptions disabled, and RTTI
disabled. Run the shared host test executable and available host sanitizers.
Keep every state-changing Act paired with a direct public postcondition.
