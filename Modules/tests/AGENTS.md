# MicroWorld Host Behavior Tests

Inherits `../AGENTS.md`.

## Architecture

`tests/` holds the host behavior-test tree for every system, kept outside
`MicroWorld/` so the portable library's recursive `srcDir` never compiles test
sources. Each system subdir is one test executable built by the superbuild. The
shared static-registration harness lives under `Core/` (`TestMain.cpp`,
`TestSupport.h`, allocation counters); other systems' tests include it.

## Concepts

- Tests act only through public APIs and assert observable outcomes.
- Explicit millisecond values replace wall-clock time and sleeps.
- Positive/negative and boundary pairs cover lifecycle, capacity, ownership,
  monotonic time, saturation, and independent schedules.
- Fakes record only the state required to prove behavior; they do not mirror
  private implementation.
- Production code never depends on this tree.

## Verification

Run CTest from a configured build (`ctest --test-dir build -C Release`). Each
system's tests are registered as `microworld_*_tests` targets and link the
matching `MicroWorld::*` alias.
