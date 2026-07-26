# MicroWorld Repository — Contributor Guide

## Mission

MicroWorld is a mini engine for microcontrollers: a small, embedded-suitable
version of familiar UE5 engine concepts. It lets UE5 developers build small
applications, interactive software, and games for constrained devices (ESP32,
STM32, RP2040-class) without first learning every hardware detail. It keeps only
essential, bounded features — lifecycle, World/Actor/Component, GC, smart
pointers, a simple `TNetManager`/`INetDriver` networking layer, and explicit
hardware boundaries. Platform support is verified one target at a time, not
claimed for every board.

`docs/RADIO_TRANSPORTS_ROADMAP.md` is the one still-active plan (E32 LoRa and
Bluetooth LE radio transports) and the only place next work is tracked. Every
other `docs/*_ROADMAP.md` is a finished plan kept only as a historical record —
do not read one as guidance.

Status has four owners, and no file summarizes them. What changed lives in git
history; headers and tests define current behavior; hardware evidence lives in
each example's `README.md`; measured margins live in
`Modules/*/benchmarks/Results/` and are indexed by `docs/ResourceBudgets.md`.
`PROGRESS.md` and `CHANGELOG.md` were deleted on 2026-07-26 — they had become a
third and fourth record of those same facts, and both had drifted out of date.
Do not reintroduce either; put the fact where its owner already is.

## Repository structure

```text
MicroWorld/
├── Modules/            One CMake/PlatformIO package per engine layer
│   ├── Core/           lifecycle, tick, containers, delegates, smart
│   │                   pointers, timers, IEngineSystem
│   ├── Object/         object store, garbage collector, handles
│   ├── Engine/         UWorld / AActor / UActorComponent, TEngine, IEngine
│   ├── Messaging/      message router, channel bindings (header-only)
│   ├── Net/            byte I/O, frame codec, TNetHost
│   ├── Application/    FApplication (including the Run template)
│   ├── Integration/    TNetSystem — the only Engine + Net joiner
│   ├── PlatformHost/   host UDP transport (non-portable)
│   └── PlatformEsp32/  ESP32 UDP + E32 LoRa UART (PlatformIO/ESP-IDF only)
├── docs/               engine-wide design docs, ADRs, diagrams, ROADMAP
├── tools/              CheckDependencyBoundaries, CheckProfileMap,
│                       CheckFolderAgents, CheckClassDocumentation
├── CMakeLists.txt      root superbuild (adds every portable/host module)
└── clang-format        repo style file (invoke as --style=file:clang-format)
```

Each package's version is its `library.json` plus its CMake `project()` line —
both currently 0.3.0. There is no root version file; one more copy of a number
is one more copy to leave stale.

Dependencies point inward:

```text
Core <- Object <- Engine <- Application
Core <- Messaging
Core <- Net
Core, Object, Messaging, Engine, Net <- Integration
```

Memory is folded into Core; no Memory package edge remains. Net never pulls
Object or Engine, and Integration is the only package permitted to see both
Engine and Net. PlatformHost and PlatformEsp32 are the non-portable edges; only
they may reach OS/SDK headers.

`CLAUDE.md` at this level carries the architecture overview and each module's
responsibility in one place.

## Architecture and concepts

- Keep hardware access at the edges; domain/runtime code is platform-neutral.
- Composition roots own concrete objects; dependencies point inward toward Core.
- Explicit state and typed results replace toggles and exception-driven control.
- Caller-supplied monotonic time keeps scheduling, safety deadlines, and tests
  deterministic without hidden clock reads.
- Fixed-capacity storage and bounded work make MCU memory/timing reviewable.
- Portable code: C++17, strict warnings (`-Werror`), no RTTI, no exceptions, no
  heap in steady-state, no global mutable state, no boolean state soup.

## Identity (frozen — do not rename during moves/refactors)

CMake `project()` names, targets (`microworld_memory` etc.), `MicroWorld::*`
aliases, `library.json` package names/versions, and the
`include/MicroWorld/...` header layout stay exactly as they are. Only directory
names and path references change.

## Code documentation and formatting

- Format C/C++ with the tracked `clang-format` policy. The filename has no
  leading dot, so invoke it explicitly:
  `clang-format --style=file:clang-format ...`
  Bare `clang-format` falls back to LLVM style and produces false positives.
- Document every function declaration and every persistent, shared,
  configuration, or state variable with why it exists, the ownership/lifecycle
  boundary it protects, or the invariant it makes observable.
- Every scoped `AGENTS.md` describes the architecture, concepts, dependency
  direction, and verification owned by its directory.

## Build and verification

Superbuild (all portable/host modules) from the repo root:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Standalone per-module (same shape) — useful for exercising the deepest sibling
chains, e.g. `Modules/Engine` or `Modules/PlatformHost`:

```sh
cmake -S Modules/Engine -B build-engine
cmake --build build-engine --config Release
ctest --test-dir build-engine -C Release --output-on-failure
```

Checkers (run per their documented args; see `tools/AGENTS.md`):

```sh
python tools/CheckDependencyBoundaries.py --self-test
python tools/CheckProfileMap.py --self-test
python tools/CheckFolderAgents.py --self-test
python tools/CheckFolderAgents.py --root Modules
python tools/CheckClassDocumentation.py --self-test
python tools/CheckClassDocumentation.py --root . --require-doxygen
python tools/CheckFormatting.py
```

`ctest --test-dir build` runs every one of them, each alongside its self-test, so
the list above is for reproducing one failure in isolation rather than a routine
step. Each checker skips generated trees through its own
`DEFAULT_EXCLUDED_DIRECTORY_NAMES`, so no `--exclude` chain belongs in a normal
invocation.

Formatting gate (mandatory — not covered by any other gate). `CheckFormatting.py`
is also wired into ctest as `microworld_format_check`, so it runs with every
`ctest --test-dir build`:

```sh
python tools/CheckFormatting.py
# equivalent manual invocation:
clang-format --style=file:clang-format --dry-run --Werror $(git ls-files 'Modules/**/*.h' 'Modules/**/*.cpp')
```

Before a change is complete: build every affected module, run host tests for
pure protocol/timing/policy logic, run the checkers, run the formatting gate,
and treat warnings as defects. Never claim a build, test, measurement, or
hardware behavior that was not actually verified.
