# Host Behavior Tests

Inherits `../AGENTS.md`.

## Architecture

The test executable uses a static-registration harness and deterministic
fixed-capacity fakes. Tick tests own scheduling behavior, World tests own
registration/lifecycle order, and Application/Network tests own composition and
policy-boundary behavior. Production code never depends on this tree.

## Concepts

- Tests act only through public APIs and assert observable outcomes.
- Explicit millisecond values replace wall-clock time and sleeps.
- Positive/negative and boundary pairs cover lifecycle, capacity, ownership,
  monotonic time, saturation, and independent schedules.
- Fakes record only the state required to prove behavior; they do not mirror
  private implementation.

## Documentation and verification

Each test and helper function states the behavior or regression it exists to
prove. Persistent fake state explains the observation it enables; local values
use behavior-specific names instead of narration. Run CTest and the named test
executable, and keep warnings as errors.
