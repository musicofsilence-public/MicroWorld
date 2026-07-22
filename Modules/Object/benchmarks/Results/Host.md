# Object Host Evidence

## Source boundary

Production source, tests, standalone consumer, and benchmark are anchored to
commit `e1e7b75`. Commit `cf5d964` and the current documentation cleanup change
metadata and prose only; they do not change the executable Object evidence.

## Environment and verification

| Item | Recorded value |
| --- | --- |
| Host | Windows x64 |
| CMake | 4.0.2 |
| MSVC | 19.44.35226.0, Release, exceptions and RTTI disabled |
| MSVC tests | 25/25 passed |
| WinLibs GCC | 16.1.0, `-O2 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti` |
| GCC tests | 25/25 passed |
| Clang | Visual Studio 18 Insiders Clang++ 20.1.8 with paired ASan+UBSan runtime |
| Clang sanitizer tests | 25/25 passed clean |
| Standalone consumer | Core+Memory+Object consumer passed |
| Object profile map | 43,939 bytes; profile check passed |

## Fixed collector workload

| Observation | Recorded value |
| --- | ---: |
| Constructed nodes | 64 |
| Rooted survivors | 32 |
| Reclaimed unreachable cycle | 32 |
| Semantic operations | 97 |
| Full collection | 1 slice; maximum 97 operations |
| Incremental budgets `{2,4,8}` | 15 slices; maximum 12 operations |
| Slot storage | 8,192 bytes |
| Metadata storage | 2,048 bytes |
| Root storage | 8 bytes |
| Worklist storage | 512 bytes |
| Payload / slot | 64 / 128 bytes |
| Live payload after collection | 2,048 bytes |
| Fixed-slot fragmentation after collection | 2,048 bytes |

Elapsed timings from this workload are host-only comparisons. They are not MCU
latency, deadline, stack, heap, power, or accepted target-budget evidence.
