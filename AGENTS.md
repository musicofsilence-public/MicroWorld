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

`PROGRESS.md` is the sole live implementation status and next-milestone record;
`docs/SIMPLICITY_ROADMAP.md` is the active improvement plan and task tracker
(`docs/ROADMAP.md` is the completed implementation plan, kept as a frozen
historical record). `docs/MESSAGING_ROADMAP.md` is the active plan for actor
messaging and engine-first examples, and `docs/RADIO_TRANSPORTS_ROADMAP.md` is
the active plan for the E32 LoRa and Bluetooth LE radio transports. Any change
to implementation, gate, evidence, decision, blocker, or next milestone must
update `PROGRESS.md` in the same commit.

## Repository structure

```text
MicroWorld/
├── Modules/            One CMake/PlatformIO package per engine layer
│   ├── Core/           lifecycle, tick, application primitives
│   ├── Memory/         memory resource, arena, smart pointers
│   ├── Object/         object store, garbage collector
│   ├── Engine/         UWorld / AActor / UActorComponent, timers
│   ├── Net/            byte I/O, frame codec, TNetManager
│   ├── PlatformHost/   host UDP transport (non-portable)
│   └── PlatformEsp32/  ESP32 UDP + E32 LoRa UART (PlatformIO/ESP-IDF only)
├── docs/               engine-wide design docs, ADRs, diagrams, ROADMAP
├── tools/              CheckDependencyBoundaries, CheckProfileMap,
│                       CheckFolderAgents, CheckClassDocumentation
├── CMakeLists.txt      root superbuild (adds every portable/host module)
├── clang-format        repo style file (invoke as --style=file:clang-format)
├── PROGRESS.md         sole live status + evidence record
├── CHANGELOG.md        released changes
└── VERSION             released version string
```

Dependencies point inward:

```text
Core <- Memory <- Object <- Engine
Core <- Memory <- Net
```

Net never pulls Object or Engine. PlatformHost and PlatformEsp32 are the
non-portable edges; only they may reach OS/SDK headers.

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
python tools/CheckFolderAgents.py --root Modules --exclude build --exclude .pio --exclude __pycache__
python tools/CheckClassDocumentation.py --root Modules --require-doxygen
python tools/CheckFormatting.py
```

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
