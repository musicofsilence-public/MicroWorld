# Engine Host Evidence

Status: **host behavior and strict-consumer evidence recorded against source/test
commit `52055fc` on 2026-07-20. The Simple Timers milestone is an accepted
implementation candidate; target runtime margins remain unmeasured.**

## Tested commit

The Engine production source, tests, and standalone consumer verified by this
record are anchored to commit **`52055fc`**
(`test(microworld): prove timer mixed stable compaction and tail reuse`).
That commit adds the final mixed-stable-compaction regression and corrects two
`Timer.h` comments; it changes no timer logic. The prior correction
`98b53c6` established the accepted production behavior (explicit mode allowlist,
single-pass post-dispatch compaction, redundant destructor removed, handle
locality documented).

## Environment and verification

| Item | Recorded value |
| --- | --- |
| Host | Windows x64 |
| CMake | 4.0.2 |
| Generator | Visual Studio 17 2022 |
| MSVC | 19.44.35226.0, Release |
| MSVC test warning gate | `/W4 /WX /Zc:__cplusplus` |
| Exceptions / RTTI (production + tests) | `/EHs-c-` / `/GR-` |
| MSVC Engine tests | 55/55 passed |
| WinLibs GCC | 16.1.0 (MinGW-W64 ucrt-posix-seh) |
| LLVM Clang | 19.1.5 (Target: x86_64-pc-windows-msvc), against MSVC 14.44.35207 STL |
| Standalone Engine consumer | Built with `/W4 /WX /EHs-c- /GR-` and exited 0 |
| Four-package dependency check | Passed across 42 files |
| Engine class-documentation check | Passed across 18 files (`--require-doxygen --max-sentences 3`) |
| Managed profile map (host consumer) | Passed |

## Corrected timer behavior

| Area | Recorded value |
| --- | --- |
| Timer behavior cases | 34 (one-shot, looping, cancellation, handles, capacity/invalid input, caller-supplied time, ordering incl. mixed stable compaction and tail reuse, dispatch mutation rules, allocation) |
| Lifecycle / registration / GC cases | 21 (unchanged) |
| Total Engine behavior cases | 55 |

`Schedule` accepts only `OneShot` and `Looping`; every other `ETimerMode`
(`None` and arbitrary casts such as `static_cast<ETimerMode>(3)`) is rejected
transactionally as `InvalidMode`. `Advance` clears completed one-shot slots in
place during dispatch and removes them all in a single stable post-dispatch
compaction pass, preserving the relative order of every survivor. The
`EngineTimerMixedStableCompactionPreservesSurvivorsAndTailReuse` regression
proves that two looping survivors around a removed one-shot keep their order
and that a replacement scheduled into the freed physical slot dispatches at the
logical insertion tail, not at the freed slot's original position.

`FTimerHandle` is documented as a plain {slot index, generation} pair local to
the issuing `TTimerManager`. Deleted move operations preserve the deliberately
simple application-owned manager lifetime/identity boundary; relocation would
not mechanically rewrite handles, so the rationale is ownership-explicitness,
not mechanical invalidation. The redundant explicit destructor loop was removed
because each `FTimerSlot` owns its `TDelegate` member and `TDelegate` destroys
its bound callable exactly once.

## Steady-state allocation

`EngineTimerOperationsPerformNoObservableAllocation` exercises Schedule, Advance
dispatch, Cancel, slot reuse, and looping operation under a process-wide
`operator new`/`delete` override. It asserts every Schedule, Advance, and
Cancel result, asserts callback counts and `TimerCount()` after each step,
actually dispatches the reused one-shot at its calculated deadline, and records
zero scalar, array, or aligned global allocations across the whole sequence.

## Exact verification commands

Run every command below from the repository root in PowerShell. All build
outputs are placed under `%TEMP%` so verification does not create repository
artifacts.

### Release tests (Core, Memory, Object, Engine)

```powershell
$EvidenceRoot = Join-Path $env:TEMP 'microworld-engine-host-evidence-52055fc'
$CoreBuild = Join-Path $EvidenceRoot 'core'
$MemoryBuild = Join-Path $EvidenceRoot 'memory'
$ObjectBuild = Join-Path $EvidenceRoot 'object'
$EngineBuild = Join-Path $EvidenceRoot 'engine'

cmake -S lib/microworld -B $CoreBuild -DMICROWORLD_BUILD_TESTS=ON -DMICROWORLD_BUILD_BENCHMARKS=OFF -DMICROWORLD_BUILD_EXAMPLES=OFF
cmake --build $CoreBuild --config Release
ctest --test-dir $CoreBuild -C Release --output-on-failure

cmake -S lib/microworld-memory -B $MemoryBuild -DMICROWORLD_MEMORY_BUILD_TESTS=ON -DMICROWORLD_MEMORY_BUILD_BENCHMARKS=OFF
cmake --build $MemoryBuild --config Release
ctest --test-dir $MemoryBuild -C Release --output-on-failure

cmake -S lib/microworld-object -B $ObjectBuild -DMICROWORLD_OBJECT_BUILD_TESTS=ON -DMICROWORLD_OBJECT_BUILD_BENCHMARKS=OFF
cmake --build $ObjectBuild --config Release
ctest --test-dir $ObjectBuild -C Release --output-on-failure

cmake -S lib/microworld-engine -B $EngineBuild -DMICROWORLD_ENGINE_BUILD_TESTS=ON -DMICROWORLD_ENGINE_BUILD_BENCHMARKS=OFF
cmake --build $EngineBuild --config Release
ctest --test-dir $EngineBuild -C Release --output-on-failure
& (Join-Path $EngineBuild 'Release\microworld_engine_tests.exe')
```

### Strict GCC 16.1.0 timer-TU compile

```powershell
$Gcc = 'C:\Users\chorn\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\g++.exe'
$GccObject = Join-Path $EvidenceRoot 'EngineTimerManagerTests.gcc.obj'
& $Gcc `
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti `
  -I lib/microworld/include `
  -I lib/microworld-memory/include `
  -I lib/microworld-object/include `
  -I lib/microworld-engine/include `
  -I lib/microworld/tests `
  -I lib/microworld-engine/tests `
  -c lib/microworld-engine/tests/EngineTimerManagerTests.cpp `
  -o $GccObject
```

Result: exit 0, no warnings.

### Strict Clang 19.1.5 timer-TU compile

The VS-bundled LLVM Clang 19.1.5 targets `x86_64-pc-windows-msvc` and must use
the VS 2022 MSVC STL (`14.44.35207`); the VS 18 Insiders STL on this host
demands Clang 20+ and would reject any C++ TU including `<cstdint>` with
`error STL1000`, independent of source. The compile uses `-nostdinc++` plus the
VS 2022 STL include path:

```powershell
$Clang = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang++.exe'
$MsvcStl = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\include'
$ClangObject = Join-Path $EvidenceRoot 'EngineTimerManagerTests.clang.obj'
& $Clang `
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti `
  -nostdinc++ `
  -isystem $MsvcStl `
  -I lib/microworld/include `
  -I lib/microworld-memory/include `
  -I lib/microworld-object/include `
  -I lib/microworld-engine/include `
  -I lib/microworld/tests `
  -I lib/microworld-engine/tests `
  -c lib/microworld-engine/tests/EngineTimerManagerTests.cpp `
  -o $ClangObject
```

Result: exit 0, no warnings.

### Strict standalone Engine consumer

```powershell
$ConsumerBuild = Join-Path $EvidenceRoot 'consumer'
cmake -S lib/microworld/tests/consumer -B $ConsumerBuild -DMICROWORLD_STANDALONE_ENGINE_CONSUMER=ON
cmake --build $ConsumerBuild --config Release
& (Join-Path $ConsumerBuild 'Release\microworld_engine_consumer.exe')
```

Result: exit 0.

### Static checks

```powershell
python lib/microworld/tools/CheckDependencyBoundaries.py `
  --package Core=lib/microworld `
  --package Memory=lib/microworld-memory `
  --package Object=lib/microworld-object `
  --package Engine=lib/microworld-engine `
  --exclude build --exclude .pio --exclude __pycache__

python lib/microworld/tools/CheckClassDocumentation.py `
  --root lib/microworld-engine --exclude build --exclude .pio `
  --exclude __pycache__ --require-doxygen --max-sentences 3

$ConsumerMap = Join-Path $ConsumerBuild 'microworld_engine_profile.map'
python lib/microworld/tools/CheckProfileMap.py --map $ConsumerMap --profile Managed
```

Results: dependency-boundary check passed (4 packages, 42 files);
class-documentation check passed (18 files); Managed profile map check passed.

Host behavior and compile evidence do not establish target runtime timing,
stack, heap, power, or physical-hardware behavior. No firmware upload, run, or
hardware measurement was performed.
