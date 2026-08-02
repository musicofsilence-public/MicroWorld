# MicroWorld Engine System

Inherits `../../AGENTS.md`.

## Architecture

Engine is the managed-runtime system: it owns object identity (the folded Object
store and garbage collector) and the actor lifetime built on it
(`UWorld` / `AActor` / `UActorComponent`). Its dependency direction is
`Core <- Engine`: it may depend only on Core and the C++17 standard library.

The system owns `UWorld`, `AActor`, `UActorComponent`, the object store, the
generation-checked handles, the bounded incremental collector, and a bounded
caller-time timer facility. Downward ownership is traced, parent observations
are weak, and applications own all fixed registration storage and roots. Engine
does not own networking, runtime spawn/destroy beyond the deferred barrier,
threads, platform adapters, or hardware APIs.

Object and Engine were separate packages; Object folded into Engine because
neither identity nor lifetime is a responsibility anything wants alone, and the
architecture model states them as one system.

## Concepts and boundaries

- Registration closes at `BeginPlay` and failures must leave ownership and
  registration unchanged.
- `UWorld` traces Actors; `AActor` traces Components; parent links are weak and
  expire when the parent is reclaimed. Engine creates no hidden roots.
- Begin and Advance use registration order; End uses reverse registration order.
  Components dispatch before their Actor during Begin and Advance.
- Caller-owned registries must outlive the objects they back. The application
  also owns the object store, root table, GC worklist, and World root.
- Constructors, destruction hooks, and reference visitors must not perform
  structural object-store or collection work.
- Each managed C++ type contributes one writable zero-initialized byte whose
  address is used only as no-RTTI type identity. Its value is not runtime state;
  writable storage deliberately prevents optimized identical-data folding.
- Managed identity is local process state, never a wire identity or platform
  handle.
- `TTimerManager` is a standalone value owned by the application. The
  application supplies every clock reading and decides when Advance is called
  relative to World dispatch. Timers hold no reference to `UWorld`, `AActor`, or
  `UActorComponent`. `Schedule` accepts only `OneShot` and `Looping` modes; every
  other `ETimerMode` value is rejected transactionally. Completed one-shots are
  cleared in place during dispatch and removed by a single stable post-dispatch
  compaction pass; `Cancel` outside dispatch remains one bounded linear removal.
- Keep portable code bounded, allocation-free in steady state, single-pass in
  dispatch, free of structural mutation during dispatch, and free of RTTI,
  exceptions, hidden clocks, threads, and SDK calls.
- `UWorld` is one type across several translation units: `World.cpp` holds the
  constructors/destructor and `StaticClassDescriptor`, and each remaining
  responsibility group lives in a flat `World_<Group>.cpp` partition
  (`World_ActorRegistration.cpp`, `World_Lifecycle.cpp`, `World_SpawnDestroy.cpp`,
  `World_GarbageCollection.cpp`). New `UWorld` methods join the matching group
  rather than growing `World.cpp`; the single `World.h` declaration list is
  unchanged.

## Verification

Build the engine from the repo root; Engine is the `microworld_engine` target
(links the folded Object sources). Run the dependency-boundary checker with the
Engine system root and the Engine behavior tests after changes. Keep Engine
absent from lower-system profiles. This guide owns durable boundaries; Engine's
headers and tests define its current behavior.
