# MicroWorld Net Host Evidence

Status: **host behavior and strict host compile verified on 2026-07-20 after
the review-correction pass; no upload, execution, radio transmit, or hardware
measurement was performed.**

## Environment

| Tool | Version |
| --- | --- |
| CMake | 4.0.2 |
| Microsoft Visual Studio 2022 MSVC | 19.44.35226 (default generator) |
| MinGW-W64 GCC | 16.1.0 (x86_64-ucrt-posix-seh) |
| LLVM Clang | 19.1.5 (Target: x86_64-pc-windows-msvc) |
| Python | 3.11.9 |
| Host | Windows 10.0.26200 x64 |

## Net CMake configure, build, CTest, and direct behavior run

All artifacts are written under an isolated `%TEMP%` directory so no generated
file enters the repository.

```powershell
$EvidenceRoot = Join-Path $env:TEMP 'microworld-net-host'
$NetBuild = Join-Path $EvidenceRoot 'net-build'
cmake -S lib/microworld-net -B $NetBuild
cmake --build $NetBuild --config Release
ctest --test-dir $NetBuild -C Release --output-on-failure
& (Join-Path $NetBuild 'Release\microworld_net_tests.exe')
```

Result:

- `ctest` reports `100% tests passed, 0 tests failed out of 1` (one aggregated
  CTest case).
- The direct executable reports `[SUMMARY] 52 tests, 0 failures`.
- The allocation case `NetOperationsPerformNoObservableAllocation` proves
  steady-state byte writer/reader, manager queue/send-advance/receive,
  loopback delivery/full/unavailable, drain, and reuse paths perform zero
  scalar, array, or aligned global allocations.

Behavior coverage (52 cases): byte writer boundaries, oversized-span
(`> total capacity`) `Invalid` versus remaining-overflow `Full`,
invalid `{nullptr, nonzero}` backing-buffer `Invalid` without null
dereference, valid empty-buffer contract, transactional cursor and
accepted-byte preservation, empty-span no-op semantics, reset and
accepted-prefix view; byte reader boundaries, truncated/exhausted reads return
`Invalid`, invalid `{nullptr, nonzero}` backing-source `Invalid` without null
dereference, valid empty `{nullptr, 0}` source reporting an empty
`RemainingBytes` view with a null data pointer (no pointer arithmetic on
null), output preservation on failure, empty-destination no-op, peek without
advance, remaining-suffix view; host loopback FIFO delivery, exact
capacities, oversized-packet `Invalid`, full-queue backpressure without
overwrite, empty-receive `Unavailable`, null destination with nonzero length
returns `Invalid` before the empty-queue check (transactional rejection even
on an empty loopback) and retains the head when one is queued, too-small-
destination `Full` head retention with sentinel-initialized transactional
outputs, drain and capacity reuse, zero-length round-trip, null-packet
`Invalid`, `INetDriver` interface satisfaction; manager fixed configuration,
oversized-packet `Invalid` and null-packet `Invalid` rejection, recorded-
packet FIFO ordering across differently sized and valued packets, full-FIFO
rejection, one-send-per-advance, exact head retention across driver
`Full`/`Unavailable`/`Invalid`, recovery sending the retained head before
later packets, caller-storage reuse across wraparound and draining,
transactional receive with sentinel-initialized failure outputs, success and
byte-count propagation. The caller-owned `FNetPacketStorage` packet arrays
are private and observed only through the matching `FNetManager`
specialization, so all storage behavior is proven via the manager and driver.

## Strict standalone Core+Memory+Net consumer

The consumer links `MicroWorld::Net` (which pulls Core and Memory), compiles
under C++17 with strict warnings, exceptions disabled, and RTTI disabled, and
exits zero.

```powershell
$ConsumerBuild = Join-Path $EvidenceRoot 'consumer'
cmake -S lib/microworld/tests/consumer -B $ConsumerBuild -DMICROWORLD_STANDALONE_NET_CONSUMER=ON
cmake --build $ConsumerBuild --config Release
& (Join-Path $ConsumerBuild 'Release\microworld_net_consumer.exe')
```

Result: exit 0. The probe exercises byte writer overflow and accepted-prefix
preservation, byte reader truncated read and output preservation, loopback FIFO
delivery, full backpressure, empty `Unavailable`, too-small `Full`, manager
queue/advance/receive, driver-Full head retention, backpressure recovery, and
direct-receive success propagation.

## Strict Net public-API single-translation-unit compile

Each Net public header is compiled alone under strict GCC 16 and Clang 19
warnings with exceptions and RTTI disabled.

### GCC 16.1.0

```powershell
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti `
    -I lib/microworld/include `
    -I lib/microworld-memory/include `
    -I lib/microworld-net/include `
    -c <probe-including-one-net-header>.cpp `
    -o <object>
```

Result: exit 0, no warnings, for `NetResult.h`, `ByteWriter.h`, `ByteReader.h`,
`NetDriver.h`, `NetPacketStorage.h`, `HostLoopback.h`, `NetManager.h`, and
`src/NetDriver.cpp`.

### Clang 19.1.5

The VS-bundled LLVM Clang 19.1.5 targets `x86_64-pc-windows-msvc` and must use
the VS 2022 MSVC STL (`14.44.35207`); the VS 18 Insiders STL on this host
demands Clang 20+ and would reject any C++ TU including `<cstdint>` with
`error STL1000`, independent of source. The compile uses `-nostdinc++` plus the
VS 2022 STL include path:

```powershell
$Clang = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang++.exe'
$MsvcStl = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\include'
& $Clang `
    -std=c++17 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti `
    -nostdinc++ -isystem $MsvcStl `
    -I lib/microworld/include `
    -I lib/microworld-memory/include `
    -I lib/microworld-net/include `
    -c <probe-including-one-net-header>.cpp `
    -o <object>
```

Result: exit 0, no warnings, for every Net public header (including
`NetPacketStorage.h`) and `src/NetDriver.cpp`.

## Static checks

```powershell
python lib/microworld/tools/CheckDependencyBoundaries.py --self-test
python lib/microworld/tools/CheckDependencyBoundaries.py `
    --package Core=lib/microworld `
    --package Memory=lib/microworld-memory `
    --package Object=lib/microworld-object `
    --package Engine=lib/microworld-engine `
    --package Net=lib/microworld-net
python lib/microworld/tools/CheckProfileMap.py --self-test
python lib/microworld/tools/CheckProfileMap.py `
    --map $ConsumerBuild\microworld_net_profile.map `
    --profile Core+Net `
    --forbid fobjectstore --forbid uworld `
    --forbid fbytearchive --forbid fnetenginesubsystem
python lib/microworld/tools/CheckClassDocumentation.py --root lib/microworld-net
python lib/microworld/tools/CheckFolderAgents.py --root lib/microworld-net
```

Results:

- Dependency-boundary self-test passed.
- Dependency-boundary scan passed (5 packages, 50 files).
- Profile-map self-test passed.
- `Core+Net` profile map check passed (38560 bytes); Object, Engine,
  Serialization, and Integration markers are forbidden.
- Class-documentation check passed (15 files).
- Folder-AGENTS check passed (8 guides).

## What this evidence does not establish

- No target upload, runtime timing, stack high-water mark, heap measurement,
  radio transmit, or physical-hardware behavior was performed or recorded.
- The host strict compile does not establish ESP32-S3 runtime margins; see
  [Esp32S3N16R8.md](Esp32S3N16R8.md). The ESP32-S3 link gate was **not**
  re-verified this session: the corrected-source re-build hit a transient
  GCC 15.2.0 internal compiler error in ESP-IDF vendor code
  (`esp_lcd_panel_rgb.c`), unrelated to MicroWorld. That evidence file
  documents the ICE and marks the prior-session RAM/Flash figures as not
  re-verified for the corrected source.
