> Historical note (2026-07-22): This roadmap was migrated from the `lora` firmware
> repository into the dedicated `MicroWorld` repository and moved to `docs/ROADMAP.md`.
> Paths inside the body refer to the historical pre-migration layout (`lib/microworld-*`
> in the `lora` repo). The body is preserved verbatim as a historical record; do not
> rewrite the references in place — they describe the layout the plan was authored against.

# MicroWorld — Review, Refactor & Implementation Plan

**Version:** 1.1 · **Date:** 2026-07-20 · **Owner:** Mykola
**Scope:** the MicroWorld package family under `lib/` (`microworld`, `microworld-memory`, `microworld-object`, `microworld-engine`, `microworld-net`).

This document is the **master improvement plan and progress tracker** for turning the
current MicroWorld candidate into a production-ready micro engine with UE5-like
concepts. It is written so that any LLM (including a weak one) can pick it up,
find the next task, complete it, and record progress without extra context.

`lib/microworld/PROGRESS.md` remains the evidence record (test counts, image
sizes, benchmark facts). **This file tracks the plan; PROGRESS.md tracks the
proof.** When a phase is accepted, update both.

---

## 1. How to use this document (protocol for LLM workers)

Follow these rules exactly:

1. Read section **2 (Ground rules)** before touching any code.
2. Open section **7 (Progress tracker)**. Find the first phase whose status is
   not ✅. Inside that phase, find the first unchecked `[ ]` task.
3. Work on **exactly one task at a time**. Do not start a later phase while an
   earlier phase has unchecked tasks. Tasks inside one phase must be done in
   order unless marked `(parallel-ok)`.
4. Every task has a **Done when** checklist and a **Verify** command block.
   A task is complete only when every "Done when" item is true and every
   "Verify" command passes.
5. When a task is complete: change its `[ ]` to `[x]` in this file, and update
   the phase status in the tracker table (⬜ → 🟨 when the first task of a phase
   starts, 🟨 → ✅ when the last task finishes).
6. When a phase reaches ✅: add one short evidence entry to
   `lib/microworld/PROGRESS.md` (what was built, how it was verified).
7. If you are blocked, mark the task `⛔ BLOCKED:` with one sentence explaining
   why, directly under the task title, and stop. Do not skip ahead.
8. Never delete or rewrite this document's structure. Only update statuses,
   checkboxes, and BLOCKED notes.

Status legend: ⬜ not started · 🟨 in progress · ✅ done · ⛔ blocked

---

## 2. Ground rules (invariants — never violate)

These apply to every task in this plan. They come from `lib/AGENTS.md` and the
existing code style, and they are what makes MicroWorld embedded-suitable.

- **C++17**, builds with exceptions and RTTI **disabled**. No `throw`, no
  `dynamic_cast`, no `typeid`.
- **No hidden allocation.** Steady-state code paths never call `new`/`malloc`.
  All storage is caller-owned, fixed-capacity, and compile-time bounded
  (`std::array`, template capacity parameters). Startup-time allocation is
  allowed only inside explicit `IMemoryResource` implementations.
- **No hidden clock.** Time is always caller-supplied
  `TimePointMilliseconds` (u64 ms). Only platform adapter packages may read
  real clocks.
- **Errors are enums, not exceptions.** Every fallible operation returns a
  result enum (`ERuntimeResult`, `EObjectResult`, `EEngineResult`,
  `ETimerResult`, `ENetResult`, `EDelegateResult`). Failures are
  **transactional**: a failed call leaves all inputs and state unchanged.
- **Determinism.** Registration order defines dispatch order. Begin/tick run in
  registration order; shutdown runs in reverse order. Components begin and tick
  before their Actor; Actors end before their Components. No catch-up bursts:
  a late tick runs once.
- **Naming:** `F` plain class/struct, `T` template, `E` enum, `I` interface,
  `b` bool prefix, `U`/`A` reserved for managed (`UObject`-derived) types.
- **Every function declaration and persistent member gets a doc comment**
  explaining intent/invariant/ownership (enforced by
  `lib/microworld/tools/CheckClassDocumentation.py`).
- **Format** with `clang-format --style=file:clang-format` (repo root policy
  file).
- **Dependency direction** (must stay acyclic; enforced by
  `lib/microworld/tools/CheckDependencyBoundaries.py`):

```
microworld-memory  →  (nothing)
microworld (Core)  →  (nothing)
microworld-object  →  Core, Memory
microworld-engine  →  Core, Memory, Object
microworld-net     →  Core, Memory
platform adapters  →  anything above, never the reverse
```

- **Every new public behavior needs host tests** in the owning package's
  `tests/` directory, wired into its CMake + CTest.
- Building never flashes hardware. Uploading/running on a board requires
  explicit human authorization (see root `AGENTS.md` safety rules).

---

## 3. Review verdict — what exists today

Reviewed at commit state of 2026-07-20. Quality is generally **high**: strict
warnings, transactional failure semantics, generation-checked handles, real
test suites, zero steady-state allocation. The problems are **duplication**,
**missing must-have features**, and **no platform layer** — not code quality.

### 3.0 Codebase map (navigation reference for workers)

Every public type and where it lives. Consult this before searching.

**`lib/microworld` (Core) — `include/MicroWorld/`**

| File | Types | Purpose |
| --- | --- | --- |
| `Time.h` | `TimePointMilliseconds` (u64), `DurationMilliseconds` (u32), `FTickContext`, `ERuntimeResult`, `FTickDecision` | Canonical time + shared result enum |
| `Lifecycle.h` | `ELifecycleState`, `FLifecycleGuard` | Forward-only lifecycle state machine (Constructed→Playing→Ended, Failed terminal) |
| `TickFunction.h` / `src/TickFunction.cpp` | `FTickConfiguration`, `FTickFunction` | One bounded per-object schedule; monotonic validation; at most one due tick |
| `Tickable.h` | `FTickable` | Mix-in giving a type one primary tick |
| `Application.h` / `src/Application.cpp` | `FApplication` | Abstract composition root (BeginPlay/Advance/EndPlay hooks) |
| `Version.h` | version constants | |
| `World.h`, `Actor.h`, `ActorComponent.h`, `Network.h` + `.cpp` | `TWorld`, `FActorBase`, `TActor`, `FActorComponent`, `FNetwork` | **RETIRED in Phase 1 — do not build on these** |

Tests: `tests/TickFunctionTests.cpp` (keep), `WorldTests.cpp` + `ApplicationNetworkTests.cpp` (retire in 1.3). Checker scripts: `tools/*.py`.

**`lib/microworld-memory` — `include/MicroWorld/`**

| File | Types | Purpose |
| --- | --- | --- |
| `Containers/Span.h` | `TSpan<T>` | Non-owning pointer+size view |
| `Containers/StaticVector.h` | `TStaticVector<T,N>` | Fixed-capacity vector, no heap |
| `Delegates/Delegate.h` | `TDelegate<Sig,InlineBytes>`, `TMulticastDelegate<Sig,MaxBindings,InlineBytes>`, `FDelegateHandle`, `EDelegateResult` | Bounded inline-storage callbacks + events |
| `Memory/MemoryResource.h` / `src/…cpp` | `IMemoryResource`, `FMemoryBlock` | Explicit allocation interface |
| `Memory/FixedArena.h` | `TFixedArena<Bytes>` | Caller-owned arena resource |
| `Memory/UniquePtr.h` | `TUniquePtr<T>` | Resource-bound unique ownership |
| `Memory/SharedPtr.h` | `TSharedPtr<T>`, `TWeakPtr<T>`, `MakeShared` | Resource-bound shared ownership |

**`lib/microworld-object` — `include/MicroWorld/Object/`**

| File | Types | Purpose |
| --- | --- | --- |
| `ObjectHandle.h` | `FObjectHandle` {index,generation}, `EObjectResult`, `FObjectId` | Generation-safe local identity |
| `ClassDescriptor.h` | `FClassDescriptor`, `TClassRegistry<MaxClasses>`, type tokens | No-RTTI type identity, ancestry, exact destruction |
| `Object.h` | `UObject` | Managed base: store identity, `VisitReferences`, `BeginDestroy` |
| `ObjectPtr.h` | `TObjectPtr<T>` (traced), `TWeakObjectPtr<T>` (observing), `TStrongObjectPtr<T>` (explicit root) | The three managed reference kinds |
| `ObjectStore.h` / `src/…cpp` | `FObjectStore`, `FObjectStoreStorage`, `TObjectCreationResult<T>`, `NewObject<T>`, `Resolve`, `MarkPendingDestroy`, roots API, dispatch guard | Fixed-slot object store |
| `GarbageCollector.h` / `src/…cpp` | `FGarbageCollector`, `FGarbageCollectionBudget/Result/Stats`, `EGarbageCollectionPhase`, `FReferenceCollector` | Budgeted incremental mark/sweep: `RequestCollection()` → `Advance(Budget)`; `CollectFull()`, `CancelCollection()` |

**`lib/microworld-engine` — `include/MicroWorld/Engine/`**

| File | Types | Purpose |
| --- | --- | --- |
| `EngineResult.h` | `EEngineResult` | Registration outcomes (adds CrossStore, InvalidReference) |
| `EngineClassIds.h` | `UWorldClassId`, `AActorClassId`, `UActorComponentClassId` | Stable type ids for base descriptors |
| `EngineStorage.h` | `FActorComponentRegistry<N>`, `FWorldActorRegistry<N>` | Caller-owned registry storage, one-shot `MakeView()` lease |
| `EngineRegistryView.h` | `FActorComponentRegistryBase`, `FWorldActorRegistryBase` | Move-only leases; only AActor/UWorld may use them |
| `World.h` / `src/World.cpp` | `UWorld` | Managed world; traces actors; gains Spawn/Destroy in Phase 2 |
| `Actor.h` / `src/Actor.cpp` | `AActor` | Managed actor; traces components; weak world parent |
| `ActorComponent.h` / `src/…cpp` | `UActorComponent` | Managed component; weak actor parent |
| `Timer.h` | `TTimerManager<MaxTimers,InlineCallbackBytes>`, `FTimerHandle`, `ETimerMode` (OneShot/Looping only), `ETimerResult` | Bounded timers, caller time |

Tests: `EngineLifecycleTests`, `EngineRegistrationTests`, `EngineGarbageCollectionTests`, `EngineTimerManagerTests`, `EngineAllocationCounters`.

**`lib/microworld-net` — `include/MicroWorld/Net/`**

| File | Types | Purpose |
| --- | --- | --- |
| `NetResult.h` | `ENetResult` {Success, Full, Invalid, Unavailable} | Normalized: Full = no capacity now; Invalid = never succeeds; Unavailable = retry later |
| `ByteWriter.h` / `ByteReader.h` | `FByteWriter`, `FByteReader` | Bounded LE serialization over caller spans |
| `NetDriver.h` / `src/…cpp` | `INetDriver`, `FNetReceiveResult` | Non-blocking transport; gains addressing in Phase 4 |
| `NetPacketStorage.h` | `FNetPacketStorage<MaxPackets,MaxPacketBytes>` | Caller-owned FIFO backing |
| `NetManager.h` | `FNetManager<MaxPackets,MaxPacketBytes>` | Outbound FIFO (head retained on failure) + direct receive |
| `HostLoopback.h` | `FHostLoopback` | Deterministic in-process driver for tests |

### 3.1 KEEP AS-IS (good, do not rewrite)

| Area | What | Why it stays |
| --- | --- | --- |
| Core primitives | `FTickFunction`, `FTickable`, `FLifecycleGuard`, `FApplication`, `Time.h` (`FTickContext`, `ERuntimeResult`) | Clean, tested, the foundation everything else already uses. Monotonic-time validation and forward-only lifecycle are exactly right for MCUs. |
| Memory | `TFixedArena`, `IMemoryResource`, `TUniquePtr`, `TSharedPtr`/`TWeakPtr`, `TStaticVector`, `TSpan`, `TDelegate`, `TMulticastDelegate` | Complete, bounded, ASan/UBSan-tested. Multicast delegates already exist — reuse them for engine events, do not invent a second event system. |
| Object / GC | `UObject`, `FObjectHandle` (index+generation), `FClassDescriptor` + `TClassRegistry` (no-RTTI type identity), `FObjectStore` (caller-owned slots), `TObjectPtr`/`TWeakObjectPtr`/`TStrongObjectPtr`, bounded incremental `FGarbageCollector` | This is the hardest part of a mini-UE and it is already done well: generation-safe handles, budgeted mark/sweep, no reflection, no RTTI, caller-owned storage. |
| Engine | `UWorld`, `AActor`, `UActorComponent`, `TTimerManager` | Correct managed layer; matches UE5 mental model. Gains spawn/destroy in Phase 2 but the lifecycle/dispatch core is sound. |
| Net building blocks | `FByteWriter`/`FByteReader`, `ENetResult`, `FNetManager` FIFO discipline, `FHostLoopback` | Solid bounded serialization and queue semantics. Extended (not replaced) in Phase 4. |
| Process | AGENTS.md guides, doc-comment rule, checker scripts in `lib/microworld/tools/`, per-package tests/benchmarks/Results structure | This discipline is why the code is trustworthy. Keep enforcing it. |

### 3.2 REFACTOR (works, but wrong shape for the goal)

| # | Problem | Decision |
| --- | --- | --- |
| R1 | **Two parallel Actor models.** Core's `TWorld`/`TActor`/`FActorBase`/`FActorComponent` duplicate Engine's `UWorld`/`AActor`/`UActorComponent`. Two ways to do everything = double docs, double tests, permanent confusion. | **Retire the Core actor model** (Phase 1). Engine layer becomes the only user-facing World/Actor/Component API. Core keeps only shared primitives. (Owner decision, 2026-07-20.) |
| R2 | **`FNetwork` in Core** (`microworld/include/MicroWorld/Network.h`) is a second, unrelated "network" concept that overlaps `microworld-net`. | Delete together with R1 (Phase 1). `microworld-net` is the only networking layer. |
| R3 | **Registry-lease ceremony.** Creating one actor requires a separate caller-owned `FActorComponentRegistry<N>` object plus a `MakeView()` lease passed into the constructor. Correct, but verbose and hostile to runtime spawning. | Keep the lease mechanism internally; add `TInlineActor<N>` / `TInlineWorld<N>` convenience templates that own their registry storage (Phase 2). |
| R4 | **Registration frozen at BeginPlay.** No runtime spawn/destroy — a static framework, not an engine. | Implement bounded deferred spawn/destroy (Phase 2). (Owner decision: must-have.) |
| R5 | **`INetDriver` has no addressing.** It models one point-to-point link, so a server can never tell peers apart — client/server roles are impossible on top of it. | Extend driver interface with an opaque `FNetAddress` (Phase 4). Breaking change, accepted while consumers are few. |
| R6 | **No composition recipe.** Wiring store + registry + roots + GC budget + timers + world takes ~60 lines of expert boilerplate in every app. | Add one `TEngineHost` composition template with a canonical frame order (Phase 3). |
| R7 | Docs describe the retired split (`UE5ConceptMap.md`, READMEs, `PROGRESS.md`). | Refresh at the end of each phase; final sweep in Phase 6. |

### 3.3 IMPLEMENT (missing must-haves)

| # | Missing | Delivered by |
| --- | --- | --- |
| M1 | Runtime `SpawnActor` / `DestroyActor` with deterministic deferred application | Phase 2 |
| M2 | One-call engine composition (`TEngineHost`) with canonical frame order incl. GC budget slot | Phase 3 |
| M3 | Minimal logging facade (`MW_LOG`) with compile-time level stripping | Phase 3 |
| M4 | Network roles: `ENetMode` (Standalone / Client / ListenServer / DedicatedServer), bounded peer table, hello/heartbeat/timeout session control, channel-based message send/receive. **Simple messages, not UE replication.** | Phase 4 |
| M5 | Real transports: UDP driver (host + ESP32) first, E32 LoRa UART driver second | Phase 5 |
| M6 | Platform adapter package for ESP32 (time source, app_main glue) | Phase 5 |
| M7 | End-to-end examples (ESP32 device app; two-node client/server demo) + measured runtime margins | Phase 6 |

### 3.4 Explicitly OUT OF SCOPE (do not build, even if asked nicely)

Rendering, physics, audio, assets, editor tooling, reflection generation,
UE-style property replication/RPC, threads/task graph, tick groups,
runtime component add/remove on live actors (components are fixed at spawn),
universal hardware APIs. If a future need appears, it gets its own plan first.

---

## 4. Target architecture (after this plan)

```
┌─────────────────────────────────────────────────────────────┐
│ Application (user code)                                     │
│   TEngineHost<...> owns: ClassRegistry, ObjectStore storage,│
│   GC + budget, UWorld root, TTimerManager, TNetHost         │
└──────────────┬──────────────────────────────────────────────┘
               │ Tick(nowMs) — one canonical frame
┌──────────────▼──────────────┐  ┌───────────────────────────┐
│ microworld-engine           │  │ microworld-net            │
│ UWorld / AActor /           │  │ TNetHost (roles, peers,   │
│ UActorComponent,            │  │ channels) over INetDriver │
│ Spawn/Destroy, TTimerManager│  │ ByteWriter/Reader         │
└──────┬──────────┬───────────┘  └─────────────┬─────────────┘
┌──────▼────┐ ┌───▼──────────────┐             │
│ microworld│ │ microworld-object│             │
│ (Core:    │ │ UObject, store,  │             │
│ tick/life-│ │ handles, GC      │             │
│ cycle/time│ └───┬──────────────┘             │
└──────┬────┘     │      ┌─────────────────────▼───┐
┌──────▼──────────▼──┐   │ platform adapters       │
│ microworld-memory  │   │ esp32: time, UDP, LoRa  │
│ arena, ptrs, spans,│   │ host: time, UDP,        │
│ delegates          │   │ loopback                │
└────────────────────┘   └─────────────────────────┘
```

Frame order inside `TEngineHost::Tick(now)` (fixed, documented, tested):

1. `NetHost.PumpReceive(now)` — drain driver, dispatch messages, update peers
2. `Timers.Advance(now)` — fire due timer callbacks
3. `World.Advance(now)` — components tick, then actors
4. `World.ApplyPending(now)` — pending spawns begin play; pending destroys end play, are removed from the live set, and are marked for destruction on the store
5. Reclamation slice — `Store.ApplyPendingDestroy(Budget)`: runs `BeginDestroy` + the exact destructor for the actors/components step 4 marked, freeing their slots. Bounded and caller-driven, like the GC slice. (Decision 2026-07-21, option a: the incremental GC's sweep intentionally skips pending-destroy slots, so this explicit step — not GC mark/sweep — is what reclaims destroyed actors.)
6. GC slice — `RequestCollection()` when idle (policy: every tick), then `Advance(Budget)` — reclaims other unreachable objects
7. `NetHost.PumpSend(now)` — flush outbound FIFO, heartbeats

---

## 5. Phases and tasks

### Phase 0 — Baseline & governance ⬜

Goal: prove the current state builds and passes everywhere before changing it,
and make this document the recognized plan.

- [x] **0.1 Record a green baseline.** Build and run host tests for all five
  packages. Fix nothing yet; if something fails, record it under the task as a
  note and create a `⛔ BLOCKED` marker only if a later task depends on it.

  **Verify:**
  ```sh
  cmake -S lib/microworld -B build/host-core && cmake --build build/host-core && ctest --test-dir build/host-core --output-on-failure
  cmake -S lib/microworld-memory -B build/host-mem && cmake --build build/host-mem && ctest --test-dir build/host-mem --output-on-failure
  cmake -S lib/microworld-object -B build/host-obj && cmake --build build/host-obj && ctest --test-dir build/host-obj --output-on-failure
  cmake -S lib/microworld-engine -B build/host-eng && cmake --build build/host-eng && ctest --test-dir build/host-eng --output-on-failure
  cmake -S lib/microworld-net -B build/host-net && cmake --build build/host-net && ctest --test-dir build/host-net --output-on-failure
  ```
  **Done when:** all five test suites pass (or failures are recorded here as notes).

  **Baseline result — recorded 2026-07-20** (Windows 11; MinGW-w64 UCRT g++
  16.1.0; CMake/CTest 4.0.2; Ninja 1.13.2; Python 3.11.9). CMake's default
  generator on this host is multi-config `Visual Studio 17 2022`, which does not
  satisfy the single-config `ctest --test-dir` form used above, so each configure
  step was run with `-G Ninja -DCMAKE_CXX_COMPILER=g++` appended (plus a harmless,
  ignored `-DCMAKE_C_COMPILER=gcc`) — single-config GCC 16, the strict-warnings
  toolchain already documented in PROGRESS.md. Otherwise the Verify commands were
  run unchanged.

  | Package | Configure + build | Tests | Result |
  | --- | --- | --- | --- |
  | Core (`build/host-core`) | ok | 5/5 passed | ✅ |
  | Memory (`build/host-mem`) | ok | 1/1 passed | ✅ |
  | Object (`build/host-obj`) | ok | 1/1 passed | ✅ |
  | Engine (`build/host-eng`) | **failed** | not reached | 🚫 |
  | Net (`build/host-net`) | **failed** | not reached | 🚫 |

  All five *production* libraries compile; only the two test executables fail to
  build, both inside allocation-counter test support:

  - **Engine + Net — `std::aligned_alloc` is not a member of `std`**
    (`lib/microworld-engine/tests/EngineAllocationCounters.cpp:29`,
    `lib/microworld-net/tests/NetAllocationCounters.cpp:29`). The `AllocateAligned`
    helper has an MSVC branch (`_aligned_malloc`) and a POSIX branch
    (`std::aligned_alloc`); MinGW-w64 UCRT takes the POSIX branch, but its
    libstdc++ does not declare `std::aligned_alloc` (UCRT has no C11
    `aligned_alloc`). Platform gap in test-support code, not production code.
  - **Net — `-Werror=unused-variable` on `ReadDestination`**
    (`lib/microworld-net/tests/NetAllocationTests.cpp:42`): declared but never
    used; GCC 16.1.0 strict warnings reject it.

  Not fixed — task 0.1 is record-only. PROGRESS.md evidence for Engine/Net cites
  GCC/Clang + ASan/UBSan runs (consistent with Linux, where `std::aligned_alloc`
  exists), so this is most likely a Windows/MinGW-only baseline gap.

  ⛔ BLOCKED → **Engine portion RESOLVED 2026-07-20; Net portion RESOLVED 2026-07-21.**
  The Engine suite was fixed during the merged 1.2+1.3 step by broadening the
  `AllocateAligned` guard in `EngineAllocationCounters.cpp` from `_MSC_VER` to
  `_WIN32` (MinGW then uses `_aligned_malloc`); `build/host-eng` now builds and
  passes here. The **Net** (`build/host-net`) suite was fixed the same way on
  2026-07-21: broadened the `AllocateAligned`/`FreeAligned` guards in
  `NetAllocationCounters.cpp` from `_MSC_VER` to `_WIN32` (MinGW then uses
  `_aligned_malloc` instead of the absent `std::aligned_alloc`), and removed
  the unused `ReadDestination` variable in `NetAllocationTests.cpp` that tripped
  `-Werror=unused-variable`. `build/host-net` now builds and passes 52/0 here,
  unblocking **4.1–4.4**.

- [x] **0.2 Register this plan in governance docs.** Edit `AGENTS.md` (root)
  and `lib/AGENTS.md`: state that `MICROWORLD_ROADMAP.md` is the improvement
  plan and task tracker, and `lib/microworld/PROGRESS.md` remains the evidence
  record. One or two sentences each, no other changes.

  **Done when:** both files mention this document; no other content changed.

---

### Phase 1 — Consolidation: one Actor model ✅

Goal: remove the duplicate Core actor model so the Engine layer
(`UWorld`/`AActor`/`UActorComponent`) is the **only** World/Actor/Component
API. Core shrinks to shared primitives: `Application.h`, `Lifecycle.h`,
`TickFunction.h`, `Tickable.h`, `Time.h`, `Version.h`.

**Decision record (do not relitigate):** owner chose "retire Core model" on
2026-07-20. Old code stays available in git history.

- [x] **1.1 Inventory dependents of the retired types.** Find every reference
  to `TWorld`, `TActor`, `FActorBase`, `FActorComponent`, `FNetwork` outside
  `lib/microworld/include/MicroWorld/{World,Actor,ActorComponent,Network}.h`
  and their `.cpp` files. Expect hits in: `lib/microworld/tests/`,
  `lib/microworld/examples/HostLifecycle/`, `lib/microworld/benchmarks/`,
  `lib/microworld/tests/consumer/src/`, docs. List the files as notes under
  this task before proceeding.

  **Verify:**
  ```sh
  grep -rn "TWorld\|FActorBase\|FActorComponent\|MicroWorld::FNetwork\|MicroWorld/World.h\|MicroWorld/Actor.h\|MicroWorld/ActorComponent.h\|MicroWorld/Network.h" lib src --include=*.h --include=*.cpp --include=*.md -l
  ```
  **Done when:** the list of affected files is written under this task.

  **Inventory — recorded 2026-07-20.** Verify grep run via `rg` (the repo hook
  blocks `grep`; identical result). The Verify pattern's bare `FActorComponent`
  also matches the Engine types `FActorComponentRegistry` /
  `FActorComponentRegistryBase`, so its raw `-l` output over-reports engine files;
  the list below uses word-boundary matching to isolate genuine retired-type
  references (the same disambiguation task 1.3's own grep applies with `TWorld<` /
  `FActorBase` / `MicroWorld::FNetwork`). Counts are matches per file.

  *Retired type definitions* (the files task 1.2 deletes — not dependents), all
  under `lib/microworld/`: `include/MicroWorld/World.h` (5), `Actor.h` (19),
  `ActorComponent.h` (12), `Network.h` (7); `src/Actor.cpp` (13),
  `ActorComponent.cpp` (7), `Network.cpp` (5). (No `World.h` `.cpp` — `TWorld` is
  header-only.)

  *Genuine dependents — Core package code (migrate or delete in task 1.3):*
  - `lib/microworld/tests/WorldTests.cpp` (40) — delete
  - `lib/microworld/tests/ApplicationNetworkTests.cpp` (18) — delete
  - `lib/microworld/examples/HostLifecycle/Main.cpp` (8) — rewrite on Engine types, move to engine
  - `lib/microworld/benchmarks/DispatchBenchmark.cpp` (15) — port to Engine or delete
  - `lib/microworld/tests/consumer/src/NativeMain.cpp` (2) — retarget to Core primitives
  - `lib/microworld/tests/consumer/src/Esp32Main.cpp` (2) — retarget to Core primitives
  - `lib/microworld/tests/consumer/src/Esp32BenchmarkMain.cpp` (11) — retarget to Core primitives

  *Docs referencing retired types by name (task 1.4 doc sweep; 1.2 covers the
  README public-headers list):* `lib/microworld/README.md` (5), `CHANGELOG.md`
  (1), `AGENTS.md` (2), `docs/UE5ConceptMap.md` (2), `docs/Style.md` (1),
  `docs/ModulePackaging.md` (1), `docs/decisions/0001-modular-runtime.md` (1),
  `benchmarks/Results/Host.md` (4). NB: task 1.4 explicitly names only README,
  UE5ConceptMap, PROGRESS, ModulePackaging — the other four above also match and
  fall under 1.4's "no doc references retired types as current API" bar.
  (`PROGRESS.md` itself does **not** match — it names World/Actor/Component only
  generically.)

  *Engine package — stale doc-comment references only, NOT code dependencies
  (no `#include`, no code use); refresh in task 1.4:*
  - `lib/microworld-engine/include/MicroWorld/Engine/Actor.h:29-30` — comment
    "…use FActorBase and TActor from <MicroWorld/Actor.h> instead."
  - `lib/microworld-engine/include/MicroWorld/Engine/World.h:25` — comment
    "…matching Core's non-managed TWorld dispatch."

  *False positives — NOT retired-type dependents (match only
  `FActorComponentRegistry` / `FWorldActorRegistry`; leave untouched):* engine
  `EngineStorage.h`, `EngineRegistryView.h`, `src/Actor.cpp`, `src/World.cpp`,
  `tests/EngineLifecycleTests.cpp`, `tests/EngineRegistrationTests.cpp`,
  `tests/EngineGarbageCollectionTests.cpp`, and
  `lib/microworld/tests/consumer/src/EngineConsumerProbe.h`.

  *Build wiring* (filenames, not type refs — handled by 1.2/1.3):
  `lib/microworld/CMakeLists.txt` lists the retired sources and the retired test
  files. No genuine retired-type reference exists in the top-level `src/` app.

- [x] **1.2 Delete the retired headers and sources.** Remove
  `lib/microworld/include/MicroWorld/World.h`, `Actor.h`, `ActorComponent.h`,
  `Network.h` and `lib/microworld/src/Actor.cpp`, `ActorComponent.cpp`,
  `Network.cpp`. Update `lib/microworld/CMakeLists.txt` target sources and the
  public-headers list in `lib/microworld/README.md`.

  **Done when:** `cmake --build build/host-core` succeeds with the files gone.

- [x] **1.3 Migrate or delete dependent tests/examples/benchmarks.**
  - `lib/microworld/tests/WorldTests.cpp`, `ApplicationNetworkTests.cpp`:
    delete (their engine-level equivalents live in
    `lib/microworld-engine/tests/`). Keep `TickFunctionTests.cpp`.
  - `lib/microworld/examples/HostLifecycle/`: rewrite the same
    sensor-actor demo using `UWorld`/`AActor`/`UActorComponent` and move it to
    `lib/microworld-engine/examples/HostLifecycle/`.
  - `lib/microworld/benchmarks/DispatchBenchmark.cpp`: port to the Engine
    types or delete if `microworld-engine` benchmarks already cover dispatch.
  - Consumer probes under `lib/microworld/tests/consumer/src/` that used Core
    actors: retarget to Core primitives only (tick/lifecycle), since Engine has
    its own consumer probe.

  **Verify:**
  ```sh
  cmake -S lib/microworld -B build/host-core && cmake --build build/host-core && ctest --test-dir build/host-core --output-on-failure
  cmake -S lib/microworld-engine -B build/host-eng && cmake --build build/host-eng && ctest --test-dir build/host-eng --output-on-failure
  grep -rn "TWorld<\|FActorBase\|MicroWorld::FNetwork" lib/microworld lib/microworld-engine --include=*.h --include=*.cpp ; test $? -eq 1
  ```
  **Done when:** both suites pass and the grep finds nothing.

  **Merge outcome (1.2 + 1.3 done together, 2026-07-20).** Per the owner's
  decision in section 6, 1.2 and 1.3 were completed as one step so the Core build
  goes green once. Done here (GCC 16.1.0 via Ninja):
  - Deleted the 7 retired Core files, `tests/WorldTests.cpp`,
    `tests/ApplicationNetworkTests.cpp`, and `benchmarks/DispatchBenchmark.cpp`
    (dispatch is covered by the Engine suite).
  - `HostLifecycle` example rewritten on `UWorld`/`AActor`/`UActorComponent` and
    moved to `lib/microworld-engine/examples/HostLifecycle/Main.cpp`; wired into
    the engine CMake with the strict no-exceptions/RTTI options. It builds and,
    when run, prints the deterministic trace (component-before-actor begin;
    sensor ticks at 0/100/200 with 50 and 175 correctly skipped; actor-before-
    component end).
  - Consumer probes (`NativeMain`, `Esp32Main`, `Esp32BenchmarkMain`) retargeted
    to Core primitives (`FTickFunction`); the Core profile-map check still passes
    (no Object/Engine/Net markers leak into the Core profile). Follow-up
    (2026-07-20): the three shared their probe body into a new
    `CoreConsumerProbe.h` (`RunCoreConsumerProbe()`), matching the
    Memory/Object/Engine family convention — verified by a host GCC build+run of
    the native probe (exit 0) and strict compile-checks of both ESP32 entry
    points; the Xtensa `pio` build stays Phase 5's gate.
  - Core `CMakeLists.txt` (library sources, tests, retired benchmark/example
    targets) and the README public-headers list updated. Two stale Core-type
    references in engine header doc-comments (`Engine/Actor.h`, `Engine/World.h`)
    removed.
  - **Blocker cleared for the Engine suite:** `EngineAllocationCounters.cpp`
    `AllocateAligned` guard broadened `_MSC_VER` → `_WIN32` so MinGW builds.

  Evidence: `build/host-core` 5/5 pass; `build/host-eng` 1/1 pass (+ example
  builds and runs, exit 0); grep gate `rg "TWorld<|FActorBase|MicroWorld::FNetwork"`
  over `lib/microworld` + `lib/microworld-engine` (.h/.cpp) returns no matches;
  Core class-documentation checker passes (35 files). **Not verified this
  session:** the ESP32 PlatformIO consumer builds (no `pio`/hardware here) —
  those are Phase 5's gate; the retargeted probes are trivial Core-primitive
  `app_main`s. Remaining in Phase 1: **1.4** (documentation sweep), which also
  updates the README/UE5ConceptMap prose still describing the retired model.

- [x] **1.4 Documentation sweep for the retirement.** Update
  `lib/microworld/README.md` (Core = primitives only),
  `lib/microworld/docs/UE5ConceptMap.md` (remove Core actor rows; Engine rows
  are the concept map), `lib/microworld/PROGRESS.md` (note the retirement and
  why), `lib/microworld/docs/ModulePackaging.md` if it names retired headers.
  Run the doc checkers.

  **Verify:**
  ```sh
  python lib/microworld/tools/CheckClassDocumentation.py --root lib/microworld
  python lib/microworld/tools/CheckDependencyBoundaries.py --self-test
  # Then run each checker the same way the existing package CI/docs prescribe
  # (see lib/microworld/tools/AGENTS.md for the exact invocation per package).
  ```
  **Done when:** checkers pass; no doc references retired types as current API.

  **Done 2026-07-20 (documentation sweep).** Updated for the retirement:
  `README.md` (Core = lifecycle/tick primitives; the managed Actor model lives
  in Engine), `docs/UE5ConceptMap.md` (dropped the Core `TWorld`/`TActor`/
  `FActorComponent` rows so the managed Engine rows are the World/Actor/Component
  concept map), `PROGRESS.md` (Core "Done" entry reworded to primitives + a
  Phase 1 completion evidence row), `docs/ModulePackaging.md` and
  `docs/decisions/0001-modular-runtime.md` (the deleted Core `FNetwork` boundary
  reframed as retired, not current). Beyond the four named docs, the 1.1
  inventory's remaining retired-type hits were handled to the same bar:
  `AGENTS.md` (Core description → primitives), `docs/Style.md` (T-prefix example
  `TWorld<4>` → `TStaticVector<uint32_t, 4>`), `CHANGELOG.md` (added a "Removed"
  entry; the 0.1.0 release history left intact), and
  `benchmarks/Results/Host.md` (a "Historical (retired model)" note over the
  retired size/dispatch tables).

  Evidence: `CheckClassDocumentation.py` passes (36 files literal / 27 strict);
  `CheckDependencyBoundaries.py --self-test` and `--package Core=lib/microworld`
  pass; a grep for `TWorld`/`TActor`/`FActorBase`/`FActorComponent`/`FNetwork`
  over `lib/microworld/**/*.md` returns only retired/historical-framed hits — no
  doc presents a retired type as current API. Improvement applied alongside: the
  three Core consumer probes now share `CoreConsumerProbe.h` (host GCC build+run
  of the native probe exits 0; both ESP32 entry points compile clean under strict
  flags; Xtensa `pio` build stays Phase 5's gate).

  ⚠️ **Pre-existing gap flagged (not a Phase 1 blocker; see section 6).** The
  prescribed `CheckFolderAgents.py` run fails on `lib/microworld/docs/diagrams:
  missing AGENTS.md`. That directory (roadmap / C4 diagrams) predates Phase 1 and
  has no working-tree change from this task — the strict folder check was already
  red. Whether a generated-diagram directory needs a local guide is a design
  decision the plan does not cover; recorded as a proposed row in section 6 for
  the owner.

---

### Phase 2 — Runtime Spawn & Destroy ✅

Goal: a UE5 developer can spawn and destroy actors while the world is playing.
Bounded, deterministic, deferred: structural changes apply only at one barrier
point per frame, never in the middle of dispatch.

**Design (follow exactly):**

- Capacity never grows: a world can hold at most `MaxActors` live + pending
  actors (the existing `FWorldActorRegistry<MaxActors>` capacity).
- `UWorld::SpawnActor(TObjectPtr<AActor>)` — allowed while `Playing`. The actor
  must be `Constructed` (never begun), same store, not owned. It goes into a
  bounded **pending-spawn list**; it is registered and receives
  `DispatchBeginPlay` at the next barrier. Returns `EEngineResult`.
- `UWorld::DestroyActor(TObjectPtr<AActor>)` — allowed while `Playing`. The
  actor goes into a bounded **pending-destroy list**; at the barrier it gets
  `DispatchEndPlay`, is removed from the registry (order of survivors
  preserved), and `MarkPendingDestroy` is called on the store so GC reclaims
  it. Its components are ended with it (reverse order) and marked for destroy.
- The barrier is `UWorld::ApplyPending(TimePointMilliseconds Now)`, called by
  the application (or `TEngineHost` in Phase 3) **after** `Advance` each frame.
  Order at the barrier: destroys first, then spawns (frees capacity first).
- Registration before `BeginPlay` keeps working exactly as today.
- Components remain fixed per actor at construction (no runtime component
  add/remove — out of scope).

**Exact API to add to `UWorld` (Engine/World.h):**

```cpp
/** Queues one constructed, same-store, unowned actor for begin at the next barrier. */
EEngineResult SpawnActor(TObjectPtr<AActor> Actor) noexcept;

/** Queues one actor registered with this world for end+release at the next barrier. */
EEngineResult DestroyActor(TObjectPtr<AActor> Actor) noexcept;

/** Applies pending destroys (first) then pending spawns; call once per frame after Advance. */
ERuntimeResult ApplyPending(TimePointMilliseconds NowMilliseconds) noexcept;

/** Report pending structural work so tests and budget policy can observe it. */
std::size_t PendingSpawnCount() const noexcept;
std::size_t PendingDestroyCount() const noexcept;
```

**Validation → result mapping (must be exact and tested):**

| Condition | `SpawnActor` | `DestroyActor` |
| --- | --- | --- |
| World not `Playing` | `LifecycleLocked` | `LifecycleLocked` |
| Empty/stale/non-resolvable reference | `InvalidReference` | `InvalidReference` |
| Reference from another store | `CrossStore` | `CrossStore` |
| Actor already registered or already pending-spawn | `Duplicate` | — |
| Actor not registered with this world | — | `InvalidReference` |
| Actor already pending-destroy | — | `Duplicate` |
| Actor owned by another world | `AlreadyOwned` | `InvalidReference` |
| live + pendingSpawn == MaxActors | `CapacityExceeded` | — |
| All checks pass | `Success` (queued) | `Success` (queued) |

- [x] **2.1 Extend registry storage for pending lists.** In
  `lib/microworld-engine/include/MicroWorld/Engine/EngineStorage.h` and
  `EngineRegistryView.h`: give `FWorldActorRegistry<MaxActors>` two extra
  fixed arrays (`PendingSpawn`, `PendingDestroy`, each `MaxActors` slots with
  counts) exposed through the existing lease type, plus a
  `RemoveAt(index)` on the lease that removes one entry and shifts the tail
  left (stable order). Only `UWorld` may call these (existing friend pattern).

  **Done when:** engine package still builds; new lease operations have doc
  comments; no public API leaks mutable storage.

  **Done 2026-07-20.** `FWorldActorRegistry<MaxActors>`
  ([EngineStorage.h](lib/microworld-engine/include/MicroWorld/Engine/EngineStorage.h))
  gained `PendingSpawn`/`PendingDestroy` fixed arrays (each `MaxActors`) with
  counts, passed into the lease via `MakeView`. The lease
  ([EngineRegistryView.h](lib/microworld-engine/include/MicroWorld/Engine/EngineRegistryView.h))
  gained `RemoveAt(Index)` (stable left-shift of the live array, vacated tail
  slot cleared) plus symmetric private `Get*Count`/`*At`/`Add*`/`Clear*`
  accessors for both pending lists; `IsValid` now also bounds the pending
  counts. All new members/methods are private (`friend UWorld`), doc-commented,
  and nothing is called yet (task 2.2 uses them). Evidence: `host-eng` builds
  clean under strict warnings and CTest passes 1/1; the example still links.
  Note for 2.2/consumers: the lease (and therefore `UWorld`) grew by four
  pointers — object-store slots must still fit `UWorld`.

- [x] **2.2 Implement SpawnActor/DestroyActor/ApplyPending on UWorld.** In
  `Engine/World.h` + `src/World.cpp` per the design above. Validation reuses
  the same checks as `RegisterActor` (duplicate, capacity incl. pending,
  cross-store, invalid reference, already-owned, already-pending). All
  failures transactional. `VisitReferences` must also trace pending-spawn
  actors (they are reachable). While `Constructed`, `SpawnActor` returns
  `LifecycleLocked` (use `RegisterActor` instead); after `EndPlay`, both
  return `LifecycleLocked`.

  **Done when:** compiles clean under the package's strict warnings; every new
  method documented; `AActor::DispatchEndPlay` path also marks the actor's
  registered components `MarkPendingDestroy` after their `EndPlay` ran.

  **Done 2026-07-21 (option A, owner chose "simplest solution").** The plan's
  literal "mark components inside `AActor::DispatchEndPlay`" is infeasible: the
  store rejects `MarkPendingDestroy` while a dispatch guard is held, and
  `DispatchEndPlay` is shared with non-destroying world `EndPlay`. Resolved by
  keeping `DispatchEndPlay` a pure end-cascade and making `ApplyPending`
  two-phase: (1) end the doomed actors under the dispatch guard; (2) after
  releasing it, mark each actor's components (new `UWorld`-friend
  `AActor::MarkRegisteredComponentsPendingDestroy`) and the actor itself
  `MarkPendingDestroy`, then `RemoveAt` from the live set (survivor order
  preserved); (3) register + begin pending spawns under a fresh guard.
  `SpawnActor`/`DestroyActor` validate per the table (queue-only; world identity
  bound at the barrier). `VisitReferences` now also traces pending-spawn actors.
  Evidence: `host-eng` builds clean under strict warnings and CTest passes 1/1
  (no regression). Spawn/destroy behavior is exercised by task 2.3's tests.

- [x] **2.3 Tests for spawn/destroy.** New
  `lib/microworld-engine/tests/EngineSpawnDestroyTests.cpp` covering at
  minimum: spawn during play begins at next barrier (not immediately); destroy
  during play ends at barrier with reverse-order component shutdown; capacity
  counts live+pending; duplicate spawn rejected; destroy of never-registered
  actor rejected; destroyed actor's handle goes stale after GC; survivor
  dispatch order preserved after mid-list removal; spawn+destroy of the same
  actor in one frame; all rejection paths leave state unchanged (sentinel
  checks); GC cycle after destroy reclaims actor and components with roots and
  worklist accounted.

  **Verify:**
  ```sh
  cmake -S lib/microworld-engine -B build/host-eng && cmake --build build/host-eng && ctest --test-dir build/host-eng --output-on-failure
  ```
  **Done when:** all listed behaviors have passing cases.

  **Done 2026-07-21.** New
  [EngineSpawnDestroyTests.cpp](lib/microworld-engine/tests/EngineSpawnDestroyTests.cpp)
  (12 cases) wired into `MICROWORLD_ENGINE_TEST_SOURCES`. It mirrors the existing
  engine-test idiom (`TEngineEnvironment`, `FSequenceCounter`/event states, a
  `FCollectorFixture`, and a `FSecondStore` for cross-store references). Cases:
  spawn begins only at the barrier (not at the call) and then ticks as a live
  participant; destroy ends at the barrier with actor-before-components and
  components in reverse order; spawn capacity counts live+pending before *and*
  after the barrier; duplicate spawn rejected while pending and while live;
  destroy of a never-registered actor rejected; spawn/destroy lifecycle-locked
  before BeginPlay and after EndPlay; every spawn reference rejection
  (empty→InvalidReference, foreign→CrossStore, other-world→AlreadyOwned) and
  destroy reference rejection (empty→InvalidReference, foreign→CrossStore,
  repeat→Duplicate) with pending-queue sentinels; survivor tick order preserved
  after a mid-list removal; a destroyed actor's handle hidden at the barrier and
  durably stale (slot reused with a fresh generation) after reclamation; and a
  full GC cycle after destroy that accounts every root and keeps the worklist
  within capacity, followed by the store barrier reclaiming the actor + both
  components.

  **Open question resolved (no design change).** "Spawn+destroy of the same actor
  in one frame": `DestroyActor` on a still-pending-spawn actor returns
  `InvalidReference` — the documented section-5 behavior (a pending-spawn actor is
  not yet registered; destroy does not cancel a spawn). Tested as-is; no new
  decision needed.

  ⚠️ **Plan/implementation discrepancy found and RESOLVED (see section 6,
  2026-07-21).** The section-4 frame order and the section-2 design said the GC
  slice reclaims the "released" (pending-destroy) actors, but the object store's
  GC sweep explicitly *skips* pending-destroy slots
  ([GarbageCollector.cpp](lib/microworld-object/src/GarbageCollector.cpp) sweep
  phase) — reclamation of destroyed actors is the store's `ApplyPendingDestroy`
  barrier, not GC mark/sweep. The tests document the real mechanism (GC accounts
  roots+worklist and leaves pending-destroy to the store; `ApplyPendingDestroy`
  reclaims). Owner chose **option (a)**: section 4 now has an explicit bounded
  reclamation slice (`Store.ApplyPendingDestroy`) before the GC slice, to be
  wired into `TEngineHost` in Phase 3.2.

  Evidence: `build/host-eng` builds clean under strict warnings
  (`-Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti`); CTest 1/1;
  runner reports 67 tests, 0 failures (12 new).

- [x] **2.4 Ergonomics: `TInlineActor<N>` / `TInlineWorld<N>`.** New header
  `lib/microworld-engine/include/MicroWorld/Engine/InlineTypes.h`. Use the
  base-from-member idiom so the registry storage lives inside the object:

  ```cpp
  namespace Detail {
  template<std::size_t N> struct TActorRegistryHolder { FActorComponentRegistry<N> Registry; };
  }
  template<std::size_t N>
  class TInlineActor : private Detail::TActorRegistryHolder<N>, public AActor
  {
  public:
      explicit TInlineActor(FTickConfiguration Cfg = {}) noexcept
          : AActor(this->Registry.MakeView(), Cfg) {}
  };
  // TInlineWorld<N> mirrors this over FWorldActorRegistry<N> and UWorld.
  ```

  Derived user classes extend `TInlineActor<N>` and register their components
  in their constructor. Add a test that builds a world + actor + component
  using only inline types, and rewrite the Phase 1 HostLifecycle example with
  them. **Note:** each concrete inline instantiation is its own managed type —
  it needs its own `FClassDescriptor` registered (document this in the header).

  **Done when:** example + test compile and pass using inline types only;
  doc comments explain the descriptor requirement.

  **Done 2026-07-21.** New header
  [InlineTypes.h](lib/microworld-engine/include/MicroWorld/Engine/InlineTypes.h)
  adds `TInlineActor<N>` and `TInlineWorld<N>` via the base-from-member idiom: a
  private `Detail::TActorRegistryHolder<N>` / `TWorldRegistryHolder<N>` base is
  declared *before* the `AActor` / `UWorld` base, so its embedded registry is
  fully constructed when leased to the managed base's constructor (and, by
  reverse-destruction order, outlives it). Doc comments state the descriptor
  requirement — each concrete instantiation (the template used directly or any
  subclass) is its own managed type needing an `FClassDescriptor` from
  `MakeClassDescriptor<ThatExactType>` with the right parent, and store slots
  wide enough for the embedded registry.

  New [EngineInlineTypesTests.cpp](lib/microworld-engine/tests/EngineInlineTypesTests.cpp)
  (2 cases, wired into `MICROWORLD_ENGINE_TEST_SOURCES`) builds a world + actor +
  component from inline types only — no caller-composed `FWorldActorRegistry` /
  `FActorComponentRegistry` object — and proves begin/tick/end order matches the
  lease-composed types, plus that an inline actor still spawns and destroys
  through the deferred barrier. The Phase 1
  [HostLifecycle example](lib/microworld-engine/examples/HostLifecycle/Main.cpp)
  is rewritten on `TInlineWorld<1>` / `TInlineActor<1>` (registry-lease locals
  removed; slot size widened to 512 for the embedded registries) and still prints
  the same deterministic trace.

  Evidence: `build/host-eng` builds clean under strict warnings
  (`-Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti`); CTest 1/1;
  runner reports 69 tests, 0 failures (2 new); the example runs (exit 0) with the
  unchanged trace (component-before-actor begin; sensor ticks at 0/100/200 with 50
  and 175 skipped; actor-before-component end); `CheckClassDocumentation.py
  --root lib/microworld-engine` passes (24 files).

---

### Phase 3 — Engine composition root & logging ✅

Goal: one type that wires the whole runtime, and a logging facade. After this
phase a "hello world" app is ~20 lines, and every app shares the same frame
order.

- [x] **3.1 `MW_LOG` logging facade.** New header
  `lib/microworld/include/MicroWorld/Log.h` (Core owns it; every package may
  use it). Requirements:
  - Levels: `Error`, `Warning`, `Log`, `Verbose`.
  - Compile-time floor: `MW_LOG_MIN_LEVEL` (default `Log`); calls below the
    floor compile to nothing (zero code, zero strings in flash).
  - One process-global sink: `using FLogSink = void(*)(ELogLevel, const char* Category, const char* Message)`,
    set via `SetLogSink(FLogSink)`. Default sink is null → logging disabled.
    No allocation, no formatting inside Core: the macro forwards a
    printf-style format + args to the sink adapter only if you can do it
    without allocation — otherwise keep the sink signature message-only and
    let callers format into a caller-owned buffer. Choose the simplest option
    and document it.
  - Usage: `MW_LOG(Warning, "Net", "peer %u timed out", Index);` or the
    message-only variant.
  - Tests: floor stripping (a below-floor call must not evaluate arguments),
    sink routing, null-sink safety.

  **Verify:** Core package build + new tests pass.
  **Done when:** requirements above met; README of Core documents the macro.

  **Done 2026-07-21.** New Core-owned header
  [Log.h](lib/microworld/include/MicroWorld/Log.h) (+ [src/Log.cpp](lib/microworld/src/Log.cpp))
  adds `ELogLevel` (Error/Warning/Log/Verbose), one process-global
  `FLogSink` function pointer set via `SetLogSink` (default null → disabled), and
  two macros: `MW_LOG(Level, Category, Fmt, ...)` (printf-style) and
  `MW_LOG_MSG(Level, Category, Message)` (message-only, `%`-safe).

  **Design decision (plan delegated "choose the simplest option and document
  it").** Level gating is done in the **preprocessor**, not with `if constexpr`:
  a below-floor call selects a `((void)0)` emitter that drops all arguments, so
  it emits zero code, keeps **zero format/category strings in flash**, and never
  evaluates its arguments — guaranteed at any optimization level, which
  `if constexpr` cannot promise. Formatting uses a fixed
  `MW_LOG_MESSAGE_CAPACITY` (default 128) **caller-stack** buffer + `vsnprintf`
  (no heap, no exceptions, no clock); the plan's sink stays message-only so Core
  never allocates. `MW_LOG_MIN_LEVEL` defaults to `Log`.

  New [LogTests.cpp](lib/microworld/tests/LogTests.cpp) (6 cases, wired into
  `microworld_tests`) covers message + formatted sink routing, null-sink safety
  (drop then reinstall), floor stripping proving a below-floor call does **not**
  evaluate its arguments (both macros), and the full level boundary
  (Error/Warning/Log route, Verbose stripped). Core README documents the macro.

  Evidence: `build/host-core` (Ninja g++ 16.1.0, `-Wall -Wextra -Wpedantic
  -Werror`) builds clean; CTest 5/5; runner 20 cases, 0 failures (6 new); the
  stripped `Verbose` literal `"verbose"` is **absent** from `LogTests.cpp.obj`
  while live literals are present (empirical zero-strings proof at `-O0`);
  `CheckClassDocumentation.py --root lib/microworld` passes (30 files).

- [x] **3.2 `TEngineHost` composition template.** New header
  `lib/microworld-engine/include/MicroWorld/Engine/EngineHost.h`:

  ```cpp
  template<
      std::size_t MaxClasses, std::size_t MaxObjects, std::size_t SlotBytes,
      std::size_t SlotAlign, std::size_t MaxRoots, std::size_t MaxActors,
      std::size_t MaxTimers, std::size_t TimerCallbackBytes>
  class TEngineHost final { ... };
  ```

  Owns (as members, in construction order): class registry, object-store
  storage arrays, `FObjectStore`, GC worklist storage, `FGarbageCollector`,
  `FWorldActorRegistry<MaxActors>`, `TTimerManager<MaxTimers, TimerCallbackBytes>`.
  API:
  - `EObjectResult RegisterClass(const FClassDescriptor&)` — forwards to registry;
    engine base descriptors (`UWorld`, `AActor`, `UActorComponent`) registered
    automatically in the constructor.
  - `TObjectPtr<UWorld> CreateWorld()` — constructs the `UWorld` in the store,
    takes one `TStrongObjectPtr<UWorld>` root, stores it.
  - `template<typename T, ...Args> TObjectCreationResult<T> NewObject(Args&&...)`
    — forwards to `FObjectStore::NewObject<T>` (which already exists).
  - `TTimerManager& GetTimerManager()`, `UWorld& GetWorld()`,
    `FObjectStore& GetObjectStore()`.
  - `ERuntimeResult BeginPlay(Now)`, `ERuntimeResult Tick(Now)`,
    `ERuntimeResult EndPlay()` — `Tick` runs the canonical frame order from
    section 4 (net steps become live in Phase 4; leave clearly marked call
    slots until then). GC each tick: call `RequestCollection()` if the
    collector phase is `Idle`, then `Advance(Budget)`; the
    `FGarbageCollectionBudget` is a constructor parameter.
  - No heap use anywhere; `TEngineHost` itself is meant to live in static
    storage or `main`'s stack frame.

  **Done 2026-07-21.** New header
  [EngineHost.h](lib/microworld-engine/include/MicroWorld/Engine/EngineHost.h)
  adds `TEngineHost<MaxClasses, MaxObjects, SlotBytes, SlotAlign, MaxRoots,
  MaxActors, MaxTimers, TimerCallbackBytes>`. It owns, in construction order, the
  `TClassRegistry`, the object-store byte/metadata/root storage arrays, the
  `FObjectStore`, the GC worklist array, the `FGarbageCollector`, the
  `FWorldActorRegistry<MaxActors>`, and the `TTimerManager` — all fixed-capacity,
  no heap. The constructor registers the three engine base descriptors and takes
  the GC budget (plus a bounded reclamation budget, default = all slots).

  API delivered per spec: `RegisterClass`, `CreateWorld` (constructs the one
  `UWorld` from the registry-owned descriptor via `Find`+`NewObject`, roots it with
  a stored `TStrongObjectPtr<UWorld>`, one-per-host guarded), transparent
  `NewObject<T>` forwarding, `GetWorld`/`GetObjectStore`/`GetTimerManager`, and
  `BeginPlay`/`Tick`/`EndPlay`. `Tick` runs the section-4 frame order exactly:
  (1) net-receive slot [Phase 4], (2) `Timers.Advance`, (3) `World.Advance`,
  (4) `World.ApplyPending`, (5) the bounded `Store.ApplyPendingDestroy` reclamation
  slice (the 2026-07-21 decision — GC sweep skips pending-destroy, so this frees
  destroyed actors), (6) idle-gated `RequestCollection` + bounded `Advance`,
  (7) net-send slot [Phase 4]. A per-tick monotonic guard rejects a rolled-back
  clock transactionally before any step runs.

  Evidence: a throwaway strict-compile probe instantiating `TEngineHost<8,16,512,
  16,4,8,4,32>` and exercising every substantive method compiles clean under the
  engine's `-std=gnu++17 -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic
  -Werror` (probe not committed — the host tests + example are task 3.3);
  `build/host-eng` still builds and CTest passes 1/1 (no regression);
  `CheckClassDocumentation.py --root lib/microworld-engine` passes.

- [x] **3.3 Tests + example for the host.**
  `lib/microworld-engine/tests/EngineHostTests.cpp`: begin/tick/end lifecycle;
  frame order observable via instrumented timer callback vs. actor tick vs. GC
  reclamation (spawn garbage, verify bounded reclamation across ticks);
  monotonic-time rejection; idempotent EndPlay. Rewrite the HostLifecycle
  example (from 2.4) on top of `TEngineHost` — that example is now the
  canonical "hello MicroWorld" and gets linked from the README.

  **Verify:** engine package build + tests; example runs and prints expected
  lifecycle trace.
  **Done when:** example ≤ ~60 lines of user code; tests pass.

  **Completed 2026-07-21.** Added five `TEngineHost` behavior cases in
  `lib/microworld-engine/tests/EngineHostTests.cpp` (lifecycle order; timer-
  before-tick frame order; bounded GC reclamation of unrooted objects across
  ticks; transactional non-monotonic-time rejection; idempotent `EndPlay`),
  wired into `MICROWORLD_ENGINE_TEST_SOURCES` between the inline-types and GC
  suites. Rewrote `examples/HostLifecycle/Main.cpp` on `TEngineHost` (the
  `FDeviceWorld`/`TInlineWorld` typedef and hand-rolled store/registry/world
  composition are gone; `FSensorComponent`/`FDeviceActor` and the canonical
  trace are unchanged). Applied the mandatory 3.2 gap fix — a public
  `TEngineHost::FindClass(FTypeId)` accessor — so user descriptors can be built
  against the registry's own parent copies and user types constructed through
  them (roadmap section 6, 2026-07-21, option a). GCC 16.1.0 via Ninja:
  `host-eng` builds clean under `-Wall -Wextra -Wpedantic -Werror
  -fno-exceptions -fno-rtti`; CTest 1/1; runner 74 host cases, 0 failures
  (5 new); `microworld_engine_host_lifecycle` prints the unchanged lifecycle
  trace and exits 0; `CheckClassDocumentation.py --root lib/microworld-engine`
  passes (24 files).

- [x] **3.4 Host creation ergonomics.** Add `TEngineHost::RegisterClass<T>(id,
  name)` and `CreateObject<T>(id, args...)` mirroring the test fixture's
  `RegisterDerivedClass`/`CreateDerivedObject`; simplify the `HostLifecycle`
  example onto them (trace unchanged). **Done when:** helpers covered by tests;
  example composition body meaningfully shorter with an unchanged trace; suite
  green.

  **Completed 2026-07-21.** Added two `TEngineHost` public templates in
  `lib/microworld-engine/include/MicroWorld/Engine/EngineHost.h`:
  `RegisterClass<T>(FTypeId, const char*)` derives each descriptor's parent from
  `T`'s engine base (`AActor`/`UActorComponent`/`UWorld`) via `std::is_base_of`
  and registers it with the shared `&TraceManagedObjectReferences` tracer, and
  `CreateObject<T>(FTypeId, Args&&...)` folds `FindClass` + `Store.NewObject<T>`
  into one call (returning `EObjectResult::UnknownClass` + null for an
  unregistered id). The existing non-template `RegisterClass(const
  FClassDescriptor&)` overload is kept. Added two `TEngineHost` cases in
  `lib/microworld-engine/tests/EngineHostTests.cpp` (helper-driven register +
  construct + lifecycle-order proof; unregistered-id rejection) wired into
  `MICROWORLD_ENGINE_TEST_SOURCES`. Rewrote `examples/HostLifecycle/Main.cpp`
  on the helpers: the per-type build-descriptor → `RegisterClass` → `FindClass`
  → `NewObject` dance (and the `MakeClassDescriptor`/`TraceManagedObjectReferences`
  includes it dragged in) is gone; `FSensorComponent`/`FDeviceActor` and the
  canonical trace are unchanged. GCC 16.1.0 via Ninja: `host-eng` builds clean
  under `-Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti`; CTest
  1/1; runner 78 host cases, 0 failures (2 new);
  `microworld_engine_host_lifecycle` prints the byte-identical lifecycle trace
  and exits 0; `CheckClassDocumentation.py --root lib/microworld-engine` passes.
  `main()` shrank 46 → 43 lines; the register+construct section shrank
  18 → 7 lines.

---

### Phase 4 — Simple networking with roles ✅

Goal: the UE5 *concept* of dedicated server / listen server / client, delivered
as simple bounded message passing. **No replication, no RPC, no property sync.**
One driver, one peer table, channels, heartbeats. Everything host-testable via
loopback.

**Design (follow exactly):**

- `FNetAddress` (in `microworld-net`): opaque driver-defined bytes.
  `std::array<std::uint8_t, 12> Bytes; std::uint8_t Size;` with `operator==`.
  UDP uses IPv4+port (6 bytes), LoRa uses a 1–2 byte node id, loopback uses 1
  byte. Drivers document their encoding.
- `INetDriver` v2 (breaking change):
  ```cpp
  virtual ENetResult TrySend(const FNetAddress& To, TSpan<const std::uint8_t> Packet) noexcept = 0;
  virtual ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> Dest, FNetReceiveResult& Out) noexcept = 0;
  virtual std::size_t MaxPacketBytes() const noexcept = 0;
  ```
  Same transactional semantics as today. Update `FNetManager` (queue entries
  gain a destination address) and `FHostLoopback` (multi-endpoint: N bounded
  mailboxes keyed by 1-byte address, so client/server tests run in-process).
- Message header (on the wire, after any driver framing):
  `[u8 Channel][u8 Flags][u16 PayloadBytes]` little-endian via
  `FByteWriter`/`FByteReader`. Channel 0 is reserved for session control;
  channels 1–255 are application-defined. Flags reserved, must be 0.
- Session control messages (channel 0, first payload byte = type):
  `Hello=1` (client→server), `Welcome=2` (server→client, carries assigned peer
  index), `Heartbeat=3` (both directions on a configured interval), `Bye=4`.
- `ENetMode { Standalone, Client, ListenServer, DedicatedServer }`.
- `TNetHost<MaxPeers, MaxPacketBytes>` (new header `Net/NetHost.h`):
  - `Configure(ENetMode, INetDriver&, FNetHostConfig)` where config carries
    heartbeat interval ms, peer timeout ms, server address (client mode).
  - `Start(Now)`, `Stop()`.
  - `PumpReceive(Now)`: drain driver (bounded per call: at most `MaxPeers + 4`
    receives), handle channel 0 internally (peer admission — bounded table
    with generation-checked `FPeerId{u8 Index, u8 Generation}` —, heartbeat
    bookkeeping, timeout eviction), dispatch channels ≥1 to the registered
    handler: one `TMulticastDelegate<void(FPeerId, std::uint8_t Channel,
    TSpan<const std::uint8_t>), MaxHandlers, InlineBytes>` (reuse Memory's
    multicast delegate; pick small fixed capacities and document them).
  - `SendTo(FPeerId, Channel, payload)`, `Broadcast(Channel, payload)` —
    build header + payload into the outbound FIFO (`FNetManager`).
  - `PumpSend(Now)`: emit due heartbeats, then drain the FIFO via
    `AdvanceSend` (bounded per call).
  - Role semantics: `DedicatedServer`/`ListenServer` accept `Hello` up to
    `MaxPeers`; `Client` holds exactly one peer (the server) and sends `Hello`
    on `Start`; `Standalone` runs no driver traffic and reports `Unavailable`
    on send. `ListenServer` additionally owns a **local peer**: messages sent
    to it are dispatched directly to the local handler without the driver
    (this is what makes it a listen server rather than a naming gimmick).
- Peers who miss heartbeats for the timeout window are evicted; their
  `FPeerId` generation bumps so stale ids fail safely.

**Wire format (exact byte layout, little-endian):**

| Offset | Size | Field | Rule |
| --- | --- | --- | --- |
| 0 | 1 | `Channel` | 0 = session control, 1–255 = application |
| 1 | 1 | `Flags` | Must be 0; receiver drops packet if nonzero |
| 2 | 2 | `PayloadBytes` | u16 LE; receiver drops packet if it disagrees with actual packet size |
| 4 | N | `Payload` | N == `PayloadBytes` |

**Control payloads (channel 0, first payload byte = message type):**

| Type | Name | Payload after type byte | Direction |
| --- | --- | --- | --- |
| 1 | `Hello` | `[u8 ProtocolVersion]` | client → server |
| 2 | `Welcome` | `[u8 ProtocolVersion][u8 PeerIndex][u8 PeerGeneration]` | server → client |
| 3 | `Heartbeat` | (empty) | both |
| 4 | `Bye` | (empty) | both |

Version mismatch: server ignores the `Hello` (no reply) and logs via `MW_LOG`.
Unknown control type / malformed control payload: drop, log, take no action.

**Core structs (exact fields):**

```cpp
struct FNetHostConfig
{
    DurationMilliseconds HeartbeatIntervalMilliseconds{1000};
    DurationMilliseconds PeerTimeoutMilliseconds{5000};
    FNetAddress ServerAddress{};      // Client mode only; ignored otherwise.
    std::uint8_t ProtocolVersion{1};
};

struct FPeerId { std::uint8_t Index{0xFF}; std::uint8_t Generation{0}; };

struct FNetPeerSlot                    // internal to TNetHost
{
    FNetAddress Address{};
    TimePointMilliseconds LastReceiveMilliseconds{0};
    TimePointMilliseconds LastSendMilliseconds{0};   // paces heartbeats
    std::uint8_t Generation{0};
    bool bActive{false};
};
```

**Peer state machines (implement exactly; test every transition):**

Server slot: `Free` → (`Hello` received, free slot, version ok) → `Active`
(send `Welcome`) → (`Bye` received OR `Now - LastReceive > PeerTimeout`) →
`Free`, `Generation += 1`. A repeated `Hello` from an already-active address
re-sends `Welcome` (idempotent), does not allocate a second slot.

Client: `Idle` → `Start()` → `Connecting` (send `Hello`; re-send each
heartbeat interval) → (`Welcome` received) → `Connected` (heartbeats flow) →
(timeout) → `Connecting` again (fresh `Hello`). Application can observe state
via `ENetHostState GetState() const noexcept`.

- [x] **4.1 `FNetAddress` + `INetDriver` v2 + migrate `FNetManager` and
  `FHostLoopback`.** Update all existing net tests to the addressed API.
  Loopback becomes multi-endpoint as described.

  **Verify:** net package build + all tests pass.
  **Done when:** no unaddressed send/receive API remains; loopback supports at
  least 4 endpoints in tests.

  **Completed (2026-07-21):** Added `Net/NetAddress.h` (`FNetAddress` = 12-byte
  `Bytes` + `Size`, `==`/`!=`, `MakeLoopbackAddress`); broke `INetDriver` to v2
  (`TrySend(const FNetAddress&, ...)`, `TryReceive(FNetAddress& OutFrom, ...)`,
  `MaxPacketBytes()`); rewrote `FHostLoopback` as a shared-mailroom
  `FHostLoopback<MaxPorts, MailboxCapacity, PacketBytes>` with N per-port
  mailboxes and N embedded per-port drivers (`Port(i)`);
  `FNetPacketStorage` gained a parallel `Destinations[]` array and `FNetManager`
  `QueueSend`/`AdvanceSend`/`Receive` carry the destination/sender address.
  Migrated `NetConsumerProbe.h` and all 27 existing net cases; added 4
  HostLoopback multi-port cases (4-port routing + isolation, two-way reply,
  unroutable-address rejection, `MaxPacketBytes`/too-small retention) and 1
  NetManager case (per-packet destination routing in FIFO order). Verify
  evidence: clean GCC 16.1.0 build (zero warnings), `ctest` 1/1 Passed, runner
  `[SUMMARY] 57 tests, 0 failures`, CheckClassDocumentation passed (16 files),
  CheckDependencyBoundaries passed (1 package, 9 files), standalone net
  consumer exited 0. Files changed: `lib/microworld-net/include/MicroWorld/Net/{NetAddress.h,NetDriver.h,HostLoopback.h,NetPacketStorage.h,NetManager.h}`,
  `lib/microworld-net/tests/{HostLoopbackTests,NetManagerTests,NetAllocationTests}.cpp`,
  `lib/microworld/tests/consumer/src/NetConsumerProbe.h`, `MICROWORLD_ROADMAP.md`.
  No `PROGRESS.md` row (phase not yet ✅). Tasks 4.2–4.4 not started.

- [x] **4.2 Message framing + session control.** New `Net/NetProtocol.h` with
  header/read/write helpers and control-message encode/decode built on
  ByteWriter/Reader. Pure functions, fully unit-tested (round trip, truncation,
  bad flags, unknown control type).

  **Done when:** framing tests pass; no allocation; all failures transactional.

  **Completed (2026-07-21):** Added header-only `Net/NetProtocol.h` with the
  4-byte `[Channel][Flags][PayloadBytes LE][Payload]` message frame and the
  channel-0 control-message encode/decode, all pure `inline noexcept` functions
  composed on `FByteWriter`/`FByteReader` (no driver/addressing/logging).
  `WriteMessage` pre-checks total capacity before the first `WriteByte` so a
  `Full`/`Invalid` leaves the writer cursor and accepted bytes unchanged;
  `ReadMessage` parses one whole packet via direct indexing, writing outputs
  only on `Success`; `WriteControlMessage` builds the per-type payload in a
  fixed local array and delegates to `WriteMessage` (DRY);
  `ReadControlMessage` uses a local `FByteReader`, validates the type byte
  against {Hello,Welcome,Heartbeat,Bye}, enforces the exact per-type payload
  length (Hello==2, Welcome==4, Heartbeat==1, Bye==1), and writes outputs only
  on `Success`. Added 18 unit cases in `tests/NetProtocolTests.cpp` covering
  app-message round trips (empty/1-byte/multi-byte), `WriteMessage` Full and
  oversized-length rejection, `ReadMessage` truncated header / size mismatch /
  nonzero-flags rejection, all four control-message round trips, unknown-type
  and empty/malformed-control rejection, and `WriteControlMessage` unknown-type
  rejection. Extended `NetAllocationTests.cpp`'s
  `NetOperationsPerformNoObservableAllocation` case to exercise all four
  framing functions inside its counted region (zero-delta). Verify evidence:
  clean GCC 16.1.0 build (zero warnings), `ctest` 1/1 Passed, runner
  `[SUMMARY] 75 tests, 0 failures`, CheckClassDocumentation passed (18 files),
  CheckDependencyBoundaries passed (1 package, 10 files), `clang-format
  --style=file:clang-format --dry-run --Werror` exit 0 (clean). Files changed:
  `lib/microworld-net/include/MicroWorld/Net/NetProtocol.h` (new),
  `lib/microworld-net/tests/NetProtocolTests.cpp` (new),
  `lib/microworld-net/tests/NetAllocationTests.cpp`,
  `lib/microworld-net/CMakeLists.txt`, `MICROWORLD_ROADMAP.md`. No
  `PROGRESS.md` row. Task 4.3 not started.

- [x] **4.3 `TNetHost` with roles.** Implement per the design. Tests (over
  multi-endpoint loopback): server admits client (Hello→Welcome), peer table
  capacity rejection, heartbeat keeps peer alive, missed heartbeats evict,
  stale `FPeerId` rejected after eviction, client reconnect gets new
  generation, broadcast reaches all peers, listen-server local peer receives
  without driver traffic, dedicated server has no local dispatch, `Standalone`
  sends report `Unavailable`, bounded pumps (a flooded driver cannot starve
  the frame).

  **Verify:** net package build + tests.
  **Done when:** every listed behavior has a passing case.

  **Completed (2026-07-21):** Added header-only `Net/NetHost.h` with
  `TNetHost<MaxPeers, MaxPacketBytes>`, `ENetMode`, `ENetHostState`,
  `FNetHostConfig`, `FPeerId` (generation-checked), and an internal
  `FNetPeerSlot`. Constructor-injects the driver (`explicit TNetHost(INetDriver&)`)
  and takes mode/config via `Configure(ENetMode, const FNetHostConfig&)` — an
  approved, documented deviation from the spec's `Configure(driver)` that lets the
  host own an `FNetManager` as a plain member (DRY; no deferred-construction
  machinery). Reuses 4.2's `NetProtocol` framing and 4.1's `FNetManager`/`FHostLoopback`.
  Tick-driven (`PumpReceive`/`PumpSend`, no hidden clock); channel 0 handled
  internally (admission, heartbeats, timeout eviction), channels 1..255 dispatch
  to one bounded `TMulticastDelegate`. `PumpReceive` is bounded to `MaxPeers + 4`
  receives per call; `ListenServer` owns a sentinel local peer dispatched directly
  without the driver; `Stop()` sends best-effort `Bye`; version-mismatch and
  unknown/malformed control log via `MW_LOG` and take no action. Added 18
  behavioral cases in `tests/NetHostTests.cpp` (one per listed behavior plus the
  Idle→Connecting→Connected machine, repeated-Hello idempotency, `Bye` eviction,
  unknown-control drop, exactly-once delivery, and a no-allocation full session)
  over `FHostLoopback`, using a small counting mock driver to prove the pump bound.
  Verify evidence: clean GCC 16.1.0 build (zero warnings), `ctest` 1/1 Passed,
  runner `[SUMMARY] 93 tests, 0 failures` (>75 from 4.2), CheckClassDocumentation
  passed (20 files), CheckDependencyBoundaries passed (1 package, 11 files),
  `clang-format --style=file:clang-format --dry-run --Werror` exit 0 (clean). Files
  changed: `lib/microworld-net/include/MicroWorld/Net/NetHost.h` (new),
  `lib/microworld-net/tests/NetHostTests.cpp` (new),
  `lib/microworld-net/CMakeLists.txt`, `MICROWORLD_ROADMAP.md`. No `PROGRESS.md`
  row at 4.3 (deferred to phase close; added when 4.4 completed Phase 4).

- [x] **4.4 Wire `TNetHost` into `TEngineHost`.** Optional slot: a
  `TEngineHost` constructor overload accepts a `TNetHost&` (caller-owned) and
  the canonical frame order from section 4 becomes fully live (receive pump
  first, send pump last). Add one engine-level test: two `TEngineHost`
  instances over loopback exchange a message that spawns an actor on the
  server world — the "concept proof" that net + engine compose.

  **Done when:** test passes; frame order documented in `EngineHost.h`.

  **Completed (2026-07-21):** `CheckDependencyBoundaries.py` forbids `Engine → Net`
  (Engine may reach only Core/Memory/Object), so the literal `TNetHost&` parameter
  cannot be written; instead added engine-owned `Engine/NetworkFrame.h` with
  `INetworkFrame` (`TickDispatch`/`TickFlush`, mirroring UE5 `UNetDriver`) plus a
  caller-side `TNetHostFrame<TNet>` adapter forwarding to `PumpReceive`/`PumpSend`.
  `TEngineHost` gained an `INetworkFrame&` constructor overload (null default keeps
  the slot optional, so every existing standalone host is unchanged); `Tick` steps
  1 and 7 now drive the frame, and the full seven-step order is documented in
  `EngineHost.h`. The concept proof
  (`EngineNetHostClientMessageSpawnsActorOnServerWorld`) drives two `TEngineHost` +
  two `TNetHost` over one `FHostLoopback` through the live frame slots: the client
  connects, then a channel-1 message dispatches server-side and spawns an actor in
  the server world at the same frame's barrier (asserted via begin-count and store
  occupancy; the client spawns nothing). A second case
  (`EngineHostTickDrivesBoundNetworkFrameDispatchThenFlush`) pins dispatch-before-
  flush ordering and that a rejected tick drives neither slot. Net links PRIVATE
  into the engine TEST target only (guarded `add_subdirectory`); production
  `microworld_engine` stays net-free. Verify evidence: clean GCC 16.1.0 build (zero
  warnings), `ctest` 1/1 Passed, runner `[SUMMARY] 80 tests, 0 failures` (78 from
  3.4, +2), CheckClassDocumentation passed (26 files), CheckDependencyBoundaries
  passed (3 packages, 33 files — Engine still net-free),
  `clang-format --style=file:clang-format --dry-run --Werror` exit 0 (clean; also
  fixed one pre-existing `RegisterClass<T>` line wrap). Files changed:
  `lib/microworld-engine/include/MicroWorld/Engine/NetworkFrame.h` (new),
  `lib/microworld-engine/include/MicroWorld/Engine/EngineHost.h`,
  `lib/microworld-engine/tests/EngineNetHostTests.cpp` (new),
  `lib/microworld-engine/CMakeLists.txt`, `lib/microworld/PROGRESS.md`,
  `MICROWORLD_ROADMAP.md`. Phase 4 (4.1–4.4) complete.

---

### Phase 5 — Platform adapters (ESP32 + host) 🟨

Goal: MicroWorld runs on the real board. New packages; portable packages stay
platform-free (dependency checker must keep passing).

- [x] **5.1 `microworld-platform-host` package.** `lib/microworld-platform-host/`
  with: `FHostTimeSource` (steady_clock → ms since start) and
  `FHostUdpDriver` implementing `INetDriver` v2 over BSD/WinSock UDP
  (non-blocking sockets; `FNetAddress` = IPv4 + port). CMake + tests (two
  drivers on localhost exchange packets; `Unavailable` on empty;
  `Invalid`/`Full` mapping documented and tested where the OS allows).

  **Done when:** a host demo sends `TNetHost` traffic over real UDP localhost.

  **Completed (2026-07-21):** Added the first **non-portable** platform package
  `lib/microworld-platform-host/`, excluded by design from
  `CheckDependencyBoundaries.py` (it may include OS socket headers; the checker
  governs only Core/Memory/Object/Engine/Net). Deliverables: `FHostTimeSource`
  (header-only `steady_clock` → `TimePointMilliseconds`, the single real clock the
  engine consumes), `MakeUdpAddress`/`IsUdpAddress`/`UdpAddressPort` (this
  package's 6-byte IPv4+port encoding of the opaque `FNetAddress`, mirroring the
  `MakeLoopbackAddress` precedent), `FWinSockScope` (refcounted RAII so N drivers
  share one `WSAStartup`/`WSACleanup`; POSIX no-op; non-thread-safe by design),
  and `FHostUdpDriver final : INetDriver` (one non-blocking `SOCK_DGRAM` on
  `127.0.0.1`, ephemeral port via bind-0 + `getsockname`). All OS socket headers
  are confined to `src/UdpSocketGlue.h` (the sole home of `<winsock2.h>` /
  `<sys/socket.h>`); the four public headers stay platform-free. Every op is
  transactional — arguments validated before any syscall, `BytesReceived`/`OutFrom`
  left unchanged on any non-`Success`; the receive path peeks into an internal
  1200-byte scratch (`MSG_PEEK`, `WSAEMSGSIZE`/`MSG_TRUNC`) to size the head
  datagram without consuming or touching the caller's destination, so `Full`
  leaves both the destination and the queue untouched. The Done-when proof
  (`HostNetHandshakeAndApplicationMessageCrossRealUdp`) drives two `TNetHost<4,256>`
  over two `FHostUdpDriver` on localhost: the client reaches `Connected`, the server
  admits one peer, then one channel-1 app message dispatches server-side with the
  exact bytes. Driver contract tests cover two distinct ephemeral sockets, a full
  byte/exact-sender round trip, `Unavailable` on empty, `Invalid` (null-span-nonzero,
  oversize, non-UDP address), transactional `Full` (destination bytes + `BytesReceived`/
  `OutFrom` all unchanged, queue-not-consumed proven by a later larger read), and
  `MaxPacketBytes()==1200`. Note: the concept doc's "~1200
  byte" `MaxPacketBytes` is honored literally; the end-to-end test's app payload is
  kept well under 256 so real UDP datagrams never exceed the `TNetHost<4,256>`
  `PumpReceive` scratch buffer. POSIX glue is compiled behind `#ifdef` but
  unverified on this Windows-only host (deferred to 5.2's POSIX-like build).
  Verify evidence: clean GCC 16.1.0 build (zero warnings), `ctest` 1/1 Passed,
  runner `[SUMMARY] 7 tests, 0 failures`, CheckClassDocumentation passed (9 files),
  CheckDependencyBoundaries passed (5 packages, 51 files — platform-host excluded
  by design), `clang-format --style=file:clang-format --dry-run --Werror` exit 0
  (clean). Files changed:
  `lib/microworld-platform-host/include/MicroWorld/PlatformHost/HostTimeSource.h` (new),
  `lib/microworld-platform-host/include/MicroWorld/PlatformHost/UdpAddress.h` (new),
  `lib/microworld-platform-host/include/MicroWorld/PlatformHost/WinSockScope.h` (new),
  `lib/microworld-platform-host/include/MicroWorld/PlatformHost/HostUdpDriver.h` (new),
  `lib/microworld-platform-host/src/HostUdpDriver.cpp` (new),
  `lib/microworld-platform-host/src/UdpSocketGlue.h` (new),
  `lib/microworld-platform-host/tests/HostTimeSourceTests.cpp` (new),
  `lib/microworld-platform-host/tests/HostUdpDriverTests.cpp` (new),
  `lib/microworld-platform-host/tests/HostNetEndToEndTests.cpp` (new),
  `lib/microworld-platform-host/CMakeLists.txt` (new), `MICROWORLD_ROADMAP.md`.
  No `PROGRESS.md` row (phase not yet ✅). Tasks 5.2–5.3 not started.

- [x] **5.2 `microworld-platform-esp32` package.** `lib/microworld-platform-esp32/`
  (ESP-IDF component style, PlatformIO-compatible like the existing consumer
  probes): `FEsp32TimeSource` (`esp_timer_get_time()/1000`), `FEsp32UdpDriver`
  (lwIP non-blocking UDP, same address encoding as host driver), a log sink
  adapter to `ESP_LOG`, and an `app_main` glue example running `TEngineHost`
  at a fixed cadence via `vTaskDelay`. **Compile-only evidence** (like existing
  `Esp32S3N16R8.md` records); no upload without explicit authorization.

  **Verify:**
  ```sh
  pio run -d lib/microworld/tests/consumer -e esp32-s3   # existing probes still build
  # plus the new esp32 example environment added by this task
  ```
  **Done when:** ESP32 image builds; RAM/flash recorded in the package's
  `benchmarks/Results/Esp32S3N16R8.md`.

  **Completed (2026-07-21):** Added the second **non-portable** platform package
  `lib/microworld-platform-esp32/`, the ESP32 sibling of the host adapter
  shipped in 5.1 and likewise excluded by design from
  `CheckDependencyBoundaries.py` (it may include lwIP/ESP-IDF headers; the
  checker governs only Core/Memory/Object/Engine/Net). Deliverables:
  `FEsp32TimeSource` (`esp_timer_get_time()/1000` → `TimePointMilliseconds`,
  the single real clock the engine consumes; no baseline — `esp_timer` is
  already monotonic-since-boot), `MakeUdpAddress`/`IsUdpAddress`/
  `UdpAddressPort` (byte-identical 6-byte IPv4+port encoding of the opaque
  `FNetAddress`, intentionally duplicated from the host adapter so each
  platform package is self-contained), `Esp32LogSink` (free function matching
  `FLogSink`, mapping `ELogLevel` → `ESP_LOGE/W/I/V` with `Category` as the
  ESP-IDF tag), and `FEsp32UdpDriver final : INetDriver` (one non-blocking
  `SOCK_DGRAM` on `INADDR_ANY`, ephemeral port via bind-0 + `getsockname`,
  no netif/WiFi initialized). All lwIP/ESP-IDF socket headers are confined to
  `src/Esp32SocketGlue.h` (sole home of `<lwip/sockets.h>`); the four public
  headers stay platform-free. Every op is transactional — arguments validated
  before any syscall, `BytesReceived`/`OutFrom` left unchanged on any
  non-`Success`; the receive path ports the 5.1 fix verbatim: the sizing peek
  reads into an internal 1200-byte scratch (never the caller's destination), so
  `Full` leaves both the destination and the queue untouched. The MSG_TRUNC
  caveat is documented: when lwIP defines `MSG_TRUNC` the peek returns the true
  length, otherwise it sizes from the delivered length and the exact oversize
  behavior is UNVERIFIED at runtime (compile-only phase); a `static_assert`
  ties `PeekScratchBytes` to `UdpMaxPacketBytes`. The Done-when proof
  (`lib/microworld/tests/consumer/src/PlatformEsp32Main.cpp`) composes the full
  stack — `SetLogSink(&Esp32LogSink)` → `FEsp32TimeSource` → `FEsp32UdpDriver`
  → `TNetHost<4,256>` (DedicatedServer) → `TNetHostFrame` → `TEngineHost<6,8,
  256,16,1,2,4,64>` (same template args as the Engine profile probe), then
  ticks at 20 ms via `vTaskDelay(pdMS_TO_TICKS(20))`; it is a
  compile/composition proof only (no netif/WiFi, so no packet can flow).
  Verify evidence: clean Xtensa-ESP-ELF GCC 15.2.0 build, `[SUCCESS]` in 62.44
  s, **RAM 21,772 / 327,680 bytes (6.6%)**, **Flash 283,473 / 4,194,304 bytes
  (6.8%)**; regression rebuilds of `esp32-s3-engine` (74.63 s) and `esp32-s3-net`
  (74.91 s) both `[SUCCESS]`; CheckClassDocumentation passed (7 files);
  CheckDependencyBoundaries passed (5 packages, 51 files — platform-esp32
  excluded by design); `clang-format --style=file:clang-format --dry-run
  --Werror` exit 0. POSIX/lwIP caveat: the platform env is the first consumer
  to `#include` ESP-IDF headers from C++, whose `esp_libc`/lwIP shims use the
  GCC `#include_next` extension that `-Wpedantic` flags; the env keeps the full
  strict set but adds `-Wno-error=pedantic` scoped to that one env in
  `src/CMakeLists.txt` (analogous to the existing `-Wno-deprecated-declarations`
  for the libstdc++ `std::aligned_storage` issue). Files changed:
  `lib/microworld-platform-esp32/library.json` (new),
  `lib/microworld-platform-esp32/include/MicroWorld/PlatformEsp32/Esp32TimeSource.h` (new),
  `lib/microworld-platform-esp32/include/MicroWorld/PlatformEsp32/UdpAddress.h` (new),
  `lib/microworld-platform-esp32/include/MicroWorld/PlatformEsp32/Esp32LogSink.h` (new),
  `lib/microworld-platform-esp32/include/MicroWorld/PlatformEsp32/Esp32UdpDriver.h` (new),
  `lib/microworld-platform-esp32/src/Esp32UdpDriver.cpp` (new),
  `lib/microworld-platform-esp32/src/Esp32LogSink.cpp` (new),
  `lib/microworld-platform-esp32/src/Esp32SocketGlue.h` (new),
  `lib/microworld-platform-esp32/benchmarks/Results/Esp32S3N16R8.md` (new),
  `lib/microworld/tests/consumer/src/PlatformEsp32Main.cpp` (new),
  `lib/microworld/tests/consumer/platformio.ini`,
  `lib/microworld/tests/consumer/src/CMakeLists.txt`, `MICROWORLD_ROADMAP.md`.
  No `PROGRESS.md` row (phase not yet ✅). Task 5.3 not started.

- [x] **5.3 E32 LoRa UART driver.** `FEsp32E32LoraDriver` in the esp32 package:
  UART framing `[u8 0xA5][u8 SrcNodeId][u16 Len][payload][u16 CRC16-CCITT]`,
  bounded RX state machine (resync on bad magic/CRC), `FNetAddress` = 1-byte
  node id, respects E32 payload limits via `MaxPacketBytes()`. Host-side unit
  tests for the framing state machine (feed byte streams incl. corruption,
  truncation, resync) — the state machine must be a portable class in
  `microworld-net` (`Net/FrameCodec.h`) so it is testable off-target; only the
  UART glue lives in the esp32 package.

  **Done when:** FrameCodec host tests pass; esp32 package compiles with the
  driver; no radio transmission performed.

  **Completed (2026-07-21):** Added the E32 LoRa transport in two parts per the
  task spec. **Part A — portable framer in `microworld-net`:** header-only
  `Net/FrameCodec.h` with `ComputeCrc16Ccitt` (CRC-16/CCITT-FALSE: poly
  `0x1021`, init `0xFFFF`, no input/output reflection, xorout `0x0000`;
  canonical check value 0x29B1 asserted), the transactional free function
  `EncodeFrame` (validates before writing; `Invalid` for null-span-nonzero,
  `Full` when the payload cannot fit the destination or the u16 length field;
  leaves `OutFrame` and `OutWritten` unchanged on any non-`Success`), the
  `EFrameEvent` enum (`None`/`FrameReady`/`Discarded`), and the bounded
  `TFrameDecoder<MaxPayloadBytes>` template state machine
  (`WaitingForMagic → ReadingSrcNodeId → ReadingLenHi → ReadingLenLo →
  ReadingPayload → ReadingCrcHi → ReadingCrcLo`) that accumulates the CRC
  incrementally over the source node id, both length bytes, and each payload
  byte, resyncing to `WaitingForMagic` on a non-magic byte, an oversize
  declared length, or a CRC mismatch. The resync guarantee and its honest
  truncation non-guarantee (the frame immediately after a truncated frame may
  be consumed as that frame's payload and lost, recovering within one frame)
  are documented in the class contract. Nine host cases in
  `tests/FrameCodecTests.cpp`: the canonical CRC check value, encode→decode
  round trips for payload sizes 0/1/Max including a `0xA5` payload byte,
  transactional `Invalid`/`Full` rejection with `OutWritten` unchanged, leading
  non-magic garbage then a valid frame, two back-to-back frames in order, a
  corrupted CRC byte discarded then a valid frame, an oversize declared length
  discarded then a valid frame, the documented truncated-frame resync, and a
  steady-state no-allocation round trip. `FrameCodec.h` depends only on
  Core/Memory/std so `CheckDependencyBoundaries` stays green. **Part B — ESP32
  UART glue in `microworld-platform-esp32`:** `LoraAddress.h` (the 1-byte LoRa
  `FNetAddress` encoding mirroring `UdpAddress.h`), `Esp32E32LoraDriver.h`
  (`class FEsp32E32LoraDriver final : INetDriver` with a `FEsp32E32LoraConfig`
  struct, `E32MaxPayloadBytes == 58`, a held-by-value `TFrameDecoder`, and a
  platform-free public header), `src/Esp32E32LoraDriver.cpp`
  (validate-then-syscall; `TrySend` encodes into a stack buffer then maps the
  glue outcome `Sent→Success / WouldBlock→Full / Error→Invalid`; `TryReceive`
  delivers a held frame first, else pumps the UART one byte at a time bounded
  by `2*(E32MaxPayloadBytes+FrameOverheadBytes)`, logging a `Discarded` and
  continuing), and `src/E32UartGlue.h` (the SOLE home of `<driver/uart.h>`;
  `namespace MicroWorld::Detail`; `FUartPort = uart_port_t` so call sites need
  no implicit conversion; outcome enums `EUartWriteOutcome`/
  `EUartReadStatus`; `OpenConfiguredUartPort` 8N1 with rollback-on-failure; the
  exact would-block/drain behavior documented as runtime-UNVERIFIED in the
  compile-only phase, mirroring the 5.2 UDP `MSG_TRUNC` caveat). The LoRa
  addressing model is documented as broadcast: the frame carries only the
  sender's node id, `To` is validated but the wire is broadcast, and
  per-destination routing is a higher-layer concern. The Done-when proof
  extends `PlatformEsp32Main.cpp` to additionally construct one
  `FEsp32E32LoraDriver` (UART_NUM_1, GPIO17/18, 9600 baud, node 1) so the
  driver object compiles and links; it is NOT ticked (compile-only; no UART
  traffic, no radio, no upload). The existing UDP composition is untouched.
  Verify evidence: clean GCC 16.1.0 host build (zero warnings), `ctest` 1/1
  Passed, runner `[SUMMARY] 102 tests, 0 failures` (+9 FrameCodec cases over
  5.2's 93); clean Xtensa-ESP-ELF GCC 15.2.0 build, `esp32-s3-platform`
  `[SUCCESS]` in 7.23 s, **RAM 21,804 / 327,680 bytes (6.7%)**, **Flash
  309,921 / 4,194,304 bytes (7.4%)** (+32 bytes RAM, +26,448 bytes flash over
  the 5.2 UDP-only image); `CheckClassDocumentation.py` passed (Net 22 files,
  platform-esp32 11 files); `CheckDependencyBoundaries.py` passed (5 packages,
  52 files — platform-esp32 excluded by design); `clang-format --style=file:
  clang-format --dry-run --Werror` exit 0. Files changed:
  `lib/microworld-net/include/MicroWorld/Net/FrameCodec.h` (new),
  `lib/microworld-net/tests/FrameCodecTests.cpp` (new),
  `lib/microworld-net/CMakeLists.txt`,
  `lib/microworld-platform-esp32/include/MicroWorld/PlatformEsp32/LoraAddress.h` (new),
  `lib/microworld-platform-esp32/include/MicroWorld/PlatformEsp32/Esp32E32LoraDriver.h` (new),
  `lib/microworld-platform-esp32/src/Esp32E32LoraDriver.cpp` (new),
  `lib/microworld-platform-esp32/src/E32UartGlue.h` (new),
  `lib/microworld-platform-esp32/library.json`,
  `lib/microworld/tests/consumer/src/PlatformEsp32Main.cpp`, `MICROWORLD_ROADMAP.md`.
  PROGRESS.md Phase 5 row added below. Phase 5 (5.1–5.3) complete.

---

### Phase 6 — Examples, measurement & release hardening 🟨

- [x] **6.1 Two-node demo.** `examples/` app: dedicated server on host (UDP),
  ESP32 or second host process as client; client button/keyboard event spawns
  an actor server-side, server broadcasts state at a heartbeat cadence.
  This is the acceptance demo for "UE5 dev can build a small networked thing".
  **Completed (2026-07-21):** single host executable
  `lib/microworld-platform-host/examples/TwoNodeDemo/Main.cpp` composes a
  dedicated-server `TEngineHost` and a bare `TNetHost` client over real
  localhost `FHostUdpDriver`s in one deterministic interleaved loop. Client
  channel-1 input events spawn actors in the server world; the server
  broadcasts a 2-byte state payload `{tick, actor-count}` on channel 2 each
  step. Evidence: clean strict-gate build (`-Wall -Wextra -Wpedantic -Werror
  -fno-exceptions -fno-rtti`, no warnings); existing
  `microworld_platform_host_tests` still `[SUMMARY] 7 tests, 0 failures`; the
  demo trace is byte-identical across three runs and exits 0;
  `CheckClassDocumentation.py` (10 files) and `CheckDependencyBoundaries.py`
  (5 packages, 52 files) pass; `clang-format --dry-run --Werror` clean.

- [x] **6.2 Measure runtime margins.** Every PROGRESS.md row says "target
  runtime margins unmeasured". Measure on ESP32-S3 (requires explicit human
  authorization to flash): tick duration for a representative world (e.g. 8
  actors / 16 components / 8 timers / GC budget slice), max GC pause per
  budget unit, net pump cost, RAM/flash of the full image. Record in the
  package `benchmarks/Results/` files and PROGRESS.md.
  **Part A landed (2026-07-21):** the compile-verified on-target measurement
  harness is `lib/microworld/tests/consumer/src/Esp32BenchmarkMain.cpp`, and
  `[env:esp32-s3-benchmark]` is re-pointed at the full platform-esp32 stack
  (Engine+Net+PlatformEsp32, `-Os`, release) with a single-file
  `build_src_filter`. It builds the representative world (8 actors / 16
  components / 8 timers, all spawned in setup before BeginPlay so the
  in-loop GC slice never hits the mutation lock), a standalone
  `FObjectStore`+`FGarbageCollector` probe sized so one `Advance` is a
  measurable slice (budget `{1,1,8}` over 32 slots), and a no-traffic net
  pump, then prints labeled lines over serial at 115200:
  `tick` (min/mean/max µs over 1000 iterations), `gc` (min/mean/max µs per
  slice + slices-in-cycle + budget), `net_pump` (mean µs, labeled
  no-traffic overhead), and `mem` (free heap before/after setup + stack
  high-water mark). Static image RAM/Flash are read from the build summary
  (benchmark env: RAM 6.6% / 21,772 B, Flash 6.9% / 289,217 B). **No
  measured runtime numbers yet** — Part B (human-authorized flash) captures
  them. Capture commands:
  ```sh
  pio run -d lib/microworld/tests/consumer -e esp32-s3-benchmark -t upload
  pio device monitor -d lib/microworld/tests/consumer -e esp32-s3-benchmark
  ```
  then transcribe the labeled lines into
  `lib/microworld-platform-esp32/benchmarks/Results/Esp32S3N16R8.md`.
  **Part B landed (2026-07-21):** flashed to a connected ESP32-S3 (COM5, USB
  `303A:1001`) under explicit human authorization and captured the labeled
  serial lines. Measured @ 160 MHz, release `-Os` (fixed image RAM 43,148 B /
  Flash 313,269 B): tick min 62 / mean 73 / max 114 µs (1000 iterations, one
  full GC cycle per tick); GC Advance-slice min 21 / mean 25 / max 39 µs (budget
  `{1,1,8}`, 4 slices/cycle); no-traffic net pump mean 47 µs; world-setup heap
  580 B; main-task stack 2,476 B free after setup. Recorded in the results file
  above and PROGRESS.md. Two runtime defects surfaced only on hardware (both
  invisible to the compile-only Part A proof) and were fixed in the harness —
  see the §6 decision row dated 2026-07-21 (Phase 6.2 Part B).

- [x] **6.3 Documentation release sweep.** Update: `lib/microworld/README.md`,
  `lib/microworld-engine/README.md`, `lib/microworld-net/README.md`,
  `UE5ConceptMap.md` (add Spawn/Destroy, TEngineHost, ENetMode/TNetHost rows),
  `Porting.md` (how to write a platform adapter: time source + net driver +
  log sink = one page), `PROGRESS.md` final status, `CHANGELOG.md`, bump
  version to `0.2.0` in `Version.h`. Run all checker scripts.
  **Completed (2026-07-21):** documentation-only sweep reflecting the shipped
  0.2.0 reality plus the version bump. `Version.h` → `0.2.0`; package
  `project(... VERSION ...)` and `library.json` version fields bumped across
  all seven packages; the four Core consumer probes' `Version.Minor`
  static_asserts moved to 2 so the host-core build still links. Core/Engine/
  Net READMEs, `UE5ConceptMap.md`, `Porting.md` (rewritten as a one-page
  three-seam adapter guide), `PROGRESS.md` (0.2.0 release-ready), and
  `CHANGELOG.md` (`## 0.2.0 - 2026-07-21` entry) updated; root `README.md`
  moved to 0.2.0. Added `lib/microworld/docs/diagrams/AGENTS.md` to close the
  pre-existing `CheckFolderAgents.py` gap. The 0.1.0 CHANGELOG history entry
  and dated `benchmarks/Results/` evidence were left intact. Verify gate
  (GCC 16.1.0 via Ninja): `CheckClassDocumentation.py`,
  `CheckFolderAgents.py`, `CheckDependencyBoundaries.py --self-test` +
  `--package Core=lib/microworld` pass; `clang-format --dry-run --Werror` on
  `Version.h` clean; `host-core` CTest 5/5 and `host-eng` CTest 1/1 pass;
  full portable-package dependency sweep passes (5 packages, 52 files).

- [x] **6.4 Final acceptance.** All packages build + test on host; ESP32
  images compile; dependency/doc checkers pass; the two-node demo runs; this
  document's tracker is fully ✅ except this line, which flips last.
  **Completed (2026-07-22):** independent lead-engineer acceptance sweep.
  Six host packages configure/build/ctest green under strict warnings
  (`-Wall -Wextra -Wpedantic -Werror`; Core 5/5, the rest via their aggregate
  runners). All seven consumer ESP32 envs compile (`esp32-s3`, `-memory`,
  `-object`, `-engine`, `-net`, `-platform`, `-benchmark`); `-platform` passed
  on an isolated rebuild after a transient bootloader-subproject race in the
  back-to-back batch (the same file compiled cleanly in this env's app tree
  and in `-benchmark`). `CheckClassDocumentation.py` (31 files),
  `CheckFolderAgents.py` (17 guides), and `CheckDependencyBoundaries.py`
  (5 packages, 52 files) pass. The host two-node UDP demo runs to completion
  (exit 0: connect → spawn ×2 → state broadcast → client receipt). This is the
  0.2.0 release.

---

## 6. Design decisions record

| Date | Decision | Chosen by |
| --- | --- | --- |
| 2026-07-20 | Retire Core actor model; Engine layer is the only World/Actor/Component API | Owner |
| 2026-07-20 | Runtime SpawnActor/DestroyActor is a must-have (deferred-barrier design) | Owner |
| 2026-07-20 | Networking = simple channel messages with roles (no replication/RPC); transports: UDP first (host-testable), E32 LoRa second, ESP-NOW deferred | Owner |
| 2026-07-20 | Components fixed at actor construction; no runtime component add/remove | Plan default |
| 2026-07-20 | GC stays optional-but-default via TEngineHost budget; store+GC design kept as-is | Plan default |
| 2026-07-20 | **Merge tasks 1.2 + 1.3 into one combined step** (delete the retired headers/sources *and* migrate/delete every retired-type dependent together), because 1.2's Done-when (green `build/host-core`) cannot hold while 1.3's dependents still `#include` the deleted headers. The Core build goes green once, at the end of the combined step. | Owner |
| 2026-07-20 | **PROPOSED (awaiting owner):** `lib/microworld/docs/diagrams` has no `AGENTS.md`, so the prescribed `CheckFolderAgents.py` strict run fails. Pre-existing (predates Phase 1), unrelated to the retirement. Options: (a) add a short `docs/diagrams/AGENTS.md`, (b) add `--exclude diagrams` to the prescribed invocation, or (c) accept it as a generated-assets directory needing no guide (per `tools/AGENTS.md`, the folder check is "not a policy requiring every future package subdirectory to add a local guide"). | PROPOSED |
| 2026-07-21 | Phase 2.2 marking of a destroyed actor's components: the store blocks `MarkPendingDestroy` under the dispatch guard `DispatchEndPlay` runs within, and that method is shared with non-destroying world `EndPlay`. Chose **option A** (simplest that works): keep `DispatchEndPlay` pure; `ApplyPending` ends doomed actors under the guard, then marks their components + the actor for destroy after releasing it (new `UWorld`-friend `AActor` helper). Option B (unguarded destroy cascade + a "being destroyed" flag) rejected — it loses reentrancy protection. | Owner |
| 2026-07-21 | **RESOLVED — who reclaims a destroyed actor's slot.** Surfaced writing 2.3: section 4's old frame order and section 2's design assumed the incremental collector reclaims released actors, but `FGarbageCollector`'s sweep explicitly *skips* pending-destroy slots — those are reclaimed only by the store's `ApplyPendingDestroy` barrier, so the frame order was missing a step (GC alone never frees a destroyed actor). Owner chose **option (a)** (simplest + most reliable): add an explicit, bounded `Store.ApplyPendingDestroy(Budget)` reclamation slice to the `TEngineHost` frame order (now section 4 step 5), keeping `UWorld::ApplyPending` single-purpose and the already-tested Phase 2 code untouched. Rejected: (b) run reclamation inside `UWorld::ApplyPending` — couples the structural barrier to store reclamation and buries the budget; (c) reword only — leaves GC unable to reclaim destroyed actors. Implemented as a spec in section 4; wired for real in Phase 3.2. | Owner |
| 2026-07-21 | **RESOLVED — `TEngineHost` needs a descriptor lookup.** Surfaced writing 3.3: the 3.2 spec exposed `RegisterClass` but no lookup, and the object store's construction validation requires the descriptor passed to `NewObject` to be the registry's OWN copy (identity check) with `Parent` pointing at the registry's own parent copy (`HasValidParentChain`). Without a lookup, user actors/components cannot be created through the host — which 3.3 requires. Chose **option (a)** (minimal): add one public `const FClassDescriptor* TEngineHost::FindClass(FTypeId) const noexcept` that forwards to `TClassRegistry::Find`, matching how `TEngineEnvironment::FindDescriptor` already exposes the same access for the test fixture. Rejected: (b) variadic auto-register-and-construct helper (`Host.NewObject<FDeviceActor>(id, name, parent_id, ...)` that registers + looks up + constructs in one call) — convenient but hides the descriptor-identity invariant behind a magic helper and over-reaches the 3.2 scope. Applied in the same 3.3 commit; noted in the 3.3 completion note. | Owner |
| 2026-07-21 | **RESOLVED — `TEngineHost` user-type creation ergonomics; owner chose option (a) (add both helpers as Phase 3.4).** Follow-on from the 2026-07-21 `FindClass` row above: the Phase 3 goal states a hello-world app is ~20 lines, but `examples/HostLifecycle/Main.cpp` needs ~46 lines because each user type repeats a build-descriptor → `RegisterClass` → `FindClass` → `NewObject` dance. The minimal `FindClass` accessor was correct for 3.3 scope, but it leaves the canonical app verbose. Candidate (mirrors the existing `TEngineEnvironment::RegisterDerivedClass`/`CreateDerivedObject` test-fixture API, so there is precedent): add two `TEngineHost` template helpers — `template<typename T> EObjectResult RegisterClass(FTypeId, const char* Name)` that derives the parent from `T`'s base via the engine class ids, builds the descriptor with `&TraceManagedObjectReferences`, and registers it; and `template<typename T, typename... A> TObjectCreationResult<T> CreateObject(FTypeId, A&&...)` that folds `FindClass` + `NewObject` into one call. This would cut the hello-world body toward the ~20-line goal at the cost of more `TEngineHost` API surface (two more public templates) and one more place that encodes the "which base parent" rule. Options: (a) add both helpers as a new **Phase 3.4** task (keeps Phase 3 closed; small, testable, documented); (b) defer to Phase 6 (examples/measurement) where the ergonomics gap is felt most and can be judged against real ESP32 apps; (c) do not add — keep `FindClass` as the only public creation path and accept the verbosity as a one-time setup cost. | Owner |
| 2026-07-21 | **Phase 4.1 loopback object model + `FNetAddress` encoding.** The v2 `INetDriver::TryReceive(FNetAddress& OutFrom, …)` carries no per-call endpoint identity, so a single shared loopback object cannot know which mailbox to drain. Chose the **shared-mailroom** model (owner): one `FHostLoopback<MaxPorts, MailboxCapacity, PacketBytes>` owns N per-port mailboxes AND N embedded per-port `INetDriver`s; `INetDriver& Port(index)` hands out the driver bound to a 1-byte loopback address equal to that port index. Two hosts share one network and each hold their own `Port(i)` — fits 4.3/4.4's one-driver-per-host wiring, and port lifetimes are automatic (ports live inside the network). Rejected: fabric + separately-constructed endpoint objects (adds a caller lifetime-ordering landmine); peer-linked mesh (O(N²) wiring). `FNetAddress` = `std::array<uint8_t,12> Bytes` + `uint8_t Size` + `==`/`!=`; loopback encodes exactly one byte = the port index. Internal decomposition: a `Detail::FLoopbackMailboxes` holds the FIFOs + routing so each per-port driver stores only a complete-type pointer (avoids the nested-in-incomplete-template pitfall). | Owner |
| 2026-07-21 | **Phase 5.1 first non-portable platform package; UDP address encoding; transactional Full.** Three decisions lifted from `.claude/concepts/platform-host-udp.md`: (1) `microworld-platform-host` is a **non-portable platform package** — excluded from `CheckDependencyBoundaries.py` (no `platform-host` module key; OS socket headers would fail the vendor rule anyway), may include OS socket headers; (2) the UDP `FNetAddress` encoding is 6 bytes (4 IPv4 octets + 2 big-endian port bytes), owned by this package's `MakeUdpAddress`/`IsUdpAddress`/`UdpAddressPort` helpers since `FNetAddress` is deliberately opaque; (3) a receive into a too-small destination returns `Full` **without consuming the datagram or touching the caller's destination** — the sizing peek reads into an internal 1200-byte scratch (`MSG_PEEK`+`MSG_TRUNC` on POSIX returns the true length; Windows `MSG_PEEK` returns the delivered length and `WSAEMSGSIZE` becomes a sentinel "does not fit"), so the single fits-vs-`Full` decision in the driver sees one uniform signal and the `INetDriver` transactional contract holds on both platforms. Cross-platform scope: both Windows + POSIX glue written now behind `#ifdef`; POSIX unverified on this Windows-only host (deferred to 5.2). | Owner |
| 2026-07-21 | **Phase 5.2 second non-portable platform package; intentionally duplicated UDP address encoding.** (1) `microworld-platform-esp32` is the second **non-portable platform package** — excluded from `CheckDependencyBoundaries.py` for the same reason as `platform-host` (it includes lwIP/ESP-IDF headers), and is the template the E32 LoRa adapter (5.3) will extend. (2) The UDP `FNetAddress` encoding (6 bytes = 4 IPv4 octets + 2 big-endian port bytes) is **duplicated verbatim** from `platform-host` rather than shared: each platform adapter is self-contained, the encoding is a node-local representation that never crosses the wire, and the two packages are never linked into one binary, so there is no interop requirement to match and no shared dependency to introduce. Alternative considered: promote `MakeUdpAddress`/`IsUdpAddress`/`UdpAddressPort` into portable `microworld-net` and have both adapters depend on it. **Rejected** because it would retro-touch a committed, dependency-checked portable package to serve a non-portable concern — the duplication is bounded (three `constexpr` helpers, one file each) and the cost of a future merge (if a third UDP adapter appears) is far lower than the cost of re-opening Net now. (3) The platform env adds `-Wno-error=pedantic` scoped to itself in `src/CMakeLists.txt`: it is the first ESP32 consumer to `#include` ESP-IDF headers from C++, and ESP-IDF's `esp_libc`/lwIP shims use the GCC `#include_next` extension that `-Wpedantic` flags; the full strict set (`-Wall -Wextra -Wpedantic -Werror`) is retained, with only the system-header extension downgraded to a non-fatal warning for this one env (analogous to the existing `-Wno-deprecated-declarations` suppression for libstdc++ `std::aligned_storage`). | Owner |

| 2026-07-21 | **Phase 5.3 E32 LoRa framing split, CRC choice, broadcast addressing, and length-prefixed resync caveat.** (1) The framing state machine is a **PORTABLE** class in `microworld-net` (`Net/FrameCodec.h`, header-only): the bounded `TFrameDecoder<MaxPayloadBytes>` and the transactional `EncodeFrame`/`ComputeCrc16Ccitt` are transport-agnostic and host-tested off-target (9 cases including the canonical check value, corruption, truncation, and resync), while only the UART syscalls (`uart_param_config`/`uart_set_pin`/`uart_driver_install`/`uart_write_bytes`/`uart_read_bytes`) live in the esp32 package's `src/E32UartGlue.h`, the SOLE home of `<driver/uart.h>`. The split keeps `CheckDependencyBoundaries.py` governing the framer (it adds no dependency beyond Core/Memory/std; 52 files pass) while the non-portable glue stays outside its scope by the same 5.1/5.2 precedent. (2) The CRC is **CRC-16/CCITT-FALSE** (poly `0x1021`, init `0xFFFF`, no input/output reflection, xorout `0x0000`) over `[SrcNodeId, LenHi, LenLo, payload…]` (magic and CRC excluded); chosen for its single canonical check value (`0x29B1` for ASCII "123456789", asserted in tests) and its fit for a detect-accidental-corruption role (the protocol layer will add authentication separately — CRC is not authentication). (3) The LoRa addressing model is **broadcast**: the frame carries only the SENDER's node id, the driver is constructed with its own local node id that it stamps on outgoing frames, `To` must be a valid LoRa address (`Size==1`) or `TrySend` returns `Invalid`, but the wire is broadcast — per-destination routing/ACK filtering is a higher-layer concern deliberately not implemented here. `TryReceive` sets `OutFrom = MakeLoraAddress(frame SrcNodeId)`. (4) The length-prefixed resync **guarantee** is that after any corruption (bad magic, oversize length, CRC mismatch) the decoder resyncs and correctly decodes a subsequent well-formed frame; the honest **non-guarantee**, stated in the `TFrameDecoder` contract, is that a length-prefixed framer cannot rewind, so the frame immediately after a *truncated* frame may be consumed as the truncated frame's payload and lost, recovering within one frame (tested by feeding a truncated candidate followed by valid frames and asserting only that a later frame decodes). `FUartPort = uart_port_t` (not `int`) so call sites need no `-fpermissive` enum conversion. The exact would-block/partial-write/drain behavior of the UART syscalls is documented as runtime-UNVERIFIED in the compile-only phase, mirroring the 5.2 `MSG_TRUNC` caveat. (5) **Follow-up fix (review):** `uart_driver_install` requires the RX ring buffer to be strictly greater than `UART_HW_FIFO_LEN` and the TX ring buffer to be zero or strictly greater (`esp_driver_uart/src/uart.c`; `SOC_UART_FIFO_LEN == 128` on ESP32-S3); the initial sizing of `2*(58+6) == 128` sat exactly on the forbidden boundary and would have returned `ESP_FAIL` at runtime, leaving the driver permanently inert. The ring buffers are now sized to `2*UART_HW_FIFO_LEN(Port)`, which clears the floor on any UART, and the duplicated frame-size constants in the glue were removed. Caught by review; masked by the compile-only proof. | Owner |

| 2026-07-21 | **Phase 6.1 single-process two-node demo; GC-budget vs host-sizing invariant.** (1) The acceptance demo runs as **one host executable hosting two nodes** — a dedicated-server `TEngineHost` and a bare `TNetHost` client — driven in **one deterministic interleaved loop** in one process, not as two OS processes. Two processes cannot be a single deterministic acceptance run (no shared logical clock, ports/timing vary per launch); co-hosting mirrors the proven Phase 5.1 `HostNetEndToEndTests` and makes the determinism proof a single runnable command. The client is deliberately a bare `TNetHost` driven by explicit `PumpSend`/`PumpReceive` while the server is a full `TEngineHost` advanced only through `Tick(Now)`, so the demo showcases both the low-level net API and the engine-integrated path side by side. (2) Surfaced at integration: the server's `TEngineHost` profile must satisfy `MaxRoots <= FGarbageCollectionBudget.MaxRootOperations` AND `MaxObjects <= MaxSweepOperations` so one bounded GC slice completes a full mark/sweep cycle every tick. If either bound is violated (e.g. `MaxObjects=16` with the established `{1,4,8}` budget), the incremental GC leaves the store mid-cycle (`ActiveCollector` set) across ticks, and a spawn arriving during that window fails `CreateObject` with `LifecycleLocked` because `IsMutationLocked()` includes `ActiveCollector != nullptr`. The demo therefore mirrors the proven `EngineNetHostTests` profile ratios (`MaxRoots=1`, `MaxObjects=8` against budget `{1,4,8}`) rather than the originally suggested `<8,16,…>` — fixed/bounded as the task allows, and the constraint is documented in the demo header so a future sizing change cannot reintroduce the failure. (3) "world actor count" is a dedicated logical counter incremented in the spawn handler: `UWorld` exposes only pending-spawn/pending-destroy counts and `GetObjectStore().Stats().OccupiedSlots` includes the world object itself, so neither is the honest "live actors in this world" value the broadcast should carry. | Owner |
| 2026-07-21 | **Phase 6.2 Part B — two hardware-only defects in the benchmark harness, found on-target and fixed directly under human flash authorization.** Both were invisible to the compile-only Part A proof and surfaced only when the image first ran on a physical ESP32-S3. (1) **lwIP stack uninitialized before socket use:** `FEsp32UdpDriver`'s `socket()`/`bind()` asserts `tcpip_send_msg_wait_sem … (Invalid mbox)` and panics in a boot loop because `app_main` never started the TCP/IP task; fixed by calling `esp_netif_init()` + `esp_event_loop_create_default()` before constructing the driver (no WiFi association is needed for a bound, pollable socket). The Phase 5.2 `PlatformEsp32Main` composition shares this latent defect but has only ever been compile-verified, so it never surfaced — a cleanup to track. (2) **Composition objects overflowed the 3,584-byte main task stack:** `TEngineHost` embeds its fixed object storage inline (`MaxObjects*SlotBytes` = 32×256 = 8 KB) and the standalone GC probe embeds a 4 KB slot array, so the whole `app_main` frame was reserved on entry and the first Xtensa register-window spill faulted `LoadProhibited` inside `esp_timer_get_time()`. Fixed by moving the driver, net host, frame, engine host, actor-component registries, and GC probe to `static` `.bss` storage — which also matches MicroWorld's bounded caller-owned-storage model (an 8 KB engine host never belongs on a small task stack); after the fix the main task reports 2,476 B stack free. Broader lesson: compile-only ESP32 evidence cannot certify a composition that opens sockets or holds large fixed storage — the composition root must be smoke-run on-target before any runtime-readiness claim. | LEAD (Opus 4.8); fix authorized by Owner |

Add a row here whenever a task forces a design choice not covered by this plan.

---

## 7. Progress tracker

**Update this table and the task checkboxes together.** A phase is 🟨 once any
of its tasks starts, ✅ only when all its tasks are `[x]`.

| Phase | Title | Tasks | Status |
| --- | --- | --- | --- |
| 0 | Baseline & governance | 0.1–0.2 | ✅ |
| 1 | Consolidation: one Actor model | 1.1–1.4 | ✅ |
| 2 | Runtime Spawn & Destroy | 2.1–2.4 | ✅ |
| 3 | Composition root & logging | 3.1–3.4 | ✅ |
| 4 | Networking with roles | 4.1–4.4 | ✅ |
| 5 | Platform adapters | 5.1–5.3 | ✅ |
| 6 | Examples, measurement, release | 6.1–6.4 | ✅ |

**Definition of "production ready" (exit criteria):** Phase 6 task 6.4 checked;
version 0.2.0 tagged; a UE5 developer can, following only the READMEs, build a
networked ESP32 application with worlds, actors, components, timers, spawn/
destroy, and client/server messaging — without reading MicroWorld internals.

---

## 8. Appendix A — UE5 → MicroWorld glossary

For workers who know UE5: the mapping and the deliberate differences.

| UE5 concept | MicroWorld equivalent | Deliberate difference |
| --- | --- | --- |
| `UObject` | `UObject` (microworld-object) | No reflection; explicit `VisitReferences` instead of UPROPERTY tracing |
| `UClass` / `StaticClass()` | `FClassDescriptor` / `StaticClassDescriptor()` | Registered explicitly into `TClassRegistry`; no auto-generation |
| `NewObject<T>()` | `FObjectStore::NewObject<T>()` | Fixed equal-size slots; can fail with `CapacityExceeded` |
| `TObjectPtr` | `TObjectPtr` | Resolves {index, generation} on every access; never caches an address |
| `TWeakObjectPtr` | `TWeakObjectPtr` | Same idea; expires on reclaim |
| `AddToRoot()` / `FGCObject` | `TStrongObjectPtr` | Explicit fixed-capacity root table |
| Garbage collector | `FGarbageCollector` | Incremental, caller-budgeted per tick; never runs behind your back |
| `UWorld` | `UWorld` | Fixed actor capacity chosen at compile time |
| `AActor` | `AActor` | Components fixed at construction; no attachment hierarchy/transform |
| `UActorComponent` | `UActorComponent` | No scene components, no transforms |
| `SpawnActor` / `Destroy()` | `UWorld::SpawnActor` / `DestroyActor` (Phase 2) | Deferred to a per-frame barrier; bounded by world capacity |
| `Tick(DeltaSeconds)` | `Tick(const FTickContext&)` | Milliseconds, caller-supplied clock, per-object intervals, no tick groups |
| `FTimerManager` | `TTimerManager<N, Bytes>` | Fixed capacity; OneShot/Looping only; no catch-up bursts |
| Dynamic delegates / events | `TDelegate` / `TMulticastDelegate` | Fixed binding capacity, inline storage, no heap |
| `UGameInstance` + engine loop | `TEngineHost` (Phase 3) | You call `Tick(now)`; there is no hidden main loop |
| `ENetMode` | `ENetMode` (Phase 4) | Same four roles, same meaning |
| `UNetDriver` / NetConnection | `INetDriver` + `TNetHost` peers (Phase 4) | One driver, bounded peer table, no connection object graph |
| Replication / RPC | **none — by design** | Channel-based messages via `SendTo`/`Broadcast`; you serialize with ByteWriter |
| `FMemory` / allocators | `IMemoryResource` / `TFixedArena` | No global allocator; resources are passed explicitly |
| `TArray` (fixed) / `TArrayView` | `TStaticVector` / `TSpan` | No growth, no heap |

## 9. Appendix B — Common mistakes (read before writing code)

Every one of these has either already been prevented by the current design or
will break the ground rules. Check your diff against this list before claiming
a task done.

1. **Never cache a resolved `UObject*` across a frame, barrier, or GC call.**
   Store `TObjectPtr`/`TWeakObjectPtr`/`FObjectHandle` and re-resolve.
   A raw pointer is valid only within the current call scope.
2. **No `std::function`, `std::vector`, `std::string`, `std::map`, `new`,
   `malloc`.** Use `TDelegate` (inline storage), `TStaticVector`,
   `std::array`, fixed char buffers, `IMemoryResource`.
3. **Member-init order trap:** a base class is constructed before members, so
   a member registry cannot be passed to a base constructor. That is exactly
   why `TInlineActor` uses the base-from-member idiom (private storage base
   before `AActor`). Do not "simplify" it into a member.
4. **Check every result enum.** No `(void)Result;`. Tests must assert the
   exact enum value, not just "not Success".
5. **Transactional failure means: validate everything, then mutate.** If a
   function mutates then discovers failure, that is a bug even if tests pass.
   Use sentinel-value checks in tests to prove untouched state.
6. **No platform headers in portable packages.** `esp_*`, `freertos/*`,
   `lwip/*`, `sys/socket.h` may appear only under `lib/microworld-platform-*`.
   The dependency checker enforces direction, not includes — police this
   manually in review.
7. **Time is caller-supplied u64 milliseconds.** Never call
   `std::chrono::steady_clock`, `esp_timer_get_time`, or any clock inside
   portable packages — that includes tests' production code paths.
8. **Each concrete managed type needs its own `FClassDescriptor`** with a
   unique `FTypeId`, correct parent pointer, and exact size/alignment. This
   includes every `TInlineActor<N>` instantiation you derive from. Forgetting
   the parent chain breaks `IsChildOf` and store validation.
9. **Registration order is contract.** Dispatch, shutdown (reverse), and trace
   order all derive from it. Never replace ordered arrays with anything
   unordered.
10. **`noexcept` everywhere on the public API.** The packages build with
    exceptions disabled; a missing `noexcept` is an API-consistency bug.
11. **Doc comments are mandatory** on every function declaration and
    persistent member — `CheckClassDocumentation.py` fails otherwise. Write
    intent/invariant/ownership, not a restatement of the signature.
12. **Don't widen scope.** If a task tempts you to add a feature from section
    3.4 (out of scope), stop and add a note instead. Smallest usable milestone
    wins.

