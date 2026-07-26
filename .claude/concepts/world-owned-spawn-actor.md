# World-Owned Generic Actor Spawning

## Problem

The current `UWorld::SpawnActor` only queues an already-constructed `AActor`.
The desired API is `World->SpawnActor<TActor>(...)`, with the world constructing
and queuing a concrete actor in one call.

That call cannot pass a function-local descriptor directly to `FObjectStore`:
the store accepts only the descriptor copy owned by the class registry. The
store currently has lookup-only registry access, while `UWorld` has no way to
register or obtain that canonical copy. A pointer-derived `FTypeId` would also
be unsafe because `FTypeId` is 32-bit and collisions must remain observable.

The registry owns no actor instances. A spawned actor lives in `UWorld`: the
world queues, registers, traces, starts, ticks, ends, and destroys it. The
object store only supplies its fixed memory slot; the registry only stores its
class metadata.

## Proposed Approach

Keep `UWorld` as the public owner of `SpawnActor<TActor>` and keep registry
storage in the application composition root (`TEngine` is only the default
one). Add a small non-owning registration view to
the Object package. The view can find the registry-owned descriptor for an
exact `ManagedObjectTypeToken<T>` or atomically register a direct `AActor`
subclass and return the registry's copy.

`UWorld` receives this explicit capability at construction. Its generic spawn
function only queues a fixed-capacity, type-erased construction request, so it
is safe from actor callbacks and other store-locked contexts. At the world's
next safe barrier, the request resolves the canonical descriptor, constructs
the actor in the world's object store, then registers, traces, and begins it.
`TEngine::CreateWorld` supplies the registry view and deferred-request storage;
direct Object/Engine compositions can supply both themselves.

The queued call reports only whether the request was accepted. It returns a
generation-checked spawn handle so callers can later observe completion and
resolve the actor after the barrier. A full queue rejects the request without
moving constructor arguments. If the object store remains locked because an
incremental GC cycle spans frames, the request is rejected before it captures
any arguments: enqueuing after World has been traced would otherwise let the GC
miss those captures. Requests admitted from ordinary actor callbacks are traced
by the World until their barrier runs. A construction failure becomes observable
through the handle without publishing an actor. A successful handle remains
reserved until its actor leaves the World, so it cannot become stale while the
actor is still live.

## Open Questions

None.

## Decisions Log

- 2026-07-25: Keep the public generic API on `UWorld` rather than moving it to
  `TEngine` - this satisfies the required call shape while the composition root
  still owns mutable registry state.
- 2026-07-25: A spawned actor belongs to its `UWorld` - the world retains its
  traced actor reference, while the object store supplies memory and the class
  registry contains metadata only.
- 2026-07-25: Immediate managed construction must not run while the object
  store is locked - runtime context cannot be checked by the C++ type system,
  so the generic API queues construction work rather than attempting it.
- 2026-07-25: Deferred actor spawning is required - callback-time calls queue
  bounded construction work and return a generation-checked handle rather than
  constructing immediately, so actor code can request new world-owned actors
  without racing dispatch or incremental collection.
- 2026-07-25: A request may not enter after World has already been traced by an
  active collector - it rejects before moving captures; spawned-handle slots
  remain reserved until World removes their actor, preventing live-handle ABA.
- 2026-07-25: Do not derive `FTypeId` by truncating a type-token address - a
  registry-local allocator can resolve collisions deterministically within its
  fixed capacity.
