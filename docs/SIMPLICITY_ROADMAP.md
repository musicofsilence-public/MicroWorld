# MicroWorld — Simplicity & Readability Roadmap

**Version:** 1.0 · **Date:** 2026-07-22 · **Owner:** Mykola
**Baseline:** commit `04d8e2c` (clean tree), Windows 11 + Visual Studio 2022 (MSVC), root superbuild.
**Scope:** every module under `Modules/` plus the student-facing docs.

MicroWorld must be **the art of simplicity**: it will be read and extended by
students. This document is the active improvement plan and progress tracker for
making the code self-explanatory — names a student can read without a glossary,
functions that do at most two things, comments that answer *why*, and less
ceremony wherever removing it does not weaken the embedded guarantees.

It is written so that any LLM (including a weak one) can pick it up, find the
next task, complete it, and record progress without extra context. The
companion documents are:

- `docs/ROADMAP.md` — the **completed** implementation plan. Historical record,
  frozen verbatim. **Never edit it**, even when a task renames a symbol that it
  mentions.
- `PROGRESS.md` — the live evidence record. Add one short line per finished
  phase (see protocol rule 8).

Baseline evidence (recorded 2026-07-22, all commands from the repo root):

| Gate | Command | Result |
| --- | --- | --- |
| Build + all tests | `cmake --build build --config Release` then `ctest --test-dir build -C Release --output-on-failure` | 11/11 passed (includes format, dependency-boundary, profile-map gates) |
| Class documentation | `python tools/CheckClassDocumentation.py --root Modules --require-doxygen` | passed (122 files) |
| Folder guides | `python tools/CheckFolderAgents.py --root Modules --exclude build --exclude .pio --exclude __pycache__` | **FAILS — 24 folders missing AGENTS.md** (pre-existing; fixed by task 9.1; not a blocker for earlier phases) |

---

## 1. How to use this document (protocol for LLM workers)

Follow these rules exactly:

1. Read section **2 (Ground rules)** before touching any code.
2. Open section **5 (Progress tracker)**. Find the first phase whose status is
   not ✅. Inside that phase, find the first unchecked `[ ]` task.
3. Work on **exactly one task at a time**, in order. Do not start a later phase
   while an earlier phase has unchecked tasks.
4. Every task has **Steps**, a **Done when** checklist, and a **Verify**
   instruction. A task is complete only when every "Done when" item is true and
   every Verify command passes.
5. When a task is complete: change its `[ ]` to `[x]`, append one evidence line
   directly under the task (`Done YYYY-MM-DD — <one sentence of proof>`), and
   update the phase status in the tracker table (⬜ → 🟨 when a phase's first
   task starts, 🟨 → ✅ when its last task finishes).
6. If you are blocked, write `⛔ BLOCKED:` plus one sentence under the task and
   stop. Do not skip ahead.
7. Never delete or rewrite this document's structure. Only update statuses,
   checkboxes, evidence lines, and BLOCKED notes.
8. When a phase reaches ✅: add one short evidence entry to `PROGRESS.md`
   (what changed, how it was verified).

Status legend: ⬜ not started · 🟨 in progress · ✅ done · ⛔ blocked

### 1.1 The Standard Verify

Referenced by almost every task as "**Standard Verify**". Run from the repo
root, in this order:

```sh
clang-format --style=file:clang-format -i <every .h/.cpp file you touched>
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python tools/CheckClassDocumentation.py --root Modules --require-doxygen
```

Expected: the build is warning-clean (warnings are errors), ctest reports
**11/11 passed**, and the documentation checker passes (122 files at baseline;
the count grows when tasks add headers). If `build/` is missing, create it
first with `cmake -S . -B build`.

### 1.2 How to locate code

Every `file:line` reference in this document was verified at baseline commit
`04d8e2c`. Completed tasks shift later line numbers. **Always locate code by
the quoted symbol or text; treat line numbers as hints only.** Search with the
`rg` tool (`rg -n "SymbolName" Modules`), never by opening files at a
remembered offset.

### 1.3 The rename procedure

Every rename task uses this recipe per table row:

1. `rg -n "<OldName>" Modules docs README.md AGENTS.md PROGRESS.md CHANGELOG.md`
   and list the hits.
2. Edit every hit **except the historical files** (rule 1.4). Update code,
   tests, examples, comments, and current docs together.
3. Re-run the same `rg`; the only remaining hits must be inside historical
   files.
4. Run the Standard Verify.

Renames never change behavior: signatures keep the same types, `const`,
`noexcept`, and semantics. Only the identifier text changes.

### 1.4 Files you must never edit

- `docs/ROADMAP.md` — frozen historical plan.
- Existing entries in `CHANGELOG.md` (appending a new entry is allowed).
- `Modules/*/benchmarks/Results/*.md` — measured historical records.
- `LICENSE`, `VERSION` (except task 9.2), anything under `build/` or `.git/`.

Old symbol names remaining inside these files after a rename are correct and
expected.

### 1.5 What "refactor" means here

Unless a task explicitly says "behavior change", every task is
**behavior-preserving**: same observable results, same result enums, same
dispatch order, same memory bounds. The test suite is the referee — if a test
needs its *expectation* changed to pass, stop and re-read the task; only tasks
marked "behavior change" may touch test expectations.

---

## 2. Ground rules (invariants — never violate)

### 2.1 Embedded invariants (inherited from `AGENTS.md`, unchanged)

- **C++17**; builds with exceptions and RTTI **disabled**. No `throw`, no
  `dynamic_cast`, no `typeid`.
- **No hidden allocation** in steady state; storage is caller-owned and
  fixed-capacity. **No hidden clock** — time is caller-supplied
  `TimePointMilliseconds`.
- **Errors are enums**; failures are transactional (a failed call leaves all
  inputs and state unchanged).
- **Determinism**: registration order defines dispatch order; shutdown runs in
  reverse; no catch-up ticks.
- **Dependency direction** (enforced by `tools/CheckDependencyBoundaries.py`):
  `Core <- Memory <- Object <- Engine`, `Core <- Memory <- Net`; only
  PlatformHost / PlatformEsp32 may include OS/SDK headers.
- **Frozen identity**: CMake `project()` names, targets, `MicroWorld::*`
  aliases, `library.json` package names, and **public header file paths**
  under `include/MicroWorld/...` stay exactly as they are. Renaming a *type*
  or *function* is allowed (this plan does it a lot); renaming or deleting a
  *public header file* is not. Adding a new public header is allowed.
- Every function declaration and persistent member keeps a Doxygen `/** */`
  comment (enforced by `tools/CheckClassDocumentation.py --require-doxygen`).
- Format everything you touch with the tracked policy:
  `clang-format --style=file:clang-format -i <files>`.

### 2.2 The three simplicity rules (what this whole plan enforces)

**Rule N — Naming.** A name states the full role of the thing; long and
explicit beats short and clever. Spell out units (`NowMilliseconds`, never
`Now`). Booleans read as a yes/no question (`bMustResetSchedule`, never
`bScheduleReset`). No new abbreviations. Allowed short forms, because they are
industry vocabulary the students must learn anyway: `Id`, `GC` (in comments
only), `Crc`, `Udp`, `Uart`, `Ipv4`, `Lora`, `Io`, and the fixed style
prefixes `F/T/E/I/b/U/A` (`docs/Style.md`). `T` prefix is **only** for class
templates, `F` **only** for non-template classes/structs — a template named
`FSomething` is a style bug (Phase 1 fixes the existing ones).

**Rule F — Function shape.** A function body performs **at most two logical
actions**. A logical action is one of: validate inputs, locate/search,
transform data, mutate state, dispatch to callbacks, aggregate results. Guard
clauses (early `return` on a precondition) do not count. When a body needs a
third action, extract a named private helper whose name states that action —
the parent then reads as a table of contents. Target ≤ 30 body lines; a longer
body is allowed only when it is still ≤ 2 actions (e.g. one long switch that
only dispatches). If a task's split genuinely cannot work (hidden coupling),
write a `⛔ BLOCKED` note explaining why rather than forcing a bad split.

**Rule W — Why-comments.** Comments and Doxygen contracts answer *why the code
exists, which invariant it protects, or which trap it avoids* — never what the
next line does. Delete narration ("increment the counter", "write the header,
then copy the payload"). Add a why-comment wherever a reader would otherwise
ask "why is this safe?" (clever arithmetic, sentinels, reentrancy flags).

Worked example of all three rules (this is the standard to imitate):

```cpp
// BEFORE — one body, five actions, positional braces, narration comment:
FTickDecision FTickFunction::Advance(TimePointMilliseconds NowMilliseconds)
{
    // ... 41 lines mixing guards, reset branch, due-check and rescheduling,
    //     ending in: return {ERuntimeResult::Success, true, {NowMilliseconds, 0}};
}

// AFTER — guards + a dispatch that reads as a table of contents:
FTickDecision FTickFunction::Advance(TimePointMilliseconds NowMilliseconds)
{
    if (!Lifecycle.IsPlaying()) { return FTickDecision::Rejected(ERuntimeResult::InvalidLifecycle); }
    if (IsTimeBackward(NowMilliseconds)) { return FTickDecision::Rejected(ERuntimeResult::NonMonotonicTime); }
    LastObservedMilliseconds = NowMilliseconds;
    if (!Configuration.bEnabled) { return FTickDecision::NotDue(); }
    if (bMustResetSchedule) { return BeginResetSchedule(NowMilliseconds); }
    if (!IsTickDueNow(NowMilliseconds)) { return FTickDecision::NotDue(); }
    return ProduceDueTick(NowMilliseconds);
}
```

(Exact enum values and member names in the real task come from the real file —
the shape is what matters here.)

### 2.3 Reference files (already at the target bar — imitate them, cite them)

| Module | Exemplary files |
| --- | --- |
| Core | `Lifecycle.h` (the single best file in the repo), `TickFunction.h`, `Application.h` |
| Memory | `Containers/Span.h`, `Memory/MemoryResource.h` |
| Object | `ObjectHandle.h`, `Object.h` |
| Engine | `EngineStorage.h` (member-level ownership docs), `NetworkFrame.h`, the enums in `EngineResult.h` / `Timer.h` |
| Net | `ByteReader.h`, `ByteWriter.h` |
| Platform | `WinSockScope.h`, `HostTimeSource.h`, `Esp32TimeSource.h`, and the boundary documentation in `src/UdpSocketPlatformImplementation.h` |

### 2.4 Decisions record (settled — do not relitigate while executing)

- **D1** — The frame verb stays `Advance` everywhere (`FTickFunction`,
  `FApplication`, `UWorld`, `TTimerManager`, `FGarbageCollector`). A uniform
  verb across every tickable type is worth more than a per-class "more precise"
  verb. Rejected: `EvaluateTick`.
- **D2** — The UE5 word `Delta` stays (students must learn it); only the unit
  suffix is added (`CalculateDeltaMilliseconds`).
- **D3** — `Log.h` keeps its preprocessor mechanism (compile-time stripping
  cannot be replaced by `if constexpr` without evaluating arguments); tasks
  only rename its macros and improve its docs.
- **D4** — Result enums are **not** merged across modules; each layer keeps its
  vocabulary (`ERuntimeResult`, `EObjectResult`, `EEngineResult`,
  `ETimerResult`, `ENetResult`, `EDelegateResult`, …). Task 7.3 documents the
  boundary instead. Rejected: one global result enum (couples layers).
- **D5** — `ERuntimeResult` moves out of `Time.h` into a new
  `Core/RuntimeResult.h` so containers stop including a *time* header for an
  error enum; `Time.h` keeps including it, so no consumer breaks. No new
  `EContainerResult`.
- **D6** — Wire byte orders stay as-is (frame length big-endian, message-header
  length little-endian). Changing them buys no simplicity and breaks captured
  traffic. Task 6.2 names the helpers and documents why the layers differ.
- **D7** — One deliberate **behavior change** is approved: `EncodeFrame`
  returns `Invalid` (not `Full`) for an oversize payload, because an oversize
  payload can never succeed on retry (`NetResult.h` defines Full = retry
  later). Task 6.4.
- **D8** — The `TNetPacketStorage` / `TNetManager` split stays (caller-owned
  storage is the module's teaching pattern); task 6.4 documents the rationale.
  Folding storage into the manager is listed in section 6 (deferred ideas).
- **D9** — The private `...Glue.h` platform-boundary files are renamed to
  `...PlatformImplementation.h` (owner decision 2026-07-22, superseding the
  original `...PlatformSeam.h`: both "Glue" and "Seam" are metaphors, and
  "the docs already use the word seam" does not make it plain enough for a
  student — see the no-jargon rule that also retired `Lease`).
  Public header files are never renamed (frozen identity).
- **D10** — `TEngineHost`'s positional template arguments are tamed with named
  `constexpr` constants and `/*ParameterName*/` call-site annotations, not with
  a traits struct (deferred idea).
- **D11** — The non-owning registry-handle vocabulary unifies on **Reference**:
  `F...RegistryBase` → `F...RegistryReference`, `MakeView()` → `MakeReference()`,
  and the "lease" metaphor is purged from comments and test names too. The
  header file names stay (frozen identity). (Owner decision 2026-07-22
  superseding the original `Lease` choice, rejected as jargon — a name must be
  plain English a student reads without a glossary; no metaphors.)
- **D12** — `HasAssignedWorld()` / `HasAssignedActor()` keep their names; their
  docs must state plainly that they stay true after the parent expires.

---

## 3. Codebase map (navigation reference)

Line counts at baseline. Production code only (tests listed where a task
touches them).

**Core — `Modules/Core`** (~690 LOC production)

| File | LOC | Role |
| --- | --- | --- |
| `include/MicroWorld/Lifecycle.h` | 64 | forward-only lifecycle state machine (reference standard) |
| `include/MicroWorld/Time.h` | 50 | time aliases, `FTickContext`, `ERuntimeResult`, `FTickDecision` |
| `include/MicroWorld/TickFunction.h` + `src/TickFunction.cpp` | 81 + 128 | bounded per-object tick schedule |
| `include/MicroWorld/Tickable.h` | 54 | mix-in giving a type one primary tick |
| `include/MicroWorld/Application.h` + `src/Application.cpp` | 60 + 56 | abstract composition-root base |
| `include/MicroWorld/Log.h` + `src/Log.cpp` | 129 + 54 | compile-time-stripped logging facade |
| `include/MicroWorld/Version.h` | 24 | version constants |

**Memory — `Modules/Memory`** (~1630 LOC production)

| File | LOC | Role |
| --- | --- | --- |
| `Containers/Span.h` | 64 | non-owning view (reference standard) |
| `Containers/StaticVector.h` | 151 | fixed-capacity vector |
| `Delegates/Delegate.h` | 477 | inline single/multicast delegates |
| `Memory/MemoryResource.h` + `src/MemoryResource.cpp` | 60 + 8 | allocation interface (reference standard) |
| `Memory/FixedArena.h` | 217 | first-fit arena with bit markers |
| `Memory/UniquePtr.h` | 158 | resource-bound unique owner |
| `Memory/SharedPtr.h` | 506 | resource-bound shared/weak owners |

**Object — `Modules/Object`** (~2150 LOC production)

| File | LOC | Role |
| --- | --- | --- |
| `Object/ObjectHandle.h` | 103 | index+generation handle (reference standard) |
| `Object/ClassDescriptor.h` | 196 | no-RTTI type identity + registry |
| `Object/Object.h` | 83 | `UObject` managed base |
| `Object/ObjectPtr.h` | 232 | traced / weak / strong managed pointers |
| `Object/ObjectStore.h` + `src/ObjectStore.cpp` | 544 + 445 | fixed-slot object store |
| `Object/GarbageCollector.h` + `src/GarbageCollector.cpp` | 212 + 336 | budgeted incremental mark/sweep |

**Engine — `Modules/Engine`** (~2560 LOC production + example)

| File | LOC | Role |
| --- | --- | --- |
| `Engine/Actor.h` + `src/Actor.cpp` | 132 + 242 | `AActor` |
| `Engine/ActorComponent.h` + `src/ActorComponent.cpp` | 107 + 96 | `UActorComponent` |
| `Engine/World.h` + `src/World.cpp` | 127 + 464 | `UWorld` incl. deferred spawn/destroy barrier |
| `Engine/EngineStorage.h` | 140 | caller-owned registry storage (reference standard) |
| `Engine/EngineRegistryView.h` | 258 | move-only registry leases |
| `Engine/InlineTypes.h` | 90 | `TInlineActor` / `TInlineWorld` wrappers |
| `Engine/Timer.h` | 450 | `TTimerManager` |
| `Engine/EngineHost.h` | 361 | `TEngineHost` composition root, 7-step frame |
| `Engine/NetworkFrame.h` | 57 | net seam (reference standard) |
| `Engine/EngineResult.h`, `Engine/EngineClassIds.h` | 30 + 21 | result enum, stable class ids |
| `examples/HostLifecycle/Main.cpp` | 109 | first-contact example |

**Net — `Modules/Net`** (~2380 LOC production)

| File | LOC | Role |
| --- | --- | --- |
| `Net/ByteReader.h`, `Net/ByteWriter.h` | 159 + 158 | bounded LE serialization (reference standard) |
| `Net/NetResult.h`, `Net/NetAddress.h`, `Net/NetDriver.h` + `src/NetDriver.cpp` | 42 + 62 + 81 + 9 | result enum, opaque address, driver interface |
| `Net/NetPacketStorage.h`, `Net/NetManager.h` | 64 + 169 | caller-owned FIFO backing + manager |
| `Net/NetProtocol.h` | 238 | message header + control messages |
| `Net/FrameCodec.h` | 316 | CRC-16 frame codec + streaming decoder |
| `Net/NetHost.h` | 761 | `TNetHost` roles/peers/sessions |
| `Net/HostLoopback.h` | 316 | in-process test network |

**Platform — `Modules/PlatformHost`, `Modules/PlatformEsp32`** (~2340 LOC)

| File | LOC | Role |
| --- | --- | --- |
| `PlatformHost/.../HostTimeSource.h`, `WinSockScope.h`, `UdpAddress.h`, `HostUdpDriver.h` + `src/HostUdpDriver.cpp`, `src/UdpSocketPlatformImplementation.h` | 45+30+69+122+178+420 | host time + UDP driver + OS boundary |
| `PlatformHost/examples/TwoNodeDemo/Main.cpp` | 380 | two-node client/server demo (first-contact) |
| `PlatformEsp32/.../Esp32TimeSource.h`, `Esp32LogSink.h` + `src/Esp32LogSink.cpp`, `UdpAddress.h`, `Esp32UdpDriver.h` + `src/Esp32UdpDriver.cpp`, `src/Esp32SocketPlatformImplementation.h` | 28+22+29+69+119+141+362 | ESP32 adapters |
| `PlatformEsp32/.../Esp32E32LoraDriver.h` + `src/Esp32E32LoraDriver.cpp`, `LoraAddress.h`, `src/E32UartPlatformImplementation.h` | 128+153+58+180 | E32 LoRa UART driver |

---

## 4. Phases and tasks

### Phase 0 — Baseline ✅

- [x] **0.1 Re-record a green baseline.** Confirm the working tree is clean
  (`git -C . status`), then run the Standard Verify (section 1.1) plus
  `python tools/CheckFolderAgents.py --root Modules --exclude build --exclude .pio --exclude __pycache__`.
  Record the results as the evidence line. The folder-agents checker is
  **expected to fail** with 24 missing files until task 9.1 — record the
  failure, do not fix it now.

  **Done when:** build + ctest 11/11 + doc checker pass; results recorded here.

  Done 2026-07-22 — clean tree at `610132e`; `cmake --build build --config Release`
  warning-clean; `ctest` 11/11 passed; `CheckClassDocumentation.py --require-doxygen`
  passed (122 files); `CheckFolderAgents.py` fails with exactly 24 missing AGENTS.md
  (expected until task 9.1).

---

### Phase 1 — Mechanical renames ✅

Goal: every identifier states its full role. Use the rename procedure (1.3)
for every table row. One task per module; finish a module's whole table before
verifying. Where a "Sweep docs" note appears, the listed current docs mention
the old name and must be updated in the same task.

- [x] **1.1 Core renames.**

  | Current | New | Declared at (hint) |
  | --- | --- | --- |
  | `bScheduleReset` | `bMustResetSchedule` | `TickFunction.h:78`; uses in `TickFunction.cpp:21,41,48,69-71` |
  | `CalculateNextDue` | `CalculateNextDueMilliseconds` | `TickFunction.h:51`, `TickFunction.cpp:107` |
  | `CalculateDelta` | `CalculateDeltaMilliseconds` | `TickFunction.h:54`, `TickFunction.cpp:117` (D2: keep the word `Delta`) |
  | `FLifecycleGuard::State()` | `GetState()` | `Lifecycle.h:57`; update every call site (match the `GetInterval`/`GetName` getter convention) |
  | `MW_LOG_EMITF_0` / `MW_LOG_EMITF_1` | `MW_LOG_EMIT_FORMATTED_0` / `_1` | `Log.h:110-114` |
  | `MW_LOG_EMITM_0` / `MW_LOG_EMITM_1` | `MW_LOG_EMIT_MESSAGE_0` / `_1` | `Log.h:110-114` |
  | `GLogSink` | `InstalledLogSink` | `Log.cpp:13` (file-local) |

  **Done when:** every row's old name greps to zero outside historical files;
  Standard Verify passes.

  Done 2026-07-22 — all 7 renames applied across Core+Engine (incl. the
  `MW_LOG_CONCAT` token-paste prefixes and Lifecycle-only `GetState()`, leaving
  `TNetHost::GetState()` untouched); build warning-clean; ctest 11/11; doc
  checker 122 files; all six old-name grep gates return 0.

- [x] **1.2 Memory renames.** These are template-parameter renames — they
  change no call sites, only the declarations and every use *inside* the same
  file.

  | Current | New | Where |
  | --- | --- | --- |
  | template param `T` | `ValueType` | every template in `UniquePtr.h` and `SharedPtr.h` (e.g. `UniquePtr.h:13,16,37`, `SharedPtr.h:44,47,50`); `Delegate.h` already does this — copy its style |
  | template param `TArguments` | `ConstructorArgumentTypes` | `UniquePtr.h:28,134`, `SharedPtr.h:66,469` |
  | template params `TObject` / `TObjectArguments` | `FactoryValueType` / `FactoryConstructorArgumentTypes` | friend factory declarations, `SharedPtr.h:241`, `UniquePtr.h:116` |
  | `TFixedArena` param `Bytes` | `StorageCapacityBytes` | `FixedArena.h:19` (the accessor `CapacityBytes()` keeps its name — a template parameter may not share a member's name) |
  | `TFixedArena` param `Alignment` | `GuaranteedAlignmentBytes` | `FixedArena.h:19` |

  **Done when:** greps clean; Standard Verify passes (instantiations like
  `TFixedArena<1024>` are positional, so no caller changes).

  Done 2026-07-22 — template-param renames across `UniquePtr.h`/`SharedPtr.h`
  (`T`→`ValueType`, `TArguments`→`ConstructorArgumentTypes`,
  `TObject`/`TObjectArguments`→`FactoryValueType`/`FactoryConstructorArgumentTypes`)
  and `FixedArena.h` (`Bytes`→`StorageCapacityBytes`,
  `Alignment`→`GuaranteedAlignmentBytes`); `CapacityBytes()` accessor and the
  local `AlignmentBytes`/`Mode`/`PointerMode` correctly untouched; build
  warning-clean; ctest 11/11; doc checker 122 files; all file-scoped old-name
  grep gates return 0.

- [x] **1.3 Object renames.**

  | Current | New | Declared at (hint) |
  | --- | --- | --- |
  | pointer field `Object` (an `FObjectHandle`!) | `TargetHandle` | `ObjectPtr.h:103,138,218`; kills the `Object.Object` read at `ObjectStore.h:390,393`; the `Handle()` accessor keeps its name |
  | `ApplyPendingDestroy` param `MaxObjects` | `MaxSlotsToInspect` | `ObjectStore.h:365`, `ObjectStore.cpp:120` (it bounds slots inspected, not objects destroyed) |
  | `FScopedFlagReset` | `FScopedReentryGuard` | `GarbageCollector.cpp:14` |
  | `CanMultiply` | `MultiplicationFitsSizeType` | `ObjectStore.cpp:18` |
  | `SlowDescriptor` / `FastDescriptor` | `AncestryProbe` / `CycleDetectorProbe` | `ClassDescriptor.h:70-71` (Floyd cycle guard, not "speed") |
  | `FObjectStoreStorage::Slots` | `SlotMetadata` | `ObjectStore.h:101`; sweep every brace-initializer of `FObjectStoreStorage` (Engine `EngineHost.h`, Object/Engine tests) |
  | `FObjectStoreStorage::SlotBytes` | `SlotPayloadBytes` | `ObjectStore.h:107`; same sweep |
  | `FObjectStoreStorage::SlotStorageSizeBytes` | `TotalSlotStorageBytes` | `ObjectStore.h:103`; same sweep |
  | store member `Classes` | `ClassRegistryLookup` | `ObjectStore.h:511` |

  **Done when:** greps clean (pay attention to the cross-module
  `FObjectStoreStorage` users in `Modules/Engine`); Standard Verify passes.

  Done 2026-07-22 — nine renames confined to five Object-module files; all four
  distinct `Object` fields disambiguated (only the `FObjectHandle` smart-ptr
  field → `TargetHandle`; `Creation.Object`, `Slot.Object`, and the param kept)
  and `SlotSizeBytes` preserved while `SlotBytes`→`SlotPayloadBytes`. Positional
  `FObjectStoreStorage` initializers in Engine/tests needed no change. Build
  warning-clean; ctest 11/11; doc checker 122 files; all nine old-name grep
  gates 0; keep-checks confirm `SlotSizeBytes`/`Creation.Object`/`Slot.Object`
  intact.

- [x] **1.4 Engine renames.**

  | Current | New | Declared at (hint) |
  | --- | --- | --- |
  | `FActorComponentRegistryBase` | `FActorComponentRegistryReference` | `EngineRegistryView.h:25`; sweep `Actor.h/.cpp`, `InlineTypes.h`, tests (D11 — "Base" was the opposite of what it is; "Reference" per owner 2026-07-22) |
  | `FWorldActorRegistryBase` | `FWorldActorRegistryReference` | `EngineRegistryView.h:96`; sweep `World.h/.cpp`, `InlineTypes.h`, tests |
  | `MakeView()` (both registries) | `MakeReference()` | `EngineStorage.h:40,97` + every caller; header file names stay. Also purge the "lease" metaphor from comments/test names (D11 owner update) |
  | `TTimerManager::Advance` param `Now` | `NowMilliseconds` | `Timer.h:223` |
  | `TTimerManager::Schedule` param `Duration` | `DelayAndPeriodMilliseconds` | `Timer.h:150` (doc says it is both the first delay and the repeat period) |
  | `TEngineHost` member `GcBudget` | `GarbageCollectionBudget` | `EngineHost.h:315` |
  | `TEngineHost` template param `SlotBytes` | `SlotSizeBytes` | `EngineHost.h:45` (bytes **per slot**; the test fixture already uses this name) |
  | `TTimerManager` param `InlineCallbackBytes` and `TEngineHost` param `TimerCallbackBytes` | both `InlineTimerCallbackBytes` | `Timer.h:105`, `EngineHost.h:50` — one concept, one name |
  | method template param `T` | `TManagedType` | `EngineHost.h:114,139,185` (`CreateObject` etc.) |

  Also strengthen (do not rename, D12) the docs of `HasAssignedWorld()`
  (`Actor.h:68`) and `HasAssignedActor()` (`ActorComponent.h:59`): state that
  they remain true after the parent expires.

  **Done when:** greps clean; `docs/UE5ConceptMap.md` still reads correctly
  (it does not name the leases, but check); Standard Verify passes.

  Done 2026-07-22 — all 9 Engine renames applied across 19 files. The
  `...Base`→`...Reference` and `MakeView()`→`MakeReference()` renames use
  **Reference** (owner rejected the original `Lease` choice as jargon on
  2026-07-22, superseding D11's first wording) with the "lease" metaphor purged
  from every comment and both test-case names
  (`EngineReusedRegistryReferenceFailsBeginPlay`,
  `EngineInlineTypesComposeAndDispatchLikeReferenceComposedTypes`). The
  `Now`→`NowMilliseconds`, `Duration`→`DelayAndPeriodMilliseconds`,
  `GcBudget`→`GarbageCollectionBudget`, `SlotBytes`→`SlotSizeBytes`,
  `InlineCallbackBytes`+`TimerCallbackBytes`→`InlineTimerCallbackBytes`, and
  method-template `T`→`TManagedType` renames applied. D12 doc-strengthening of
  `HasAssignedWorld()`/`HasAssignedActor()` was already satisfied (both already
  state the identity survives parent expiry). Build warning-clean; ctest 11/11;
  doc checker 122 files; `rg "Lease"`=0, `rg -i "\blease"`=0,
  `MakeReference`/`RegistryReference`=164. Old `Lease`/`MakeView` vocabulary now
  survives only in frozen historical `docs/ROADMAP.md` (excepted by task 8.4).

- [x] **1.5 Net renames — fix the template-prefix style violations.** These
  four types are **class templates carrying the `F` prefix**, which teaches
  students the wrong rule (`docs/Style.md`: `T` = template).

  | Current | New | Declared at (hint) |
  | --- | --- | --- |
  | `FNetManager` | `TNetManager` | `NetManager.h:25`; fwd-decl + friend in `NetPacketStorage.h:12,52`; users in `NetHost.h`, tests |
  | `FNetPacketStorage` | `TNetPacketStorage` | `NetPacketStorage.h:26` |
  | `FHostLoopback` | `THostLoopback` | `HostLoopback.h:216`; used across Net/Engine/Platform tests |
  | `Detail::FLoopbackMailboxes` | `TLoopbackMailboxes` | `HostLoopback.h:25` |

  **Sweep docs:** `README.md` (mission mentions `FNetManager`), root
  `AGENTS.md` (same), `Modules/Net/README.md`, `docs/UE5ConceptMap.md`,
  `Modules/AGENTS.md` if it names them. Historical files keep the old names.

  **Done when:** greps show old names only in historical files; Standard
  Verify passes.

  Done 2026-07-22 — the four Net class templates renamed F→T across 10
  code/test files (`FNetManager`→`TNetManager`,
  `FNetPacketStorage`→`TNetPacketStorage`, `FHostLoopback`→`THostLoopback`,
  `Detail::FLoopbackMailboxes`→`TLoopbackMailboxes`), including the forward
  declaration, the template-friend declaration
  (`friend class TNetManager<...>`), the `static_assert` message strings, and
  every in-class self-reference; no other `F`-prefixed Net type touched. Living
  docs swept (`README.md`, root + Net `AGENTS.md`×4, `Modules/Net/README.md`,
  `docs/UE5ConceptMap.md`) plus the decorative `CheckProfileMap.py` self-test
  fixture (its Net detection keys on the archive lines, not the symbol name, so
  the change is safe — profile-map ctests #5/#6 stayed green). Build
  warning-clean; ctest 11/11; doc checker 122 files; old-name grep 0 outside
  historical files, `friend class FNetManager` 0. The `docs/diagrams/*` files
  keep the old names — intentionally excluded from every sweep (see task 8.4).

- [x] **1.6 Net renames — abbreviations and bare literals.**

  In `FrameCodec.h`, apply four mechanical substring rules to identifiers
  (types, members, enum values, locals — read each hit, keep the rest of the
  identifier): `Src` → `Source`, `Len` → `Length`, `Hi` → `HighByte`,
  `Lo` → `LowByte` (`Crc` itself stays). Examples: `SrcNodeId` →
  `SourceNodeId`, `PendingLenHi` → `PendingLengthHighByte`,
  `ReadingCrcLo` → `ReadingCrcLowByte`, `DeclaredLen` →
  `DeclaredLength`.

  | Current | New | Declared at (hint) |
  | --- | --- | --- |
  | member `QueuedCount` + accessor `QueuedCountValue()` | member `QueuedPacketCount`, accessor `QueuedCount()` | `NetManager.h:126` |
  | `FByteWriter::Written()` | delete it; call sites use `Position()` | `ByteWriter.h:121` (`rg "\.Written\(\)" Modules` first — replace those call sites) |
  | local `Beat` | `HeartbeatMessage` | `NetHost.h:278` |
  | local `Farewell` | `ByeMessage` | `NetHost.h:203` |
  | local `Needed` | `RequiredFrameBytes` | `FrameCodec.h:104` |
  | locals `Data` / `Count` | `ChecksumBytes` / `ByteCount` | `FrameCodec.h:58-59` |
  | local `Total` | `ActiveCount` | `NetHost.h:390` |
  | literal generation `1` for the local peer | `constexpr std::uint8_t LocalPeerGeneration = 1;` + why-comment (never evicted, so never bumped) | `NetHost.h:372` |
  | literal slot `0` for "the server, seen from a client" | `constexpr std::size_t ServerPeerSlotIndex = 0;` + why-comment (only meaningful in client mode) | `NetHost.h:381,493,626` |

  **Done when:** greps clean; Standard Verify passes.

  Done 2026-07-22 — six files. `FrameCodec.h`: 13 abbreviated identifiers
  expanded (`Src`→`Source`, `Len`→`Length`, `Hi`→`HighByte`, `Lo`→`LowByte`),
  plus locals `Data`/`Count`→`ChecksumBytes`/`ByteCount` in `ComputeCrc16Ccitt`
  and `Needed`→`RequiredFrameBytes` in `EncodeFrame`; comment prose and the
  `.Data()`/`Payload*` names left intact. `NetManager.h`: member
  `QueuedCount`→`QueuedPacketCount`, accessor `QueuedCountValue()`→`QueuedCount()`
  (+7 test call sites); the unrelated `HostLoopback` `QueuedCount` correctly
  untouched. `ByteWriter.h`: `Written()` deleted (it was an alias of
  `Position()`), its one call site moved to `Position()`, `WrittenBytes()` kept.
  `NetHost.h`: locals `Farewell`/`Beat`/`Total`→`ByeMessage`/`HeartbeatMessage`/
  `ActiveCount`; the bare literals extracted to `static constexpr std::uint8_t
  LocalPeerGeneration = 1` and `ServerPeerSlotIndex = 0` with why-comments —
  declared `std::uint8_t`, not the roadmap's `std::size_t`, because `FPeerId`'s
  fields are `std::uint8_t` and `size_t` would be a narrowing error in the
  `FPeerId{…}` brace-init (lead correction; matches the sibling `LocalPeerIndex`).
  Build warning-clean; ctest 11/11; doc checker 122 files; every old-identifier
  grep gate 0. Known nit: `ByteWriterTests` now asserts `Position()==0` twice (a
  harmless duplicate the `Written()` removal left) — pending a one-line cleanup.

- [x] **1.7 Platform renames.**

  | Current | New | Where |
  | --- | --- | --- |
  | param/local `Data` | `DatagramBytes` (UDP) / `FrameBytes` (UART) | `UdpSocketGlue.h:189`, `Esp32SocketGlue.h:161`, `E32UartGlue.h:65` |
  | local `Packed` | `PackedIpv4Address` | `HostUdpDriver.cpp:143`, `UdpSocketGlue.h:158`, `Esp32UdpDriver.cpp:106`, `Esp32SocketGlue.h:130` |
  | local `Byte` | `IncomingByte` | `Esp32E32LoraDriver.cpp:110` |
  | file `src/UdpSocketGlue.h` | `src/UdpSocketPlatformImplementation.h` | private header (D9); update `#include`s and any CMake/`library.json` listing |
  | file `src/Esp32SocketGlue.h` | `src/Esp32SocketPlatformImplementation.h` | same |
  | file `src/E32UartGlue.h` | `src/E32UartPlatformImplementation.h` | same |

  (The example `Main.cpp` renames happen in Phase 8 together with their
  restructuring — do not touch the examples here.)

  **Done when:** greps clean; `rg -n "Glue" Modules` returns nothing outside
  historical files; Standard Verify passes. Note: PlatformEsp32 has no host
  build — for its files the "verify" is that PlatformHost still builds green
  and a careful re-read of each ESP32 edit (compile happens on PlatformIO, out
  of scope here).

  Done 2026-07-22 — three private platform headers renamed via `git mv`
  (`UdpSocketGlue.h`/`Esp32SocketGlue.h`/`E32UartGlue.h` →
  `...PlatformImplementation.h`), their three `#include`s repointed, and the
  `#pragma once` banners updated; no CMake/`library.json` listed them.
  Identifier renames: `Data`→`DatagramBytes` (UDP send) / `FrameBytes` (UART
  send), `Packed`→`PackedIpv4Address` (four sites), `Byte`→`IncomingByte` (LoRa
  receive-pump local; `PumpByteCap`/`ReadUartByte` left intact). The prose word
  "glue" purged everywhere in Modules — including two hits the case-insensitive
  gate caught beyond the three headers (`PlatformEsp32Main.cpp` and the public
  `Esp32E32LoraDriver.h` comments); the general "seam" prose was left as-is
  (out of scope, tracked separately). PlatformHost built green + ctest 11/11 +
  doc checker 122; PlatformEsp32 has no host build, so each ESP32 edit was
  verified by line-by-line re-read (pure renames, no logic or signature change).
  Grep gates: `rg -i "glue" Modules` (excluding frozen benchmark results) 0,
  `rg "\bPacked\b" Modules` 0.

---

### Phase 2 — Why-comment repairs ✅

Goal: every comment answers *why*; every trap has its why written next to it.
Pure comment/doc edits — zero code changes. Keep each new comment to 1-2
sentences.

- [x] **2.1 Core comments.** In `Log.h`: convert the 13-line `//` facade note
  (`Log.h:5-17`) into the standard `/** ... */` contract; delete the
  "(Phase 3.1, owner delegated ...)" provenance; reduce `Log.h:109` to the one
  load-bearing invariant (arguments of a below-floor call are never
  evaluated). Add one worked macro-expansion example (3-4 lines, in the
  header's contract) showing what `MW_LOG(Info, "x=%d", X)` becomes when the
  level is enabled vs stripped.

  **Done when:** Standard Verify passes (the doc checker still accepts the
  file).

  Done 2026-07-22 — facade banner converted to a `/** ... */` contract;
  provenance line deleted; the emitter-macro one-liner reduced to the single
  invariant ("a below-floor call drops its arguments UNEVALUATED"); worked
  example added. Corrected the task's illustrative call: `MW_LOG(Info, ...)`
  named a non-existent level (levels are Error/Warning/Log/Verbose) and omitted
  the required Category, so the committed example is `MW_LOG(Log, "Boot",
  "x=%d", X)` with its exact enabled/stripped expansions (checked against
  `MW_LOG_EMIT_FORMATTED_1`/`_0`). Build clean; ctest 11/11; doc checker 122
  files; `Phase 3.1|owner delegated` and `MW_LOG(Info` grep gates both 0.

- [x] **2.2 Memory comments.** Add the missing whys:
  - `SharedPtr.h:480` — the `(sizeof(FControlBlock) + alignof(T) - 1) & ~(alignof(T) - 1)`
    expression: "rounds the control-block size up so the value starts on its
    own alignment, immediately after the control block."
  - `FixedArena.h:70-82` — lead comment for the scan loop: "walk per-byte
    occupancy, tracking the first aligned run of SizeBytes free bytes
    (first-fit)."
  - `FixedArena.h:184-201` — `ReadMarker`/`WriteMarker`: "hand-rolled bitset:
    byte = Offset/8, bit = Offset%8; a marker bit means 'an allocation
    boundary starts here'."
  - `StaticVector.h:145`, `Delegate.h:465,468` — the `MaxElements == 0 ? 1 :
    MaxElements` arrays: "C++ forbids zero-length arrays; one dummy slot keeps
    a zero-capacity instantiation well-formed."
  - `SharedPtr.h` control block — add a short lifecycle note on
    `TSharedControlBlock` listing the teardown orderings the
    `bValueDestructionInProgress` flag protects against (see the self-observer
    regression test at `MemoryTests.cpp:558`).

  **Done when:** each listed site has its why; Standard Verify passes.

  Done 2026-07-22 — five why-comments added across four files: SharedPtr
  `ValueOffset` layout; FixedArena first-fit scan loop + `ReadMarker` bit math;
  StaticVector and Delegate zero-length-array guards; and the
  `TSharedControlBlock::bValueDestructionInProgress` lifecycle note (cites the
  self-observer regression `MemoryTests.cpp:558`). Re-anchored the roadmap's
  stale hints to live code — `alignof(ValueType)` not `T` (task 1.2), Delegate
  uses `MaxBindings` not `MaxElements`, and the loop/marker whys corrected to the
  real span-boundary and generic two-array mechanism. The fifth site was added
  via a follow-up brief after my first brief under-scoped to four sites. Build
  clean; ctest 11/11; doc checker 122 files.

- [x] **2.3 Object comments.**
  - `ClassDescriptor.h:70-80` — why the two probes exist: "a corrupted Parent
    chain could cycle; the double-step probe detects that without RTTI."
  - `ClassDescriptor.h:179` — why `VisitedDescriptors >= RegisteredClassCount`
    bounds the walk: no valid chain is longer than the registry.
  - `ObjectStore.h:318` — the `Slot.Generation == 0 ? 1 : Slot.Generation + 1`
    ternary: point at the "generation 0 = never published" invariant documented
    in `ObjectHandle.h:61-62`.
  - `ObjectStore.cpp:281,315` — why `bWasMutationLocked` is saved and
    *restored* rather than cleared (destruction can nest inside construction or
    another destruction).

  **Done when:** each listed site has its why; Standard Verify passes.

  Done 2026-07-22 — four why-comments added: the `IsChildOf` Floyd cycle guard
  and the `HasValidParentChain` registry-length bound (`ClassDescriptor.h`); the
  generation-0 "never published" ternary citing `ObjectHandle.h:61-62`
  (`ObjectStore.h`); and the save/restore-not-clear rationale for
  `bMutationLocked` in the nested-destroy path (`ObjectStore.cpp`, one comment at
  the save per DRY). Anchors re-derived from live code — the `.cpp` save/restore
  moved to lines 283/317. Build clean; ctest 11/11; doc checker 122 files.

- [x] **2.4 Engine comments.**
  - `ActorComponent.h:85` — **factual bug**: says "Advances this component's
    components and primary tick" — a component owns no components (copy-paste
    from `Actor.h:104`). Fix to: "Advances this component's primary tick for
    one dispatcher step."
  - `EngineHost.h:245-268` — delete the numbered narration comments
    (`// 2. Fire due timer callbacks.` etc.) that restate both the code and the
    class contract's 7-step list; keep only the ones carrying extra rationale
    (the step-5 GC-skip note at `EngineHost.h:256-257` stays).
  - `Timer.h:432-438` — the `MaxTimers == 0 ? 1 : MaxTimers` arrays: same
    zero-length-array why as task 2.2.
  - `Actor.h:32-35`, `ActorComponent.h:29-32`, `World.h:32-35`,
    `EngineHost.h:91-94` — the deleted copy/move members carry no Doxygen
    comment while `Timer.h:124-140` documents each; add one shared-style
    comment per block ("copying would duplicate identity/registration; managed
    objects live and die in their store slot").

  **Done when:** each listed site fixed; Standard Verify passes.

  Done 2026-07-22 — fixed the `ActorComponent::DispatchAdvance` doc (a component
  owns no components); deleted the six redundant numbered step-comments in
  `EngineHost::Tick()` (the 7-step order already lives in the `Tick()` contract)
  and kept only the step-5 GC-skip why; added the zero-length-array why once on
  Timer's three guarded arrays; documented the copy/move blocks in
  Actor/ActorComponent/World with a shared managed-object why, and `EngineHost`
  with a distinct subsystem-ownership why (it is not a store-slot object). Build
  clean; ctest 11/11; doc checker 122 files; numbered-narration grep gate 0.

- [x] **2.5 Net + Platform comments.**
  - `FrameCodec.h:109` — delete the "Write magic, source node id, ..."
    narration (the why — CRC excludes the magic — is already at line 118).
  - `FrameCodec.h:137-143` — split the 40-word decoder-contract sentence into
    two sentences (truncation behavior; rewind caveat).
  - `NetHost.h:430` — `MaxDuration = 0xFFFFFFFFu`: add "clamp so a gap longer
    than ~49 days still fits DurationMilliseconds (u32)."
  - `NetHost.h:486` — generation bump: state the accepted wrap window (a stale
    id from exactly 256 evictions ago would re-match).
  - `PlatformHost/.../UdpAddress.h:11` — says "loopback" but encodes **any**
    IPv4 endpoint; align the wording with the ESP32 twin.
  - `HostUdpDriver.cpp:135` and `Esp32UdpDriver.cpp:98` — drop the "perform
    one consuming read..." narration; state the why (the fits-check already
    happened; this read must consume exactly the probed datagram).
  - `Esp32E32LoraDriver.cpp:123` — comment says "held frame" but this branch
    delivers the **just-decoded** frame (the held path is line 91); fix the
    text and state the transactional-Full invariant.
  - `Esp32E32LoraDriver.cpp:58` — keep only the "codec is transactional on
    failure" why; delete the "Encode the full frame into a stack buffer"
    narration.
  - `UdpSocketPlatformImplementation.h:416` / `Esp32SocketPlatformImplementation.h:358` (renamed in 1.7) — the
    `select(Socket + 1, ...)` ritual: "POSIX nfds is highest-descriptor+1."
  - `UdpSocketPlatformImplementation.h` (was `UdpSocketGlue.h:280`) — name the oversize
    sentinel: introduce
    `constexpr std::size_t OversizeDatagramSentinelBytes = PeekScratchBytes + 1;`
    with a why-comment, and return it instead of the bare `PeekScratchBytes + 1`.

  **Done when:** each listed site fixed; Standard Verify passes.

  Done 2026-07-22 — all ten sites landed. Deleted the FrameCodec encode
  narration; **tightened** the decoder-contract run-on to one clearer sentence
  rather than splitting it — a literal two-sentence split would breach the
  enforced 3-sentence contract cap (`CheckClassDocumentation.py` counts the
  `@tparam` period, so sentence 1 + `@tparam` already fill two of three slots;
  owner deferred the call, chose the tighten). Added the `MaxDuration` u32-clamp
  why and the u8 generation-wrap window (NetHost); fixed the "IPv4 loopback" →
  "IPv4 UDP" mislabel (host `UdpAddress.h`); replaced the consuming-read
  narration with its why in both UDP drivers; corrected the LoRa "held frame" →
  "just-decoded frame" comment with the transactional-`Full` invariant and
  trimmed the encode narration; documented both `select()` nfds sites; and named
  the oversize sentinel `OversizeDatagramSentinelBytes` — **the one
  behavior-preserving code change** in this comment phase. A stray `@tparam`
  period regression was caught and fixed mid-review. Build clean; ctest 11/11;
  doc checker 122; all five grep gates 0, sentinel referenced 3×.

---

### Phase 3 — Function decomposition: Core & Memory ✅

Goal: no production function does more than two logical actions (Rule F). For
every extraction: new helpers are `private` members (or file-local `static`
functions in a `.cpp` / anonymous helpers in a header's `Detail` namespace),
each with a why-focused Doxygen comment, same `noexcept`/`const` discipline as
the parent, and the parent becomes guards + named calls. Behavior-preserving —
tests must pass unchanged.

- [x] **3.1 Split `FTickFunction::Advance` and add named decision factories.**
  `TickFunction.cpp:52-95` (~41 body lines, 5 actions).
  1. Add to `FTickDecision` (in `Time.h`) three documented static factories:
     `Rejected(ERuntimeResult)`, `NotDue()`, `Ticked(TimePointMilliseconds NowMilliseconds, DurationMilliseconds DeltaMilliseconds)`
     — additive API, no field changes.
  2. Extract from `Advance`: `FTickDecision BeginResetSchedule(TimePointMilliseconds)`
     (the first-tick reset branch, ~lines 69-75),
     `bool IsTickDueNow(TimePointMilliseconds) const` (~lines 77-85),
     `FTickDecision ProduceDueTick(TimePointMilliseconds)` (~lines 87-94).
  3. Rewrite `Advance` as the guards + dispatch shown in section 2.2's worked
     example; replace every positional `return {…}` in the file with a factory
     call.
  4. Add static factory `FTickConfiguration::EnabledEvery(DurationMilliseconds IntervalMilliseconds)`
     and replace the positional `FTickConfiguration Configuration{true, true, 25}`
     initializers in `TickFunctionTests.cpp` (~10 sites) — the two adjacent
     bools are unreadable at call sites.

  **Done when:** `Advance` body ≤ 15 lines, no positional `FTickDecision`
  braces remain in `Modules/Core`; Standard Verify passes.

  Done 2026-07-22 — implemented by a **Sonnet subagent** (first task of the
  direct-dispatch workflow), then reviewed and re-verified by the lead. Added
  `FTickDecision::Rejected/NotDue/Ticked` (Time.h) and
  `FTickConfiguration::EnabledEvery` (TickFunction.h); extracted
  `BeginResetSchedule` / `IsTickDueNow` (const) / `ProduceDueTick` and rewrote
  `Advance` as guards + named dispatch. 12 of 15 test initializers converted to
  `EnabledEvery`; the 3 atypical (start-disabled / cannot-ever-tick) configs left
  positional (YAGNI). Behavior-preserving — every original branch reproduced;
  build clean, ctest 11/11 unchanged, doc checker 122, positional-brace grep 0.
  Line-count note: `Advance` is 12 logical lines but ~22 physical — house style
  braces every `if` (clang-format `AllowShortIfStatementsOnASingleLine: Never`;
  zero brace-less guards exist in Modules), so a literal ≤15 physical would be
  style-inconsistent; the decomposition intent is fully met. CQS:
  `BeginResetSchedule`/`ProduceDueTick` intentionally mutate-and-return, mirroring
  the original branches verbatim.

- [x] **3.2 Split `TFixedArena::TryAllocate` and `Deallocate`.**
  `FixedArena.h:46-103` (~55 lines, 4 actions) and `:106-153` (~45 lines, 5+
  validations + mutation).
  1. Add `constexpr std::size_t AlignSizeUp(std::size_t SizeBytes, std::size_t AlignmentBytes)`
     to `MemoryResource.h` with a why-comment (shared rounding used by arena
     and, later, `MakeShared` — task 3.3).
  2. `TryAllocate` → `ValidateAllocationRequest(...)`,
     `FindAlignedFreeRange(SizeBytes, AlignmentBytes, OutStartOffset)` (the
     `bInsideAllocation`/`FreeRangeSize` scan), `CommitAllocation(StartOffset, SizeBytes, OutBlock)`.
  3. `Deallocate` → `LocateOwnedAllocation(Block, OutStart, OutEnd)`,
     `ValidateExactBlockBoundaries(Start, End)`, `ReleaseMarkedRange(Start, End, SizeBytes)`.
  4. Parent bodies become guards + the named steps.

  **Done when:** both parents ≤ 15 body lines; helpers documented; Standard
  Verify passes (MemoryTests exercise every failure path).

  Done 2026-07-22 — implemented by a **Sonnet subagent**, then reviewed and
  re-verified by the lead. `TryAllocate` → `ValidateAllocationRequest` /
  `FindAlignedFreeRange` (the aligned first-fit scan, alignment gated at
  `(Offset & (AlignmentBytes - 1U)) == 0`) / `CommitAllocation`; `Deallocate` →
  `LocateOwnedAllocation` / `ValidateExactBlockBoundaries` / `ReleaseMarkedRange`,
  with the `UsedSizeBytes < Block.SizeBytes` guard kept in the parent to preserve
  the original check order. Added `constexpr AlignSizeUp` to `MemoryResource.h`
  for task 3.3 (currently unreferenced — a deliberate one-task-ahead seam, not
  wired into the arena; `StorageBegin`'s own pointer-alignment left untouched).
  Behavior-preserving — every branch and marker read/write order reproduced;
  build clean, ctest 11/11 (MemoryTests unchanged), doc checker 122. Line-count
  note: parents are 7 and 9 logical lines (≤15); Deallocate is 18 physical only
  because house style braces every guard, same accepted reading as task 3.1. CQS:
  validators/finders are pure queries, `CommitAllocation`/`ReleaseMarkedRange`
  are commands — the split the task specified.

- [x] **3.3 Split `MakeShared` and `TSharedPtr::Reset`.**
  `SharedPtr.h:469-504` (~32 lines) and `:190-216` (~24 lines, 3
  responsibilities).
  1. Extract the layout arithmetic into a documented
     `template <typename ValueType> struct TSharedAllocationLayout`
     (fields `ValueOffsetBytes`, `CombinedSizeBytes`, `CombinedAlignmentBytes`,
     all `constexpr`, computed via `AlignSizeUp` from task 3.2) — the three
     overflow `static_assert`s move with it.
  2. `MakeShared` becomes: compute layout → allocate → construct control block
     and value (`ConstructSharedBlock(...)`) → wire the pointer.
  3. `Reset` → extract `DestroyValueInPlace(ControlBlock)` (the
     `bValueDestructionInProgress`-guarded destruction) and
     `ReclaimControlBlockIfUnreferenced(ControlBlock)`.

  **Done when:** parents read as named steps; the self-observer regression
  test (`MemoryTests.cpp:558`) still passes; Standard Verify passes.

  Done 2026-07-22 — implemented by a **Sonnet subagent**, then reviewed and
  re-verified by the lead. Extracted `Detail::TSharedAllocationLayout` (the
  alignment/offset/size arithmetic + the two overflow `static_asserts`, offset now
  via `AlignSizeUp` from task 3.2 — byte-identical to the old inline `& ~(align-1)`
  form) and `Detail::ConstructSharedBlock`; `MakeShared` is now compute-layout →
  allocate → construct → wire, its four contract asserts unchanged. `Reset` →
  `DestroyValueInPlace` / `ReclaimControlBlockIfUnreferenced` (private statics),
  same guard order. Behavior-preserving; build clean, ctest 11/11 with the
  self-observer regression (`MemoryTests.cpp:558`) green, doc checker 122.
  Tooling note: plain `clang-format -i` does NOT discover the repo's dot-less
  `clang-format` config and silently falls back to LLVM style; the correct
  invocation is `-style=file:"<repo>/clang-format"`. The `microworld_format_check`
  ctest (#1) is the authoritative gate and passed, so all Phase 3 commits are
  house-formatted regardless.

- [x] **3.4 Group the delegate machinery.** `Delegate.h`.
  1. Group the three erased function pointers (`FInvokeFunction`,
     `FMoveFunction`, `FDestroyFunction`, `Delegate.h:178-252`) into one
     documented `struct FErasedCallableOperations { Invoke; MoveConstruct; Destroy; };`
     member, with a header note: "why not std::function — it heap-allocates."
  2. Extract `template <typename StoredCallable> void InstallErasedOperations()`
     from `TDelegate::Bind` (`Delegate.h:113-144`) so `Bind` = contract asserts
     + size/alignment guards + install.
  3. `TMulticastDelegate::Add` (`:298-328`) → extract
     `OccupySlot(SlotIndex, Binding)` (returns the new handle) and
     `RecordBroadcastOrder(Handle)`.
  4. `TMulticastDelegate::Remove` (`:331-360`) → extract
     `ValidateLiveHandle(Handle, OutOrderIndex)` and
     `RetireSlotAndCompactOrder(SlotIndex, OrderIndex)`.
  5. `TMulticastDelegate::Broadcast` (`:368-391`) — replace the manually
     cleared `bBroadcastActive` on three exit paths with a tiny RAII
     `FScopedBroadcastGuard` (constructor sets, destructor clears) so the
     invariant lives in one place.

  **Done when:** each parent ≤ ~15 body lines / ≤ 2 actions; DelegateTests
  pass unchanged; Standard Verify passes.

  Done 2026-07-22 — implemented by a **Sonnet subagent**, then reviewed and
  re-verified by the lead. Grouped the three erased pointers into
  `FErasedCallableOperations` (with the "not std::function — it heap-allocates"
  note); this also collapsed the triple assignments in `MoveFrom` (`Operations =
  Other.Operations`) and `ClearFunctions` (`Operations = {}`). Extracted
  `InstallErasedOperations` from `Bind`; `OccupySlot` + `RecordBroadcastOrder`
  from `Add`; `ValidateLiveHandle` (const) + `RetireSlotAndCompactOrder` from
  `Remove`; and replaced the two manual `bBroadcastActive` clears with a RAII
  `FScopedBroadcastGuard` (the reentrancy guard still fires before the scoped
  guard constructs). Behavior-preserving — every branch, result code, and slot
  mutation order reproduced; build clean, ctest 11/11 (DelegateTests in
  `microworld_memory_tests` #7), doc checker 122. CQS: `OccupySlot`'s
  mutate-and-return is the intentional slot-factory shape; `ValidateLiveHandle`
  is a clean const query with an out-param. **Phase 3 complete.**

---

### Phase 4 — Function decomposition: Object & GC ✅

- [x] **4.1 Split `FGarbageCollector::Advance` (the largest function in the
  repo).** `GarbageCollector.cpp:108-247` (~137 body lines, 6 actions,
  duplicated mid-loop abort checks).
  1. Add a private member `bool bWorklistOverflowed{false};` documented as:
     "set by DiscoverReference when the fixed worklist cannot take one more
     entry; Advance reads it at each phase head instead of re-deriving the
     abort from CurrentPhase". Set it inside `DiscoverReference` where
     `ResetCycle()` fires today; clear it in `ResetCycle`.
  2. Extract per-phase helpers, each ≤ ~25 lines:
     `ERuntimeResult ValidateAdvancePreconditions() const`,
     `AdvanceSeedRootsPhase(const FGarbageCollectionBudget&, FGarbageCollectionResult&)`,
     `AdvanceMarkPhase(...)`, `AdvanceSweepPhase(...)`,
     `FinalizeCompletedCycle()`, `AccumulateOperations(FGarbageCollectionResult&)`.
  3. Replace both `if (CurrentPhase == Idle) { … CapacityExceeded }` mid-loop
     checks (`:151-157`, `:187-194`) with one overflow-flag check per phase
     head, each carrying the why-comment: "DiscoverReference aborts the cycle
     on worklist overflow — detect that abort here."
  4. `Advance` becomes: validate → phase dispatch loop over the named helpers
     → aggregate. Target ≤ 30 body lines.

  **Done when:** `Advance` ≤ 30 body lines; each helper documented; all
  GarbageCollectorTests pass unchanged; Standard Verify passes.

  Done 2026-07-22 — implemented by a **Sonnet subagent**, then reviewed and
  re-verified by the lead. `Advance` is now validate → per-phase dispatch loop →
  aggregate (~26 logical lines): `ValidateAdvancePreconditions` (const) +
  `AdvanceSeedRootsPhase`/`AdvanceMarkPhase`/`AdvanceSweepPhase` (each returns a
  bool "phase advanced" signal) + `FinalizeCompletedCycle` + `AccumulateOperations`
  (static). The overloaded `CurrentPhase == Idle` mid-loop abort sentinel is
  replaced by an explicit `bWorklistOverflowed` flag: `DiscoverReference` sets it
  after its existing `ResetCycle()`, `ResetCycle` clears it, and the phase loop
  reads it once after dispatch. **Roadmap correction (lead, §5.1):** the plan's
  flag scheme leaked a stale `true` into the next cycle (the flag is set after
  `ResetCycle` clears it, so it survives the aborted cycle); fixed by clearing
  `bWorklistOverflowed` once at `Advance` entry after preconditions pass — so only
  the current call's discovery can trip it. A single post-dispatch check replaces
  the roadmap's "one per phase head" (same effect, simpler). Behavior-preserving:
  every branch, counter, result code, and `ResetCycle` timing reproduced; build
  clean, ctest 11/11 (GarbageCollectorTests in `microworld_object_tests` #8), doc
  checker 122. Line-count: 26 logical (≤30); physical higher due to mandatory
  braces, same accepted reading as Phase 3. CQS: the `Advance*Phase` helpers
  mutate cursor/phase state and return a progress bool, matching the existing
  `ResetCycle`/`CompleteCycle` non-strict-CQS precedent (not introduced here).

- [x] **4.2 Split `FObjectStore::DestroySlot` and `NewObject`; consolidate the
  generation rule.** `ObjectStore.cpp:263-317` (~52 lines) and
  `ObjectStore.h:292-344` (~50 lines).
  1. Add one documented helper owning the "retire before wrap" rule that today
     lives in three places (`ObjectHandle.h:98` `CanAdvanceObjectGeneration`,
     the `?1:+1` ternary at `ObjectStore.h:318`, the Vacant/Retired branch at
     `ObjectStore.cpp:293-301`): e.g.
     `static std::uint8_t NextGenerationOrRetire(FSlot& Slot)` (or split
     compute/apply to respect CQS) — the other sites call it.
  2. `DestroySlot` → `ValidateDestroyableSlot(ObjectIndex)`,
     `RunDestructionCallbacks(Slot)` (BeginDestroy + Destroy + RemoveAllRoots),
     `RecycleOrRetireSlot(Slot)`, `UpdateOccupancyCounters()`.
  3. `NewObject` → extract `PublishObjectIntoSlot(SlotIndex, Descriptor, Object)`
     (wire identity + slot metadata + counters); the generation computation
     comes from step 1.

  **Done when:** both parents read as guards + named steps; ObjectStoreTests
  pass unchanged; Standard Verify passes.

  Done 2026-07-22 — implemented by a **Sonnet subagent**, then reviewed and
  re-verified by the lead. `DestroySlot` → `ValidateDestroyableSlot` (const) +
  `RunDestructionCallbacks` (BeginDestroy/Destroy/RemoveAllRoots) +
  `RecycleOrRetireSlot` (clear pointers, then vacate-or-retire via the existing
  `CanAdvanceObjectGeneration`) + `UpdateOccupancyCounters`; `NewObject` keeps its
  guards + `State=Constructing`/lock before placement-new, then delegates
  post-construction wiring to `PublishObjectIntoSlot`. Generation rule: took the
  roadmap's sanctioned compute/apply CQS split — a pure `NextPublishGeneration`
  (the `0 ? 1 : +1` publish rule) plus `RecycleOrRetireSlot`'s destroy-side
  decision, both anchored on the unchanged `CanAdvanceObjectGeneration` predicate
  (not merged into one function). Behavior-preserving — every branch, counter,
  callback order, and the mutation-lock save/restore reproduced; build clean,
  ctest 11/11 (ObjectStoreTests in `microworld_object_tests` #8), doc checker 122.
  CQS: `PublishObjectIntoSlot` mutates-and-returns the handle — the intentional
  slot-factory shape, same as tasks 3.1/3.3/3.4.

- [x] **4.3 Smaller Object splits.**
  1. `FGarbageCollector::RequestCollection` (`GarbageCollector.cpp:73-106`,
     ~32 lines, 4 reject branches each duplicating `RejectedRequests++`) —
     extract an enum-returning `ClassifyStartFailure()` (or equivalent) so
     there is exactly one reject path.
  2. `FObjectStore` constructor (`ObjectStore.cpp:41-67`) — extract
     `static bool IsStorageDescriptorValid(const FObjectStoreStorage&)` so the
     body is validate-then-initialize.
  3. `FClassDescriptor::IsChildOf` (`ClassDescriptor.h:68-92`) — split into
     `HasAcyclicAncestry()` (the probe loop) + `AncestryContains(Candidate)`
     (the walk).
  4. `FObjectStore::ApplyPendingDestroy` (`ObjectStore.cpp:120-151`) — extract
     `AdvancePendingScanCursor()` if it clarifies; skip with a note if the
     result reads worse (borderline case).

  **Done when:** each touched parent ≤ 2 actions; tests pass unchanged;
  Standard Verify passes.

  Done 2026-07-22 — implemented by a **Sonnet subagent**, then reviewed and
  re-verified by the lead. Four extractions across five files:
  `RequestCollection` → `ClassifyStartFailure` (the four reject branches become
  one enum-returning classifier so there is a single reject path that increments
  `RejectedRequests` once); `FObjectStore` constructor → `IsStorageDescriptorValid`
  (static; body is validate-then-initialize); `FClassDescriptor::IsChildOf` →
  `HasAcyclicAncestry` (Floyd probe) + `AncestryContains` (the walk), kept public
  so the aggregate stays all-public; `ApplyPendingDestroy` → `AdvancePendingScanCursor`
  (the wrapping ring-cursor read/advance). Behavior-preserving — every branch,
  counter, and side-effect order reproduced; build clean, ctest 11/11 (Object tests
  in `microworld_object_tests` #8), doc checker 122. CQS: `ClassifyStartFailure`
  embeds `CollectorTryBegin` (single reject path — the roadmap's goal) and
  `AdvancePendingScanCursor` is a post-increment cursor — both intentional
  command+query idioms. **Phase 4 complete.**

---

### Phase 5 — Function decomposition: Engine 🟨

- [x] **5.1 Collapse the four validate-then-commit gauntlets.** Four functions
  are near-identical 8-step validation ladders + a commit:
  `UWorld::RegisterActor` (`World.cpp:27-82`), `UWorld::SpawnActor`
  (`World.cpp:238-294`), `UWorld::DestroyActor` (`World.cpp:296-341`),
  `AActor::RegisterComponent` (`Actor.cpp:32-87`).
  1. Per function, extract the whole ladder into one documented checker
     returning the first failure:
     `EEngineResult CheckActorRegistrable(...) const`,
     `CheckSpawnable(...) const`, `CheckDestroyable(...) const`,
     `CheckComponentRegistrable(...) const`.
  2. Extract the commit halves where more than one mutation follows:
     `PublishActor(...)` / `PublishComponent(...)`.
  3. Parents become: `const EEngineResult Verdict = Check...(...); if (Verdict != EEngineResult::Success) { return Verdict; } Publish...(...); return EEngineResult::Success;`
  4. Keep the validation **order** exactly as today (tests assert specific
     verdicts for combined error conditions).

  **Done when:** each parent ≤ ~12 body lines; EngineRegistrationTests and
  EngineSpawnDestroyTests pass unchanged; Standard Verify passes.

  Done 2026-07-22 — implemented by a **Sonnet subagent**, then reviewed and
  re-verified by the lead. Each of the four functions became a thin parent
  (`Verdict = Check(...); if (Verdict != Success) return Verdict; commit; return
  Success;`) over a `const` per-function checker holding the verbatim ladder:
  `CheckActorRegistrable`/`CheckSpawnable`/`CheckDestroyable` (World) and
  `CheckComponentRegistrable` (Actor). The two-mutation commits became
  `PublishActor`/`PublishComponent`; `SpawnActor`/`DestroyActor` keep their single
  commit mutation inline (no Publish). Validation order preserved exactly — the
  diff shows every ladder body as unchanged context (no check line touched), the
  strongest evidence the combined-error verdict tests still hold; build clean,
  ctest 11/11 (EngineRegistration/SpawnDestroy in `microworld_engine_tests` #10),
  doc checker 122. CQS: each parent now cleanly separates the `const` query
  (`Check*`) from the void command (`Publish*`/inline mutation). Left intentionally
  un-DRY (reported, not hidden): the three World checkers share structurally
  similar duplicate/capacity scan blocks — merging them was rejected because it
  would risk silently reordering checks that tests assert on.

- [x] **5.2 Split `UWorld::ApplyPending`.** `World.cpp:343-436` (~91 body
  lines — the barrier that ends doomed actors, unregisters them, then begins
  pending spawns under a second guard).
  Extract, preserving order and guard scopes exactly:
  `ERuntimeResult EndDoomedActorsUnderGuard(TimePointMilliseconds)`,
  `MarkAndUnregisterDoomedActors()` (the nested search + `RemoveAt` walk),
  `ERuntimeResult BeginPendingSpawnsUnderGuard(TimePointMilliseconds)`.
  `ApplyPending` becomes: validate + early-out + three named phases + first-
  error fold. Add one why-comment: destroys run before spawns so freed
  capacity is reusable in the same barrier.

  **Done when:** `ApplyPending` ≤ 25 body lines; EngineSpawnDestroyTests and
  EngineGarbageCollectionTests pass unchanged; Standard Verify passes.

  **Evidence (2026-07-22, commit pending):** `ApplyPending` reduced to a
  ~17-logical-line orchestrator (validate + early-out + three named phase
  calls + first-error fold). Three private member helpers added to `World.h`
  after `DispatchActorEnd` and defined in `World.cpp`:
  `EndDoomedActorsUnderGuard`, `MarkAndUnregisterDoomedActors`,
  `BeginPendingSpawnsUnderGuard`. Loop bodies lifted verbatim (confirmed
  unchanged context in the diff), so runtime behavior is byte-identical. Two
  behavior-critical early-return semantics preserved: an end-guard acquire
  failure aborts before mark/unregister (so `ClearPendingDestroy` never runs),
  and a begin-guard acquire failure returns from inside the inner guard scope
  (so `ClearPendingSpawn` never runs) — both matching the original.
  **Deviations from the illustrative signatures (justified):** (a) helpers take
  `FObjectStore&` (parent validates non-null once, then passes the guaranteed
  reference — avoids redundant null-checks, `FObjectStore` already forward-
  declared in `World.h`); (b) the cascade helpers add an `ERuntimeResult&
  FirstError` out-param plus an `ERuntimeResult` guard-status return, so a
  guard-fail abort is distinguishable from a folded cascade error that spans
  both phases; (c) `EndDoomedActorsUnderGuard` drops the `TimePointMilliseconds`
  the roadmap listed — the end cascade calls `DispatchActorEnd(AActor&)`, which
  takes no time. **CQS note:** the two cascade helpers are commands that also
  report guard-status and fold the first error via out-param — an intentional
  command-with-status, consistent with the barrier's existing contract.
  Verified: build clean, ctest 11/11 (`#1 format_check`, `#10 engine_tests`
  including EngineSpawnDestroy + EngineGarbageCollection), doc checker 122 files.

- [ ] **5.3 Split `TTimerManager::Advance` and `Schedule`.** `Timer.h:223-292`
  (~67 lines) and `:148-189` (~35 lines).
  1. `Advance` → `SnapshotActiveTimers()` (the insertion-order snapshot),
     `FireAndRescheduleSlot(SlotIndex, NowMilliseconds)` (the due-check +
     one-shot-retire vs looping-reschedule branch), keep the dispatch-lock
     guard + `CompactInsertionOrder()` in the parent.
  2. `Schedule` → extract the 6-field slot population into a documented
     `FTimerSlot::Arm(Callback, FirstDeadlineMilliseconds, PeriodMilliseconds, Mode)`.

  **Done when:** `Advance` ≤ 30 body lines; EngineTimerManagerTests pass
  unchanged; Standard Verify passes.

- [ ] **5.4 Name the `TEngineHost::Tick` frame steps and the lifecycle
  cascades.**
  1. `TEngineHost::Tick` (`EngineHost.h:232-272`, ~38 lines, 7 sequenced
     subsystem calls) → extract `DispatchInboundNetwork(NowMilliseconds)`,
     `AdvanceWorldAndApplyBarrier(NowMilliseconds)`, `ReclaimAndCollect()`,
     `FlushOutboundNetwork(NowMilliseconds)`. `Tick` = validate + monotonic
     guard + four named calls; the class contract's 7-step list stays the
     single source of frame-order truth.
  2. `UWorld::BeginPlay` (`World.cpp:84-135`) → extract
     `ERuntimeResult BeginRegisteredActorsWithRollback(TimePointMilliseconds)`
     (the loop that begins actors and unwinds already-begun ones in reverse on
     failure) with a why-comment on the rollback order.
  3. `UWorld::EndPlay` (`World.cpp:178-221`) → extract
     `ERuntimeResult EndRegisteredActorsReverse()` (first-error retention).
  4. `AActor::DispatchBeginPlay` (`Actor.cpp:101-141`) → extract
     `ERuntimeResult BeginComponentsWithRollback(TimePointMilliseconds)`.

  **Done when:** each parent reads as guards + named steps; EngineLifecycle,
  EngineHost and EngineNetHost tests pass unchanged; Standard Verify passes.

---

### Phase 6 — Function decomposition: Net & Platform ⬜

- [ ] **6.1 Split the frame codec.** `FrameCodec.h`.
  1. Derive the by-hand indices `OutFrame[4 + PayloadSize]` / `[4 + PayloadSize + 1]`
     (`FrameCodec.h:120-121`) from one named `constexpr std::size_t FrameHeaderBytes`
     (and the existing overhead constant) so the layout lives in one place.
  2. `EncodeFrame` (`:87-124`, ~34 lines) → `ValidateEncodeInputs(...)`,
     `WriteFrameHeader(...)`, `AppendPayloadAndChecksum(...)`.
  3. `TFrameDecoder::PushByte` (`:179-248`, ~66 lines, one 8-state switch) →
     per-state private helpers: `BeginFrameOnMagic(IncomingByte)`,
     `CaptureSourceNodeId(...)`, `CaptureLengthByte(...)` (validates declared
     length), `AccumulatePayloadByte(...)`, `CaptureChecksumByte(...)`,
     `CompleteFrameIfChecksumMatches()`. `PushByte` stays the state dispatch
     (a switch that only dispatches is one action — Rule F).

  **Done when:** FrameCodecTests pass unchanged; Standard Verify passes.

- [ ] **6.2 Split `ReadControlMessage`; name the byte-order helpers.**
  `NetProtocol.h`.
  1. `ReadControlMessage` (`:189-236`, ~46 lines) →
     `ValidateControlPayloadLength(TypeByte, PayloadSize)` +
     `DecodeControlFields(Reader, Type, OutMessage)`.
  2. Introduce named 16-bit helpers used by both layers:
     `WriteUint16BigEndian` / `ReadUint16BigEndian` (frame length,
     `FrameCodec.h:112-113,203-205`) and `WriteUint16LittleEndian` /
     `ReadUint16LittleEndian` (message header, `NetProtocol.h:104-105,132-133`).
     Place them where both headers can share them (`ByteWriter.h`/`ByteReader.h`
     are the natural home). Add the D6 why-comment at each use: the frame
     layer is big-endian for on-air readability with LoRa sniffer tools; the
     message layer matches the little-endian byte I/O convention.

  **Done when:** NetProtocolTests + FrameCodecTests pass unchanged (D6: same
  bytes on the wire); Standard Verify passes.

- [ ] **6.3 Split the `TNetHost` handlers.** `NetHost.h`.
  1. `HandleHello` (`:571-607`, ~35 lines) → extract
     `std::size_t AdmitPeer(const FNetAddress& From, TimePointMilliseconds Now)`
     (find-or-allocate + refresh-or-init; returns `MaxPeers` on full table);
     parent keeps the guards + `SendWelcome`.
  2. `PumpSend` (`:258-286`) → `SendClientHelloIfDue(Now)`,
     `SendDueHeartbeats(Now)`, then the existing drain.
  3. `PumpReceive` (`:230-252`) → extract `DrainInboundPackets(Now)`.
  4. `Stop` (`:199-223`) → `SendByeToAllActivePeers()` + `EvictAllPeers()`.
  5. `SendTo` (`:294-319`) → extract `SendToLocalPeer(Channel, Payload)`.
  6. Hoist the shared slack in `SendQueueDepth = 2 * MaxPeers + 4`
     (`NetHost.h:117`) and `MaxReceives = MaxPeers + 4` (`:237`) into one
     `static constexpr std::size_t PumpSlackPackets = 4;` with a why-comment
     (headroom for control traffic bursts between pumps).

  **Done when:** NetHostTests + EngineNetHostTests pass unchanged; Standard
  Verify passes.

- [ ] **6.4 Unify the FIFO enqueue ladder; apply the D7 verdict fix
  (behavior change).**
  1. `TLoopbackMailboxes::Deliver` (`HostLoopback.h:40-76`) duplicates its
     store logic across the zero-length and normal paths — unify through one
     private `EnqueuePacket(Target, From, Packet)`; extract
     `ValidateDeliverAddress(...)`.
  2. `TLoopbackMailboxes::Receive` (`:84-125`) → `ValidateReceiveDestination(...)`,
     `HeadFitsDestination(...)`, `PopHeadInto(...)`.
  3. `TNetManager::QueueSend` (`NetManager.h:50-79`) — same zero-length
     duplication; unify through a private `EnqueuePacket(...)`.
  4. **D7 behavior change:** `EncodeFrame`'s oversize verdict becomes
     `ENetResult::Invalid` (today `Full`, `FrameCodec.h:102`) — oversize can
     never succeed on retry. Update the one test asserting `Full` for oversize
     (find it with `rg -n "Full" Modules/Net/tests/FrameCodecTests.cpp`).
     Document the rule once in `NetResult.h`: "oversize input → Invalid;
     out-of-capacity-now → Full."
  5. Add the D8 rationale comment on `TNetPacketStorage`: why storage and
     manager are separate types (caller-owned storage is the repo-wide
     pattern; the manager stays reusable over any backing).

  **Done when:** the only test-expectation change is the D7 oversize verdict;
  Standard Verify passes.

- [ ] **6.5 Deduplicate the UDP address codec; split the UDP drivers.**
  1. The two `UdpAddress.h` files (PlatformHost + PlatformEsp32) are
     code-identical, and both drivers hand-copy the same ntohl sender-decode
     block (`HostUdpDriver.cpp:143-149` == `Esp32UdpDriver.cpp:106-112`).
     Create **one** new portable header `Modules/Net/include/MicroWorld/Net/UdpAddressCodec.h`
     (pure arithmetic over `FNetAddress` — no OS includes, so the dependency
     rule holds) holding the encode/decode plus
     `MakeUdpAddressFromPackedHostOrder(...)`. Each platform `UdpAddress.h`
     becomes a thin forwarder (public header files must keep existing — frozen
     identity); both drivers call the shared decode.
  2. `FHostUdpDriver::TryReceive` (`HostUdpDriver.cpp:104-152`, ~46 lines; twin
     `Esp32UdpDriver.cpp:67-115`) → guards + `ProbeAndClassify` + consume +
     shared sender-decode.
  3. `ProbeReadableDatagram` (`UdpSocketPlatformImplementation.h`, was `:262-299`) —
     extract per-platform `ClassifyPeekError(ErrorCode)` so each `#ifdef`
     branch is ~5 lines.
  4. `OpenBoundLoopbackUdpSocket` (was `:369-395`, + ESP32 twin) — extract the
     thrice-repeated close-and-fail rollback into `CloseAndReportFailure(...)`.
  5. Update `Modules/Net` docs (README/AGENTS) to mention the new public
     header.

  **Done when:** `rg -n "ntohl" Modules/Platform*` shows the decode in exactly
  one shared place per platform seam; HostUdpDriverTests +
  HostNetEndToEndTests pass unchanged; Standard Verify passes.

- [ ] **6.6 Split the E32 LoRa driver.** `Esp32E32LoraDriver.cpp`.
  1. `TryReceive` (`:79-141`, ~61 lines) — the held-frame delivery block is
     duplicated at `:93-104` and `:124-134`; extract one
     `ENetResult DeliverFrameToDestination(Destination, OutFrom, OutReceived)`
     used by both sites, plus `PumpDecoderForFrame(...)` (the bounded byte
     pump).
  2. `TrySend` (`:38-77`, ~38 lines) → extract
     `ValidateOutgoingPacket(To, Packet)` and `MapUartWriteOutcome(...)`.
  3. This module has no host build: verification is a careful re-read plus
     PlatformHost still green (the shared codec from 6.5 is compiled there).

  **Done when:** duplication gone, helpers documented; Standard Verify passes.

---

### Phase 7 — Cross-cutting ceremony reduction ⬜

- [ ] **7.1 One shared construct/destroy helper for raw slots.** The
  placement-new + `std::aligned_storage` + `std::launder` ritual repeats in
  `StaticVector.h:131-142`, `Delegate.h:186-217`, and the smart-pointer
  factories. Add one small documented header
  `Modules/Memory/include/MicroWorld/Containers/RawSlot.h` (or extend an
  existing Memory header) with `ConstructAt`, `DestroyAt`, `LaunderedPointer`
  helpers and **one** authoritative why-comment explaining the launder rule;
  retarget the three call-site families. No behavior change.

  **Done when:** `rg -n "std::launder" Modules` hits only the helper (plus
  comments); Memory tests pass unchanged; Standard Verify passes.

- [ ] **7.2 Move `ERuntimeResult` into its own Core header (D5).**
  1. Create `Modules/Core/include/MicroWorld/RuntimeResult.h`; move the enum
     (with its per-enumerator docs) there verbatim; add a note that some
     values are raised only by higher layers (Object/Engine registration).
  2. `Time.h` gets `#include <MicroWorld/RuntimeResult.h>` in its place — no
     consumer breaks; consumers that only need the enum (e.g.
     `StaticVector.h`) switch their include to the new header.
  3. Add the new header to any Core file list the build/checkers read
     (Core `CMakeLists.txt` header lists, `library.json` if it enumerates).

  **Done when:** `StaticVector.h` no longer includes `Time.h`; Standard Verify
  passes; doc checker count grows by one file.

- [ ] **7.3 Label the collector back-channel and the enum boundary (D4).**
  1. In `ObjectStore.h`, the ~15 `Collector*`-prefixed private methods
     (`:453-496`) get one banner comment
     (`// --- Collector-only interface (FGarbageCollector friend) ---`) with
     two sentences on why the GC needs a private back-channel instead of
     public mutators.
  2. Each friend grant in `Object.h:65-68` and the registry leases
     (`EngineRegistryView.h:47-49,128-130`) gets a one-line why on the grant
     itself.
  3. On `EObjectResult` (`ObjectHandle.h`) and `ERuntimeResult`
     (`RuntimeResult.h` after 7.2): one line each stating the boundary — the
     store/handles speak `EObjectResult`; cross-layer lifecycle/tick speaks
     `ERuntimeResult`; they overlap by design and are not to be merged.
  4. In `ObjectStore.h`, add a three-line map of the locking trio
     (`bMutationLocked` / `bDispatchLocked` / `ActiveCollector`) to the phase
     each lock guards.

  **Done when:** all four sites documented; Standard Verify passes.

---

### Phase 8 — Student entry points & the style contract ⬜

- [ ] **8.1 Extend `docs/Style.md` with the simplicity rules.** Append a
  section covering: Rule N (long self-explanatory names, units in names,
  yes/no booleans, allowed abbreviation list from section 2.2), Rule F (≤ 2
  logical actions, guard-clause exemption, extract-named-helper recipe), and
  Rule W (why-comments; before/after example). Use `FTickFunction::Advance`
  (task 3.1) as the worked before/after. Keep the existing table and prefix
  rules untouched. State that `CheckClassDocumentation.py --require-doxygen`
  remains the enforcement gate for comment presence, and this section defines
  comment *content*.

  **Done when:** Style.md carries the three rules with one worked example.
  No Verify needed beyond `ctest --test-dir build -C Release` (markdown is not
  format-gated; the run just proves nothing else broke).

- [ ] **8.2 Make `HostLifecycle` read as a first lesson.**
  `Modules/Engine/examples/HostLifecycle/Main.cpp` (109 LOC) — the first
  engine code a student reads.
  1. Replace magic literals with named `constexpr` values:
     the tick cadence (`FTickConfiguration{true, true, 100}` → use
     `FTickConfiguration::EnabledEvery(SensorCadenceMilliseconds)` from 3.1
     with `constexpr DurationMilliseconds SensorCadenceMilliseconds = 100;`),
     the GC budget braces (`FGarbageCollectionBudget{1, 4, 8}` → named
     constants with one-line whys).
  2. Annotate the engine-host instantiation:
     `TEngineHost<5, 3, 512, 16, 1, 1, 1, 32>` → named constants
     (`MaxObjects`, `MaxClasses`, `SlotSizeBytes`, …) passed with
     `/*ParameterName*/` comments (D10).
  3. Rename `Updates` → `TickTimesMilliseconds` and give the demo's phases
     3-4 why-comments (what lifecycle law each printed line demonstrates).

  **Done when:** no unexplained numeric literal remains in the file; the
  example builds and, when run manually, prints the same trace as before;
  Standard Verify passes.

- [ ] **8.3 Restructure the `TwoNodeDemo` into named phases.**
  `Modules/PlatformHost/examples/TwoNodeDemo/Main.cpp` (380 LOC; `main` alone
  is ~165 lines — the worst first-contact file in the repo).
  1. Decompose `main` into six named free functions, in file order:
     `OpenLoopbackDriverPair`, `RegisterDemoWorld`,
     `InstallServerSpawnHandler`, `InstallClientStateHandler`,
     `ConfigureAndStartHosts`, `RunStateBroadcastLoop` — `main` becomes the
     table of contents.
  2. Extract the two inline handler lambdas (`:248-271` and the client one)
     into named functions taking a small context struct.
  3. Split the state loop (`:334-376`) into `SendSpawnRequestIfDue`,
     `AdvanceServerFrame`, `BroadcastServerState`, `DeliverToClient`.
  4. Named constants replace: `TEngineHost<6, 8, 256, 16, 1, 4, 4, 64>` and
     `TNetHost<2,256>` / `TNetHost<1,256>` capacities (D10);
     `OctetA..OctetD` → `constexpr std::uint8_t LoopbackIpv4Octets[4] = {127, 0, 0, 1};`;
     `FrameStep` → `LogicalClockStepMilliseconds`; the mutated `Now` →
     `LogicalClockMilliseconds`.
  5. The spawn schedule `bRequestsSpawn = (StateTick == 1) || (StateTick == 3)`
     gets a named predicate or constant array plus the why: exactly two spawns
     because the demo provisions two per-actor registries (`:116-123`).

  **Done when:** `main` ≤ ~30 lines; running
  `build/Modules/PlatformHost/Release/microworld_platform_host_two_node_demo.exe`
  manually produces the same demo transcript as at baseline (capture both);
  Standard Verify passes.

- [ ] **8.4 Documentation consistency sweep.** After all renames: re-grep
  every new-vs-old name pair from Phase 1 across `README.md`, root
  `AGENTS.md`, `Modules/*/README.md`, `Modules/**/AGENTS.md`,
  `docs/UE5ConceptMap.md`, `docs/ModulePackaging.md`, `docs/Style.md`,
  `docs/Porting.md`, `docs/Performance.md`, `docs/ResourceBudgets.md`.
  Fix stragglers (historical files excepted). Update `docs/UE5ConceptMap.md`
  rows that name renamed symbols (`TNetManager`, `THostLoopback`, lease
  vocabulary).

  **Done when:** every Phase-1 "old name" greps to zero outside historical
  files and this document; Standard Verify passes.

---

### Phase 9 — Governance backfill & release ⬜

- [ ] **9.1 Add the 24 missing `AGENTS.md` files.** The folder-agents gate
  (baseline table) fails on: `Modules/Engine/{benchmarks, benchmarks/Results,
  examples, examples/HostLifecycle, include, include/MicroWorld,
  include/MicroWorld/Engine, src, tests}`, `Modules/PlatformEsp32/{., benchmarks,
  benchmarks/Results, include, include/MicroWorld,
  include/MicroWorld/PlatformEsp32, src}`, and `Modules/PlatformHost/{.,
  examples, examples/TwoNodeDemo, include, include/MicroWorld,
  include/MicroWorld/PlatformHost, src, tests}`.
  Each file follows the existing convention (see
  `Modules/Object/src/AGENTS.md` and peers as templates): `Inherits
  ../AGENTS.md`, then Architecture / Concepts / Verification sections, 5-15
  lines, describing that folder's boundary — not restating code.

  **Verify:**
  ```sh
  python tools/CheckFolderAgents.py --root Modules --exclude build --exclude .pio --exclude __pycache__
  ```
  **Done when:** the checker passes; Standard Verify passes.

- [ ] **9.2 Version, changelog, progress.** This plan's renames are
  API-breaking for source consumers (`TNetManager`, `IssueLease`, lease type
  names): bump `VERSION` to `0.3.0`, add a `CHANGELOG.md` entry summarizing
  the rename tables and the single D7 behavior change, update `PROGRESS.md`
  current-state to 0.3.0 with one evidence line per phase, and bump the
  `library.json` versions + `Version.h` constants if they carry 0.2.0.

  **Done when:** version strings agree everywhere
  (`rg -n "0\.2\.0" --glob '!build' --glob '!docs/ROADMAP.md'` returns only
  historical mentions); Standard Verify passes.

- [ ] **9.3 Final acceptance.** Run everything, from scratch:
  ```sh
  cmake -S . -B build-final
  cmake --build build-final --config Release
  ctest --test-dir build-final -C Release --output-on-failure
  python tools/CheckDependencyBoundaries.py --self-test
  python tools/CheckProfileMap.py --self-test
  python tools/CheckFolderAgents.py --root Modules --exclude build --exclude build-final --exclude .pio --exclude __pycache__
  python tools/CheckClassDocumentation.py --root Modules --require-doxygen
  python tools/CheckFormatting.py
  ```
  Then run both examples manually and compare with their baseline transcripts.
  Flip the tracker fully green and add the closing `PROGRESS.md` entry.

  **Done when:** all commands pass; both examples behave as at baseline (plus
  the D7 verdict change); tracker is all ✅.

---

## 5. Progress tracker

| Phase | Title | Tasks | Status |
| --- | --- | --- | --- |
| 0 | Baseline | 1 | ✅ |
| 1 | Mechanical renames | 7 | ✅ |
| 2 | Why-comment repairs | 5 | ✅ |
| 3 | Decompose: Core & Memory | 4 | ✅ |
| 4 | Decompose: Object & GC | 3 | ✅ |
| 5 | Decompose: Engine | 4 | 🟨 |
| 6 | Decompose: Net & Platform | 6 | ⬜ |
| 7 | Ceremony reduction | 3 | ⬜ |
| 8 | Entry points & style contract | 4 | ⬜ |
| 9 | Governance & release | 3 | ⬜ |

Total: 40 tasks.

---

## 6. Deferred ideas (owner decision required — do NOT execute from this list)

- Fold `TNetPacketStorage` into `TNetManager` as a private member (removes one
  public type and the two-object wiring; costs the caller-owned-storage
  teaching pattern and deletes a public header — conflicts with frozen
  identity as written).
- Replace `TEngineHost`'s eight positional template integers with a traits
  struct (`TEngineHost<FDemoCapacities>`); cleaner call sites, but a breaking
  API reshape beyond naming.
- Rename `EngineRegistryView.h` → `EngineRegistryLease.h` once the public
  header-path freeze is relaxed.
- Unify `FGarbageCollector`'s returned enum with `EObjectResult` (one
  vocabulary per module; breaks the collector's public API and many tests).
- A tiny documented `TFixedBitset<N>` for `TFixedArena`'s marker arrays.
- Rename `HasAssignedWorld()` → `WasEverAssignedWorld()` (D12 kept the name).
- Standardize a single wire byte order (rejected as D6 — protocol break).
