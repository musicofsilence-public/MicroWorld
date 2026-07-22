# Host Benchmark Result

Status: measured locally on 2026-07-19 from exact committed MicroWorld
production and validation sources. This is a correctness-first host baseline,
not target approval.

> **Historical (retired model).** The `FActorComponent`, `TActor<4>`,
> `TWorld<8>`, and `FNetwork` object-size rows and the dispatch profiles below
> measure the Core actor model retired in the Phase 1 consolidation
> ([roadmap](../../../../MICROWORLD_ROADMAP.md)). They are kept as the original
> 0.1.0 host baseline, not current Core structure — Core is now lifecycle/tick
> primitives only. Managed-engine runtime measurement returns in Phase 6.

| Field | Value |
|---|---|
| Host | Windows x64 |
| Compiler | MSVC 19.44.35226.0 |
| CMake | 4.0.2 |
| Generator | Visual Studio 17 2022 |
| MSBuild | 17.14.40 |
| Configuration | Release (`/O2` through the MSVC CMake profile) |
| LTO | Off |
| Trials | 30 |
| Warm-up / measured updates | 1,000 / 10,000 per trial |
| Repository HEAD | `c54f3c4a14f682a572dc8dfa8fe219f153fe5280` |
| Source state at measurement | Tracked MicroWorld production, tests, benchmark harness, and build inputs matched HEAD; only untracked `.claude` concept/plan documents existed |

## Verification completed

- standalone CMake configure and complete Release build passed with strict
  warnings;
- CTest passed its one executable entry, which contains all 31 MicroWorld
  behavior cases;
- the no-exceptions/no-RTTI consumer probe built and returned success;
- the host lifecycle example returned success and produced its documented
  Component-before-Actor trace;
- the public declaration-documentation check passed for all 10 headers;
- scoped folder guidance passed for all 15 existing guides;
- all maintained C/C++ files passed the tracked `clang-format` dry-run policy.

The separate PlatformIO native consumer resolved MicroWorld 0.1.0, compiled
with WinLibs GCC 16.1.0 (`x86_64-w64-mingw32`, UCRT, POSIX threads), linked
`program.exe`, and returned exit code zero. The compiler was installed through
WinGet and its `mingw64/bin` directory is present on the user `PATH`.

## Object sizes

| Type | Bytes |
|---|---:|
| `FTickFunction` | 32 |
| `FActorComponent` | 56 |
| benchmark `TActor<4>` | 104 |
| `TWorld<8>` | 88 |
| `FNetwork` | 48 |
| `FPerformanceSample` | 40 |

## Dispatch profiles

| Workload | Median ns/update | p95 | Worst | Steady-state allocations |
|---|---:|---:|---:|---:|
| Disabled | 4 | 5 | 5 | 0 |
| All due | 11 | 12 | 13 | 0 |
| Mixed rate | 9 | 12 | 14 | 0 |
| Maximum capacity (8 Actors × 4 Components) | 227 | 322 | 327 | 0 |

Host scheduling and background load can affect nanosecond timing. These values
are one reproduced local relative baseline. They do not establish ESP32,
STM32, or RP2040/RP2350 cycles, flash, static RAM, stack margin, or physical
behavior.
