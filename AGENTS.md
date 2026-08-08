# MicroWorld Repository — Contributor Guide

## Mission

MicroWorld is a mini engine for microcontrollers: a small, embedded-suitable
version of familiar UE5 engine concepts. It lets UE5 developers build small
applications, interactive software, and games for constrained devices (ESP32,
STM32, RP2040-class) without first learning every hardware detail. It keeps only
essential, bounded features — lifecycle, World/Actor/Component, GC, smart
pointers, a simple messaging and transport layer over `Core::ITransportDevice`,
and explicit hardware boundaries. Platform support is verified one target at a time, not
claimed for every board.

`docs/RADIO_TRANSPORTS_ROADMAP.md` is the one plan in this repository, and the
only place next work is tracked. The four finished plans that used to sit beside
it were deleted once their still-useful sections moved to durable homes — a
finished tracker nobody may act on earns no place in `docs/`. When this plan
closes, it goes the same way.

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
├── Modules/            the single engine package
│   ├── CMakeLists.txt  one superbuild defining every per-system target + tests
│   ├── library.json    one portable library (Platform/ excluded via srcFilter)
│   ├── MicroWorld/     the include root's only child; .h + .cpp side by side
│   │   ├── Core/       lifecycle, tick, containers, delegates, smart
│   │   │               pointers, timers, IPlaySystem
│   │   ├── Engine/     UWorld / AActor / UActorComponent + the folded Object
│   │   │               store, GC, handles, TEngine, IEngineRuntime
│   │   ├── Messaging/  FMessagingSystem — named channels, subscriptions,
│   │   │               reliable delivery (compiled static module)
│   │   ├── Networking/ client/server peer admission, liveness, and logical
│   │   │               message routing above Messaging
│   │   ├── Transport/  byte I/O, frame codec, TTransportHost + the optional E32
│   │   │               portable framing and device (was Net + RadioE32)
│   │   ├── Application/ FApplication (including the Run template)
│   │   └── Platform/   non-portable edges, each its own library.json
│   │       ├── Host/    host UDP transport
│   │       ├── Esp32/   ESP32 UDP + UART SDK bindings + optional E32 facade
│   │       └── Pico/    RP2040 UART SDK binding + optional E32 facade
│   ├── tests/          per-system host test dirs (outside MicroWorld/)
│   └── benchmarks/     per-system benchmark dirs (outside MicroWorld/)
├── examples/           PlatformIO examples + the host HostLifecycle/TwoNodeDemo
├── docs/               engine-wide design docs, ADRs, diagrams, the one plan
├── tools/              CheckDependencyBoundaries, CheckProfileMap,
│                       CheckFolderAgents, CheckDocumentationStyle,
│                       CheckFormatting, CheckNamespaces
├── CMakeLists.txt      root superbuild (adds Modules/)
└── clang-format        repo style file (invoke as --style=file:clang-format)
```

The package's version is its `library.json` plus the CMake `project()` line —
both currently 0.4.0. There is no root version file; one more copy of a number
is one more copy to leave stale.

Dependencies point inward:

```text
Core <- Messaging
Core <- Transport
Core, Messaging <- Networking
Core, Messaging, Networking <- Engine
Core, Engine <- Application
```

Object folded into Engine; Net and RadioE32 folded into Transport. Networking
is restored above Messaging: it owns logical peer/session policy and never
depends on Transport, a platform, Engine, or Application. Transport never pulls
Engine, and no portable system sees both Engine and Transport. Messaging and
Transport meet only at Core's `ITransportDevice`: a channel sends through the
interface, each medium realises it, and a concrete device is named only at the
application entry point.
Platform/Host, Platform/Esp32, and Platform/Pico are the non-portable edges;
only they may reach OS/SDK headers.

`CLAUDE.md` at this level carries the architecture overview and each module's
responsibility in one place.

## Architecture and concepts

- Keep hardware access at the edges; domain/runtime code is platform-neutral.
- Application entry points own concrete objects; dependencies point inward toward Core.
- Explicit state and typed results replace toggles and exception-driven control.
- Caller-supplied monotonic time keeps scheduling, safety deadlines, and tests
  deterministic without hidden clock reads.
- Fixed-capacity storage and bounded work make MCU memory/timing reviewable.
- Portable code: C++17, strict warnings (`-Werror`), no RTTI, no exceptions, no
  heap in steady-state, no global mutable state, no boolean state soup.

## Identity (frozen — do not rename during moves/refactors)

CMake `project()` names, the per-system targets (`microworld`,
`microworld_engine`, `microworld_messaging`, `microworld_transport`,
`microworld_networking`, `microworld_application`, `microworld_platform_host`),
their `MicroWorld::*` aliases, the `library.json` package names/versions, and the
`MicroWorld/<System>/<Name>.h` header layout stay exactly as they are. The folder
tree under `Modules/MicroWorld/` now mirrors the six-system architecture model;
future refactors preserve the system-directory names and the side-by-side
`.h`/`.cpp` layout.

## Code documentation and formatting

- Format C/C++ with the tracked `clang-format` policy. The filename has no
  leading dot, so invoke it explicitly:
  `clang-format --style=file:clang-format ...`
  Bare `clang-format` falls back to LLVM style and produces false positives.
- Document every C++ declaration with a tiered `/** ... */` contract: every
  `class`, `struct`, and `enum class` carries `Motivation`, `Responsibilities`,
  and `Example`; every function carries `Motivation` and `Responsibilities`;
  every variable, enumerator, and `using`/typedef carries a one-line
  `Motivation`. State why the declaration exists (Motivation), what it owes
  once it exists (Responsibilities), and one high-level use (Example, types
  only). `tools/CheckDocumentationStyle.py` enforces the labels repo-wide.
- Every scoped `AGENTS.md` describes the architecture, concepts, dependency
  direction, and verification owned by its directory.

## Build and verification

Superbuild (the single Modules package, all six systems plus platform edges)
from the repo root:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The superbuild is the only path. Standalone per-module configuration is gone —
the engine's systems live in one package and one `CMakeLists.txt`, so there is
no `cmake -S Modules/<Name>` form to fall back on.

Checkers (run per their documented args; see `tools/AGENTS.md`):

```sh
python tools/CheckDependencyBoundaries.py --self-test
python tools/CheckProfileMap.py --self-test
python tools/CheckFolderAgents.py --self-test
python tools/CheckFolderAgents.py --root Modules
python tools/CheckDocumentationStyle.py --self-test
python tools/CheckDocumentationStyle.py --root .
python tools/CheckFormatting.py
python tools/CheckNamespaces.py --self-test
python tools/CheckNamespaces.py
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

<!-- code-review-graph MCP tools -->
## MCP Tools: code-review-graph

**IMPORTANT: This project has a knowledge graph. ALWAYS use the
code-review-graph MCP tools BEFORE using Grep/Glob/Read to explore
the codebase.** The graph is faster, cheaper (fewer tokens), and gives
you structural context (callers, dependents, test coverage) that file
scanning cannot.

### When to use graph tools FIRST

- **Exploring code**: `semantic_search_nodes_tool` or `query_graph_tool` instead of Grep
- **Understanding impact**: `get_impact_radius_tool` instead of manually tracing imports
- **Code review**: `detect_changes_tool` + `get_review_context_tool` instead of reading entire files
- **Finding relationships**: `query_graph_tool` with callers_of/callees_of/imports_of/tests_for
- **Architecture questions**: `get_architecture_overview_tool` + `list_communities_tool`

Fall back to Grep/Glob/Read **only** when the graph doesn't cover what you need.

### Key Tools

| Tool | Use when |
| ------ | ---------- |
| `detect_changes_tool` | Reviewing code changes — gives risk-scored analysis |
| `get_review_context_tool` | Need source snippets for review — token-efficient |
| `get_impact_radius_tool` | Understanding blast radius of a change |
| `get_affected_flows_tool` | Finding which execution paths are impacted |
| `query_graph_tool` | Tracing callers, callees, imports, tests, dependencies |
| `semantic_search_nodes_tool` | Finding functions/classes by name or keyword |
| `get_architecture_overview_tool` | Understanding high-level codebase structure |
| `refactor_tool` | Planning renames, finding dead code |

### Workflow

1. The graph auto-updates on file changes (via hooks).
2. Use `detect_changes_tool` for code review.
3. Use `get_affected_flows_tool` to understand impact.
4. Use `query_graph_tool` pattern="tests_for" to check coverage.
