# Application Program-Entry Headers

Inherits `../../AGENTS.md`.

## Architecture

The `Application/` headers define the program-entry contract in dependency
order: `Application.h` binds one `IEngine&` and seals the begin/tick/end
forwarding behind private non-virtual methods, leaving `OnConfigure` as the only
hook that can reach the world, and `ApplicationRunner.h` composes a clock, a sleep function,
and a frame period into the begin-then-advance-then-sleep loop.

## Concepts and boundaries

- `FApplication`'s public `BeginPlay`/`Advance`/`EndPlay` keep the lifecycle
  guard and monotonic-time checks; their work forwards to the bound engine
  through private `final` methods, so a subclass can only affect the world
  through `OnConfigure`.
- `OnConfigure(IEngine&, TimePoint)` is the only override point that reaches the
  world and runs once before `IEngine::BeginPlay`; a subclass spawns actors and
  configures systems there, never by intercepting per-frame ticks. It defaults to
  success, so an application with nothing to configure omits it entirely.
- `TApplicationRunner<TimeSourceType>` is a template so a platform names its
  concrete clock; the runner holds the clock by reference (it must outlive the
  runner) and the sleep function pointer and frame period by value.
