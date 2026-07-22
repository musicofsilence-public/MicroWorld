# MicroWorld Memory Host Benchmark

Status: **runtime-measured development evidence on 2026-07-19; the owner
accepted Gate C for roadmap progression. Memory remains experimental pending
target-margin evidence.**

## Environment

| Field | Value |
| --- | --- |
| Host | Windows x64 |
| Compiler | MSVC 19.44.35226.0 |
| CMake | 4.0.2 |
| Generator | Visual Studio 17 2022 |
| MSBuild | 17.14.40 |
| Configuration | Release |
| C++ mode | C++17 |
| Repository HEAD | `c54f3c4a14f682a572dc8dfa8fe219f153fe5280` |
| Source state | Dirty Phase 0–2 roadmap implementation; benchmark measured the current working-tree Memory APIs and Gate C harness |

The benchmark was configured from `lib/microworld-memory` with
`MICROWORLD_MEMORY_BUILD_BENCHMARKS=ON`, built with strict warnings, and
returned success. The standard shared-pointer prototype alone used its normal
exceptions-enabled contract. Production Memory and the standalone consumer
were compiled separately with exceptions and RTTI disabled.

## Layout and attribution

| Item | Bytes / result |
| --- | ---: |
| `TFixedArena<256, alignof(max_align_t)>` object | 344 |
| Fixed-arena caller payload | 256 |
| Fixed-arena object overhead | 88 |
| `TUniquePtr<FBenchmarkValue>` | 32 |
| Equivalent `std::unique_ptr` plus resource deleter | 32 |
| Unique allocation payload | 16 |
| `TSharedPtr<FBenchmarkValue>` | 8 |
| `TWeakPtr<FBenchmarkValue>` | 8 |
| Custom combined object/control-block allocation | 56 |
| `std::shared_ptr<FBenchmarkValue>` | 16 |
| `std::weak_ptr<FBenchmarkValue>` | 16 |
| Attributed `std::allocate_shared` block | 40 |
| `TStaticVector<uint32_t, 4>` | 24 |
| `TSpan<const uint32_t>` | 16 |
| `TDelegate<void(uint32_t), 32>` | 56 |
| `TMulticastDelegate<void(uint32_t), 2, 32>` | 160 |

Both unique-pointer variants performed one injected 16-byte allocation and one
exact deallocation with zero global allocations. The custom shared/weak
workload performed one injected 56-byte allocation, retained it through weak
expiry, returned it once, and produced zero global allocations.

The successful `std::allocate_shared` prototype performed one attributed
40-byte global allocation and one deallocation. It was not subjected to
deliberate OOM: C++17 reports that failure through exceptions, so it cannot
satisfy the portable typed-OOM contract when exceptions are disabled.

## Bounded operation workloads

Each workload executed exactly 100,000 public operations and validated its
semantic counter before reporting.

| Workload | Elapsed ns | Semantic result | Global allocation delta |
| --- | ---: | ---: | ---: |
| `TStaticVector` construct/add/iterate | 231,800 | sum 5,000,550,000 | 0 |
| `TSpan` iterate | 115,900 | sum 1,700,000 | 0 |
| `TDelegate::Execute` | 46,300 | 100,000 callbacks | 0 |
| Two-binding multicast broadcast | 288,700 | weighted callbacks 300,000 | 0 |

Host nanoseconds are relative development evidence and may vary with scheduler,
power, and background load. No absolute performance budget was invented, and
these measurements do not establish MCU timing, stack, heap, or hardware
behavior.

## Verification

- benchmark build: passed with `/W4 /WX`;
- benchmark result: `summary,passed=1,total_global_allocations=1`;
- the one global allocation belongs to the explicitly attributed standard
  shared-pointer prototype;
- CTest integration: all 27 Memory/delegate cases passed through the shared
  Core harness (`1/1` CTest executable) with strict warnings, exceptions
  disabled, and RTTI disabled;
- standalone Core+Memory consumer: built and returned success with
  `/EHs-c- /GR-`;
- host Memory profile map: passed at 33,846 bytes and contained both Core and
  Memory archive evidence while rejecting later-module markers;
- the full 27-case Memory suite passed with exit code zero under the paired
  Visual Studio 18 Insiders Clang++ 20.1.8 AddressSanitizer and
  UndefinedBehaviorSanitizer runtime at `build/clang20-sanitize`;
- the earlier installed Windows sanitizer runtime remains a historical mismatch:
  it emitted a post-summary interception fault after assertions, while its
  UndefinedBehaviorSanitizer runtime could not link. It does not limit the
  paired-runtime sanitizer evidence above.
