# MicroWorld Core Package

Inherits `../AGENTS.md`.

## Architecture

Core is the released platform-neutral lifecycle and tick package. `FApplication`
guards a consumer composition root, `FTickFunction` owns bounded per-object
scheduling, and `FLifecycleGuard` with the `FTickable` contract expresses the
forward-only begin/tick/end lifecycle. Consumers own concrete objects. Core
retired its own World/Actor/Component model in the Phase 1 consolidation; the
managed Engine package is the sole Actor model.

Memory, Object, and Engine are adjacent packages above Core. Their current
acceptance state and next milestone live only in `PROGRESS.md`; this guide owns
durable Core boundaries rather than volatile roadmap sequencing.

## Concepts

- Keep lifecycle and tick paths bounded, single-pass, allocation-free, and
  free of structural mutation during dispatch.
- Use caller-supplied monotonic time and typed results. Core never logs,
  throws, reads hardware, or defines product policy.
- Keep vendor SDK, RTOS, radio, valve, and tutorial dependencies out.
- Preserve C++17, explicit ownership/failure, public API documentation,
  formatting, and behavior tests.
- `PROGRESS.md` is the sole live MicroWorld status record.

## Verification

Build Core, run its behavior tests and scoped checks, and compile relevant
downstream consumers after a boundary or public-contract change.
