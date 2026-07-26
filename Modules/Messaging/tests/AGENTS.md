# Messaging Tests

Inherits `../AGENTS.md`.

## Architecture

Messaging host tests use the shared Core test harness from
`Modules/Core/tests/TestMain.cpp` and `TestSupport.h`. One test executable
links Messaging plus the existing Engine allocation-counter and Engine/Net
host-loopback support needed to exercise the transport-agnostic boundary; it
aggregates behavior files through `RunAllTests()`.

## Concepts and boundaries

- Tests prove observable contracts: codec round trips and rejection paths,
  queued routing and handler selection, channel result mapping, deterministic
  frame order, and bounded reliable-delivery behavior.
- Channel and reliable-delivery tests use deterministic Net host-loopback and
  Engine support only as test fixtures. Messaging production headers still
  name Core types only, so those test-only links do not expand the package's
  public dependency boundary.
- Tests never claim target runtime behavior; they prove host-side correctness
  of portable message-layer policies.
