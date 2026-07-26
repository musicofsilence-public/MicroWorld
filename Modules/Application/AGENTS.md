# MicroWorld Application Package

Inherits `../AGENTS.md`.

## Architecture

`microworld-application` is the program-entry package: it owns the one class
that holds an engine for an application's lifetime and the bounded frame loop
that drives it. Its dependency direction is `Core <- Object <- Engine <-
Application`: Application may depend on Core, Object, and Engine, plus the
C++17 standard library. It must not depend on Net directly — a networked
application receives its already-bound engine the same way a standalone one
does.

The package owns `FApplication` (a base class that binds one `IEngine&`, seals
the begin/tick/end forwarding, and supplies the begin-then-advance-then-sleep
`Run` member template a platform entry point would otherwise hand-roll). It
does not own the engine, the world, actors, components, networking, a clock, or
a sleep implementation — the clock and sleep arrive from the caller.

## Concepts and boundaries

- `FApplication` takes `IEngine&` at construction and never rebinds it. The
  per-frame `BeginPlay`/`Advance`/`EndPlay` calls are public but their work is
  sealed: private non-virtual forwarders call `IEngine::BeginPlay`/`Tick`/`EndPlay`.
- A subclass must override `OnBeginPlayFailed`, the rollback hook on the failure
  path, and may override `OnConfigure(IEngine&, TimePoint)`, which runs once
  during `BeginPlay` before the engine begins so a subclass spawns actors and
  configures systems into a world that exists but has not started. `OnConfigure`
  defaults to success, so an application with nothing to configure writes no body.
- `BeginPlay` calls `OnConfigure` first and `IEngine::BeginPlay` second with
  the same timestamp and returns the first failure; a failed configure fires
  `OnBeginPlayFailed` and latches the lifecycle terminal.
- `Advance` still rejects a backward clock before the engine sees it; the
  engine receives only monotonic-or-equal timestamps.
- `FApplication::Run` is a member template so a platform names its concrete
  clock; it takes the clock by reference plus a `noexcept` sleep function
  pointer and a frame period, then runs until a frame fails.
- Portable code uses fixed-width/value types, deterministic lifetimes, and no
  RTTI, exceptions, logging, threads, clocks, heap containers, SDK calls, or
  global mutable state.

## Verification

Configure and build this package independently with CMake, compile its public
headers under C++17 with strict warnings, exceptions disabled, and RTTI
disabled, run the dependency-boundary checker with an Application package
root, and run the package tests required by the current package scope. This
guide owns durable boundaries; the package's headers and tests define its
current behavior.
