# MicroWorld Core System

Inherits `../../AGENTS.md`.

## Architecture

Core is the released platform-neutral lifecycle and tick foundation. `FLifecycleGuard`
with the `FTickable` contract expresses the forward-only begin/tick/end lifecycle,
`FTickFunction` owns bounded per-object scheduling, and the folded memory surface
(`Containers/`, `Delegates/`, `Memory/`, `IO/`) provides the fixed-capacity
primitives every higher system reuses. Core retired its own World/Actor/Component
model; the managed Engine system is the sole Actor model.

Engine, Messaging, Transport, Networking, and Application are the systems above
Core; Memory is folded into Core and is no longer a separate system. This guide
owns durable Core boundaries rather than volatile roadmap sequencing — next work
lives in `../../docs/RADIO_TRANSPORTS_ROADMAP.md`.

Headers and sources sit side by side under this directory; the sub-namespaces
`Containers/`, `Delegates/`, `Memory/`, and `IO/` are Core's folded segments and
resolve to Core in the dependency-boundary checker.

## Concepts

- Keep lifecycle and tick paths bounded, single-pass, allocation-free, and
  free of structural mutation during dispatch.
- Use caller-supplied monotonic time and typed results. Core never logs,
  throws, reads hardware, or defines product policy.
- Keep vendor SDK, RTOS, radio, valve, and tutorial dependencies out.
- Preserve C++17, explicit ownership/failure, public API documentation,
  formatting, and behavior tests.
- Core's headers and tests are the sole record of its current behavior.

## Verification

Build the engine from the repo root (`cmake -S . -B build`); Core is the
`microworld` target. Run the dependency-boundary checker with the Core system
root and the Core behavior tests after a boundary or public-contract change.
