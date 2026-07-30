# Application Host Behavior Tests

Inherits `../../AGENTS.md`.

## Architecture

The Application test executable owns `FApplication` lifecycle sealing, the
`OnConfigure`/`OnBeginPlayFailed` hooks, the backward-clock guard, and the
`Run` frame-loop template.

## Concepts

Tests assert sealed forwarding, first-failure latch behavior, and deterministic
frame-loop termination through a controlled clock and sleep.

## Verification

Run the `microworld_application_tests` target via CTest.
