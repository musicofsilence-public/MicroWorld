# MicroWorld Engine Package

Inherits `../AGENTS.md`.

## Architecture

Engine is the managed-runtime layer above Object: `Core <- Object <- Engine`.
It may depend only on those inward packages and the C++17 standard library.

The package owns `UWorld`, `AActor`, `UActorComponent`, and a bounded
caller-time timer facility. Downward ownership is traced, parent observations
are weak, and applications own all fixed registration storage and roots.
Engine does not own networking, runtime spawn/destroy, threads, platform
adapters, or hardware APIs.

## Concepts and boundaries

- Registration closes at `BeginPlay` and failures must leave ownership and
  registration unchanged.
- `UWorld` traces Actors; `AActor` traces Components; parent links are weak and
  expire when the parent is reclaimed. Engine creates no hidden roots.
- Begin and Advance use registration order; End uses reverse registration
  order. Components dispatch before their Actor during Begin and Advance.
- Caller-owned registries must outlive the objects they back. The application
  also owns the object store, root table, GC worklist, and World root.
- Constructors, destruction hooks, and reference visitors must not perform
  structural object-store or collection work.
- `TTimerManager` is a standalone value owned by the application. The
  application supplies every clock reading and decides when Advance is called
  relative to World dispatch. Timers hold no reference to `UWorld`, `AActor`, or
  `UActorComponent`, and dispatch of one does not trigger the other. `Schedule`
  accepts only `OneShot` and `Looping` modes; every other `ETimerMode` value is
  rejected transactionally. `FTimerHandle` is a {slot index, generation} pair
  local to the issuing manager. Completed one-shots are cleared in place during
  dispatch and removed by a single stable post-dispatch compaction pass; `Cancel`
  outside dispatch remains one bounded linear removal.
- Keep portable code bounded, allocation-free in steady state, single-pass in
  dispatch (one post-dispatch compaction, not one shift per fired timer), free
  of structural mutation during dispatch, and free of RTTI, exceptions, hidden
  clocks, threads, and SDK calls.
- Actor messaging (`Message.h`, `TMessageRouter`, `TMessageChannelBinding`,
  `TEngineSystemSet`, `TReliableChannel`) is header-only and Net-free: a
  channel binding is duck-typed on its net type, so Engine names no Net type
  and keeps zero dependency on Net.

## Verification

Build the package and standalone consumer with C++17, strict warnings,
exceptions disabled, and RTTI disabled. Run Engine behavior tests and the
dependency/profile checks after changes. Keep Engine absent from lower-package
profiles. Live status and evidence belong only in
`../../PROGRESS.md`.
