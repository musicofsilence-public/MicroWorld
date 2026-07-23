# MicroWorld — ESP32 Examples Roadmap

**Version:** 1.0 · **Date:** 2026-07-23 · **Owner:** Mykola
**Baseline:** commit `050f466` (clean tree, simplicity roadmap closed).
**Target hardware:** 2 × ESP32-S3-DevKitC-1 (ESP32-S3-WROOM-1-N16R8), USB serial,
a shared 2.4 GHz WiFi network, and 2 × E32 LoRa UART modules (final example only).
**Toolchain:** Windows host, PlatformIO Core ≥ 6.1.19, Espressif32 platform 7.x,
ESP-IDF 6.x — the exact stack recorded in
`Modules/PlatformEsp32/benchmarks/Results/Esp32S3N16R8.md`.

MicroWorld now needs **worked examples**: one small standalone project per
engine feature, each runnable on a real ESP32-S3, each readable by a student in
one sitting. This document is the active plan and progress tracker for building
them. Every example lives under a new top-level `examples/` folder, composes
MicroWorld concepts with the verified ESP32 platform adapters, and demonstrates
**exactly one feature** — the progression climbs the dependency ladder
(`Core → Memory → Object → Engine → Net`) and ends with two boards talking to
each other over WiFi UDP and over E32 LoRa.

It is written so that any LLM (including a weak one) can pick it up, find the
next task, complete it, and record progress without extra context. The
companion documents are:

- `docs/SIMPLICITY_ROADMAP.md` — the completed simplicity plan. Its protocol
  (section 1) is the model for this document's protocol.
- `PROGRESS.md` — the live evidence record. Add one short line per finished
  phase (see protocol rule 8).
- `docs/Porting.md` — the three adapter seams every example composes.

---

## 1. How to use this document (protocol for LLM workers)

Follow these rules exactly:

1. Read sections **2 (Ground rules)** and **3 (Common scaffold)** before
   touching any code.
2. Open section **5 (Progress tracker)**. Find the first phase whose status is
   not ✅. Inside that phase, find the first task whose **Built** box is
   unchecked.
3. Work on **exactly one task at a time**, in order. Do not start a later phase
   while an earlier phase has unchecked **Built** boxes. (**Hardware-verified**
   boxes may lag behind — see rule 5.)
4. Every task has a **Feature**, **MicroWorld APIs**, **Reference code**,
   **Program behavior**, **Steps**, **Done when**, and **Verify** block. A
   task's **Built** box may be checked only when every "Done when" item is true
   and the Build Verify (§1.1) passes.
5. Each example has **two** status boxes: `[ ] Built` (checkable by any
   worker after a clean compile) and `[ ] Hardware-verified` (checkable only
   after the human-gated hardware checkpoint of §1.2). Hardware checkpoints may
   be batched — the human may flash several finished examples in one sitting —
   so an unchecked Hardware box never blocks the next task's build work.
6. When a box is checked: append one evidence line directly under the task
   (`Built YYYY-MM-DD — <one sentence of proof>` or
   `Hardware-verified YYYY-MM-DD — <one sentence of proof>`), and update the
   phase status in the tracker table (⬜ → 🟨 when a phase's first task starts,
   🟨 → ✅ when every Built box in the phase is checked).
7. If you are blocked, write `⛔ BLOCKED:` plus one sentence under the task and
   stop. Do not skip ahead. In particular: if an example exposes a defect in a
   `Modules/` package, you are blocked — report it; never patch the library
   from inside an example task.
8. When a phase reaches ✅: add one short evidence entry to `PROGRESS.md`
   (what was built, how it was verified).
9. Never delete or rewrite this document's structure. Only update statuses,
   checkboxes, evidence lines, and BLOCKED notes.

Status legend: ⬜ not started · 🟨 in progress · ✅ done · ⛔ blocked

### 1.1 Build Verify (run for every example task)

Run from the repository root, in this order:

```sh
clang-format --style=file:clang-format -i <every .h/.cpp file you touched>
pio run -d examples/<NN-Name>
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected:

- `pio run` ends with `[SUCCESS]` (exit 0). Record the `RAM:`/`Flash:` usage
  lines it prints — they go into the example's README (§3.6).
- The root `ctest` still passes with the same test count as before your change.
  This matters because `tools/CheckFormatting.py` (wired into ctest as
  `microworld_format_check`) checks **every git-tracked `.h`/`.cpp` in the
  repository** — the moment example sources are tracked, the repo-wide format
  gate covers them. An unformatted example breaks the whole build's gate.
- If `build/` is missing, create it first with `cmake -S . -B build`.

The first `pio run` of a fresh checkout downloads the Espressif toolchain and
takes several minutes; later runs are incremental.

### 1.2 Hardware checkpoint (human-gated — never self-serve)

Flashing and monitoring touch a physical board. Repository rule (see
`Modules/PlatformEsp32/AGENTS.md` and `docs/Porting.md`): **compile success is
never a runtime claim**, and a worker must never run `pio run -t upload` or
`pio device monitor` without explicit human authorization in the current
session.

Checkpoint procedure:

1. Announce that the example is ready for hardware verification and print the
   two commands the human (or the authorized worker) runs:

   ```sh
   pio run -d examples/<NN-Name> -t upload --upload-port <COM-port>
   pio device monitor -d examples/<NN-Name>
   ```

2. Compare the captured serial output against the task's **Program behavior**
   trace shape.
3. Paste the real captured trace into the example README's
   "Verified output" section, check the task's `Hardware-verified` box, and add
   the evidence line.

Until step 3 happens, the example's README must carry the sentence:
*"Status: compiled for ESP32-S3; not yet verified on hardware."*

### 1.3 How to locate code

Locate code by symbol, never by remembered line number: `rg -n "SymbolName"
Modules`. Every task lists **Reference code** — existing, verified files that
already exercise the same APIs (module tests, the consumer probes under
`Modules/Core/tests/consumer/src/`, and the two host examples). Read the
references before writing the example; copy their call sequences, not their
scale. `Modules/` is read-only for this roadmap (rule 7 above).

---

## 2. Ground rules (invariants — never violate)

### 2.1 Example code follows the engine's embedded style

C++17; no exceptions; no RTTI; no heap allocation through MicroWorld types
(the WiFi/ESP-IDF glue may allocate internally — that is the vendor SDK's
business, not the example's); fixed capacities visible at the call site;
caller-supplied time from `FEsp32TimeSource` — never a hidden clock read
inside example logic. Document every function and every persistent variable
with the *why*, exactly as `AGENTS.md` requires for module code (the
class-documentation checker is Modules-scoped, so for examples this is
convention-enforced — reviewers must check it). Format everything with the
tracked style file: `clang-format --style=file:clang-format -i <files>`.

### 2.2 Two hardware lessons that are already paid for (do not rediscover them)

Both were found the hard way on real hardware
(`Modules/PlatformEsp32/benchmarks/Results/Esp32S3N16R8.md`, Phase 6.2 notes):

1. **Static storage, not `app_main` stack.** The default main-task stack is
   3,584 bytes; a `TEngineHost`, arena, or net storage declared as a local
   overflows it. Every MicroWorld composition object in every example is
   declared `static` at file scope (or in a `static` function-local).
2. **Network stack init before sockets.** Any example that opens a socket must
   call `esp_netif_init()` and `esp_event_loop_create_default()` (and for
   WiFi, `nvs_flash_init()`) before constructing `FEsp32UdpDriver`.

### 2.3 One feature per example; serial trace is the observable

Each example demonstrates exactly one feature and prints a short trace to the
serial console — the same observation style the host examples use. Every line
an example prints starts with its tag `[ex<NN>]` so captures are unambiguous.
Examples without radio/network I/O must print a **deterministic** trace
(identical across runs except timing-derived numbers, which the task's trace
shape marks as approximate). Keep every example's `src/` under ~200 lines; if
it grows past that, the example is demonstrating more than one feature — split
or simplify.

### 2.4 Examples are standalone consumer projects

Each `examples/<NN-Name>/` is a self-contained PlatformIO project a student can
copy out of the repository. Duplication **across** examples (the platformio.ini
boilerplate, the WiFi glue) is deliberate and allowed; DRY applies **within**
one example only. Examples consume `Modules/` via `symlink://` exactly like the
verified consumer project; they never add sources to a module and never depend
on another example.

### 2.5 Naming

Folders are `NN-PlainName` (two-digit order prefix, PascalCase plain-language
name — e.g. `04-MemoryArena`). Types and files follow the repo's UE-style
conventions (`F`/`T`/`E` prefixes, PascalCase). No metaphor or jargon names
anywhere — a student must be able to read every identifier without a glossary.

### 2.6 Secrets never enter git

WiFi SSID/password and site-specific addresses live in
`src/NetworkConfig.h`, which is **git-ignored**; each networked example commits
a `src/NetworkConfig.example.h` template instead (§3.8). Never commit real
credentials, and never print a password to the serial console.

### 2.7 Files you must never edit

- Anything under `Modules/` (library is read-only for this roadmap; defects are
  `⛔ BLOCKED`, protocol rule 7).
- `docs/ROADMAP.md`, `docs/SIMPLICITY_ROADMAP.md` — frozen/completed plans.
- Existing entries in `CHANGELOG.md` (appending a new entry is allowed).
- `Modules/*/benchmarks/Results/*.md`, `LICENSE`, `VERSION`, anything under
  `build/` or `.git/`.

---

## 3. Common scaffold (referenced by every task as “the scaffold”)

Phase 0 creates the shared pieces; every example task starts by copying the
canonical layout and replacing the placeholders `<NN-Name>` and
`<ExampleTag>` (= `ex<NN>`).

### 3.1 Directory layout

```text
examples/
├── AGENTS.md                  folder guide (created in task 0.1)
├── README.md                  catalog + how-to-build (created in task 0.1)
├── esp32-common/              shared board profile (created in task 0.2)
│   ├── AGENTS.md
│   ├── partitions.csv
│   └── sdkconfig.defaults
└── <NN-Name>/                 one standalone PlatformIO project per example
    ├── AGENTS.md
    ├── README.md
    ├── CMakeLists.txt         ESP-IDF project entry (verbatim §3.4)
    ├── platformio.ini         verbatim §3.3
    └── src/
        ├── CMakeLists.txt     ESP-IDF main component (verbatim §3.5)
        └── Main.cpp           the example (plus helper .h/.cpp as needed)
```

### 3.2 `examples/esp32-common/` — shared board profile

Two files, copied **verbatim** from the board profile that produced every
verified ESP32-S3 build and the Phase 6.2 hardware measurements. Do not
"improve" them; they are proven.

`partitions.csv`:

```csv
# Name,    Type, SubType, Offset,   Size,      Flags
# Why: Keep device settings in a dedicated, wear-managed key-value partition.
nvs,       data, nvs,     0x9000,   0x10000,
# Why: OTA metadata records which firmware slot should boot after an update.
otadata,   data, ota,     0x19000,  0x2000,
# Why: Two equal 4 MiB slots allow a new firmware image to be tested while the
# previous working image remains available for recovery.
ota_0,     app,  ota_0,   0x20000,  0x400000,
ota_1,     app,  ota_1,   0x420000, 0x400000,
# Why: FATFS provides files and directories; mounting it through ESP-IDF's
# wear-levelling layer avoids repeatedly erasing the same physical sectors.
storage,   data, fat,     0x820000, 0x7A0000,
# Why: Retain a crash snapshot after reboot for offline debugging.
coredump,  data, coredump,0xFC0000, 0x40000,
```

`sdkconfig.defaults`:

```ini
# Why: Match the ESP32-S3-WROOM-1-N16R8 module's 16 MB Quad-SPI flash.
CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT=n
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASH_SAMPLE_MODE_STR=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# Why: Make the module's 8 MB Octal-SPI PSRAM available for larger allocations.
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_BOOT_INIT=y
CONFIG_SPIRAM_USE_MALLOC=y

# Why: Use the project layout that provides two OTA application slots and
# dedicated persistent-storage and crash-diagnostic areas.
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"

# Why: Preserve crash information across a reboot so failures can be diagnosed.
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y

# Why: A 4096-byte wear-levelling sector matches the flash erase sector and
# reduces flash operations; this board has enough RAM for the larger FAT buffers.
CONFIG_WL_SECTOR_SIZE_4096=y
```

### 3.3 Canonical `platformio.ini`

```ini
[platformio]
default_envs = esp32-s3

; Every example links the full MicroWorld stack through the same symlink://
; wiring the verified consumer project uses; the linker keeps only what the
; example calls, so unused layers cost flash-image nothing at runtime.
[microworld]
lib_deps =
    symlink://../../Modules/Core
    symlink://../../Modules/Memory
    symlink://../../Modules/Object
    symlink://../../Modules/Engine
    symlink://../../Modules/Net
    symlink://../../Modules/PlatformEsp32
lib_ldf_mode = chain+
build_flags =
    -std=gnu++17

[env:esp32-s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf
monitor_speed = 115200
lib_deps = ${microworld.lib_deps}
lib_ldf_mode = ${microworld.lib_ldf_mode}
build_flags = ${microworld.build_flags}
board_build.partitions = ../esp32-common/partitions.csv
board_build.flash_mode = qio
board_build.f_flash = 80000000L
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
board_build.cmake_extra_args =
    -DSDKCONFIG_DEFAULTS="../esp32-common/sdkconfig.defaults"
```

Examples 16 and 17 replace the single `[env:esp32-s3]` with two role
environments (their tasks show the exact diff). **Do not use
`build_src_filter` for role selection** — ESP-IDF ignores it (proven in
`Modules/Core/tests/consumer/src/CMakeLists.txt`); roles are selected with a
`-D` compile definition instead.

### 3.4 Canonical project `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(microworld_example)
```

### 3.5 Canonical `src/CMakeLists.txt`

```cmake
idf_component_register(SRCS "Main.cpp")

# Why: keep the engine's strict portable contract on example code. ESP-IDF's
# esp_libc headers use the GCC #include_next extension (pedantic warning) and
# GCC 15's libstdc++ deprecates std::aligned_storage used by C++17 MicroWorld
# headers, so exactly those two diagnostics are downgraded — the same scoped
# relaxations the verified consumer project uses.
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
        -std=gnu++17
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-error=pedantic
        -Wno-deprecated-declarations
        -fno-exceptions
        -fno-rtti
)
```

List every additional example source in `SRCS`. Examples that use WiFi
(15, 16) extend the first line with the vendor components they include:

```cmake
idf_component_register(
    SRCS "Main.cpp" "WifiStation.cpp"
    PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event
)
```

### 3.6 Canonical `src/Main.cpp` skeleton

```cpp
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>

#include <cstdio>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace
{
/** Single real-time source; every MicroWorld deadline in this example reads it. */
MicroWorld::FEsp32TimeSource GTimeSource{};
} // namespace

/** Composition root: builds the example's fixed storage and drives one bounded run. */
extern "C" void app_main(void)
{
	std::printf("[<ExampleTag>] start\n");
	// ... example body: static MicroWorld objects + a bounded loop that
	// calls GTimeSource.Now() and paces with vTaskDelay(pdMS_TO_TICKS(10))
	// so the idle task (and its watchdog) always runs ...
	std::printf("[<ExampleTag>] done\n");
}
```

Two non-negotiables baked into the skeleton: MicroWorld objects are `static`
(lesson §2.2-1), and every polling loop contains a `vTaskDelay` so the FreeRTOS
idle task is never starved.

### 3.7 Per-example `README.md` template (required sections, in order)

```markdown
# <NN-Name>

**Feature:** <one sentence — the single MicroWorld feature shown.>

## What it does            <numbered runtime behavior, 3–8 steps>
## MicroWorld APIs used    <bullet list of exact symbols>
## Hardware required       <board/USB only, or extras>
## Build                   pio run -d examples/<NN-Name>
## Flash and observe       upload + monitor commands from §1.2
## Expected output         <the trace shape from the task>
## Verified output         <real captured trace, or the §1.2 not-yet line>
## Image size              <RAM/Flash lines from the build>
```

Each example also gets a short `AGENTS.md` (architecture: one composition
root; concepts: the invariant the example makes observable; verification: the
Build Verify + checkpoint commands), mirroring
`Modules/Engine/examples/HostLifecycle/AGENTS.md` in tone and size.

### 3.8 WiFi scaffolding (examples 15 and 16 only)

Committed template `src/NetworkConfig.example.h`:

```cpp
#pragma once

/** WiFi network both boards join; copy this file to NetworkConfig.h and fill in. */
constexpr const char* kWifiSsid = "YOUR_SSID";

/** WiFi password; NetworkConfig.h is git-ignored so the real value never lands in git. */
constexpr const char* kWifiPassword = "YOUR_PASSWORD";

/** UDP port the server binds (example 15: the echo port; example 16: the game port). */
constexpr std::uint16_t kServerPort = 40404;

/** Server board's IPv4 octets — used by example 16's client build only.
 * Read them from the server board's "[ex16] server ip=..." boot line. */
constexpr std::uint8_t kServerIpv4[4] = {192, 168, 1, 50};
```

`src/WifiStation.h/.cpp` — one blocking helper with this exact contract:

```cpp
/** Joins the configured WiFi as a station and blocks until an IPv4 address is
 * bound or ~15 s elapse. Returns true only when the interface has an address;
 * prints "[<ExampleTag>] wifi ip=<a.b.c.d>" on success. Must be called once,
 * before any FEsp32UdpDriver is constructed. */
bool ConnectWifiStation(const char* ExampleTag) noexcept;
```

Implementation sequence (standard ESP-IDF station bring-up):
`nvs_flash_init` (erase+retry on `ESP_ERR_NVS_NO_FREE_PAGES`/`NEW_VERSION`) →
`esp_netif_init` → `esp_event_loop_create_default` →
`esp_netif_create_default_wifi_sta` → `esp_wifi_init` → register
`WIFI_EVENT`/`IP_EVENT` handlers → `esp_wifi_set_mode(WIFI_MODE_STA)` →
`esp_wifi_set_config` with `kWifiSsid`/`kWifiPassword` → `esp_wifi_start` →
wait on an event group for `IP_EVENT_STA_GOT_IP` with a 15 s timeout. This is
vendor glue, not MicroWorld — keep it in its own file so `Main.cpp` stays
about the feature.

### 3.9 `.gitignore` additions (task 0.1)

```gitignore
# PlatformIO build output inside example projects
**/.pio/
# ESP-IDF sdkconfig files PlatformIO generates per environment
examples/*/sdkconfig.*
# Per-site WiFi credentials (committed template: NetworkConfig.example.h)
examples/*/src/NetworkConfig.h
```

---

## 4. Example catalog

| # | Example | The one feature | Extra hardware |
| --- | --- | --- | --- |
| 01 | `01-CoreTick` | `FTickFunction` cadence from caller-supplied real time | — |
| 02 | `02-CoreLifecycle` | forward-only lifecycle: `FApplication` + `FLifecycleGuard` | — |
| 03 | `03-CoreLog` | `FLogSink` seam: `MW_LOG` through `Esp32LogSink` | — |
| 04 | `04-MemoryArena` | `TFixedArena` / `IMemoryResource` explicit allocation | — |
| 05 | `05-MemorySmartPointers` | `TUniquePtr` / `TSharedPtr` / `TWeakPtr` ownership | — |
| 06 | `06-MemoryContainers` | `TStaticVector` + `TSpan` bounded storage and views | — |
| 07 | `07-MemoryDelegates` | `TDelegate` + `TMulticastDelegate` fixed dispatch | — |
| 08 | `08-ObjectStore` | managed identity: store, handles, object pointers | — |
| 09 | `09-ObjectGarbageCollector` | rooted tracing + budgeted incremental collection | — |
| 10 | `10-EngineWorld` | `UWorld` / `AActor` / `UActorComponent` via `TEngineHost` | — |
| 11 | `11-EngineTimers` | `TTimerManager` one-shot / looping / cancel | — |
| 12 | `12-NetBytes` | `FByteWriter` / `FByteReader` transactional byte I/O | — |
| 13 | `13-NetLoopback` | `TNetManager` FIFO over `THostLoopback` | — |
| 14 | `14-NetFrameCodec` | `EncodeFrame` / `TFrameDecoder` CRC framing + resync | — |
| 15 | `15-UdpEcho` | `FEsp32UdpDriver` over real WiFi | PC on same WiFi |
| 16 | `16-TwoBoardUdp` | `TNetHost` client/server + `TNetHostFrame` engine binding | 2nd board, WiFi |
| 17 | `17-TwoBoardLora` | `FEsp32E32LoraDriver` framed radio link | 2nd board, 2 × E32 |

---

## 5. Progress tracker

| Phase | Content | Status |
| --- | --- | --- |
| 0 | Scaffold: `examples/` folder, board profile, gitignore | ✅ |
| 1 | Core on the board (01–03) | 🟨 |
| 2 | Memory (04–07) | ⬜ |
| 3 | Object (08–09) | ⬜ |
| 4 | Engine (10–11) | ⬜ |
| 5 | Net without radio (12–14) | ⬜ |
| 6 | WiFi UDP, one board (15) | ⬜ |
| 7 | Two boards (16–17) | ⬜ |

---

## 6. Phases and tasks

### Phase 0 — Scaffold

#### Task 0.1 — Create `examples/` folder, catalog, and gitignore entries

- [x] Done

Done 2026-07-23 — `examples/AGENTS.md` + `examples/README.md` (catalog 01–17 with status column, build/flash how-to, hardware list) created, §3.9 gitignore block appended, root `README.md` points at the examples plan, `PROGRESS.md` records the baseline; superbuild green with `ctest` 11/11 (unchanged count).

**Steps:**

1. Create `examples/AGENTS.md`: architecture (one standalone PlatformIO
   consumer project per engine feature, dependencies point inward — examples
   consume `Modules/`, never the reverse), concepts (one feature per example,
   `[ex<NN>]` serial-trace observable, static-storage rule §2.2), verification
   (Build Verify §1.1, human-gated hardware checkpoint §1.2). Follow the tone
   of `Modules/Core/examples/AGENTS.md`.
2. Create `examples/README.md`: the catalog table from section 4 with a status
   column, the one-paragraph build/flash how-to (§1.1–§1.2 commands), and the
   hardware shopping list from the header of this document.
3. Append the §3.9 block to `.gitignore`.
4. Add one line to the root `README.md` docs paragraph pointing to
   `docs/EXAMPLES_ROADMAP.md` as the active examples plan, and one line to
   `PROGRESS.md` recording that the examples plan started (records the current
   root ctest test count as this roadmap's baseline).

**Done when:** all four files exist/are updated; `ctest` still passes with the
same count (nothing tracked yet besides markdown and gitignore).

**Verify:** `cmake --build build --config Release` then
`ctest --test-dir build -C Release --output-on-failure`.

#### Task 0.2 — Create `examples/esp32-common/` board profile

- [x] Done

Done 2026-07-23 — `examples/esp32-common/partitions.csv` and `sdkconfig.defaults` copied byte-for-byte from §3.2, plus an `AGENTS.md` recording their provenance (the profile behind every verified ESP32-S3 compile and the Phase 6.2 measurements; examples reference it, never fork it); `ctest` unchanged at 11/11.

**Steps:**

1. Create `examples/esp32-common/partitions.csv` and
   `examples/esp32-common/sdkconfig.defaults` **byte-for-byte** from §3.2.
2. Create `examples/esp32-common/AGENTS.md` stating the provenance: this is
   the board profile that produced every verified ESP32-S3 compile and the
   Phase 6.2 hardware measurements
   (`Modules/PlatformEsp32/benchmarks/Results/Esp32S3N16R8.md`); examples must
   reference it, never fork it.

**Done when:** both files match §3.2 exactly; AGENTS.md explains provenance.

**Verify:** visual diff against §3.2; `ctest` unchanged.

---

### Phase 1 — Core on the board

#### Task 1.1 — `01-CoreTick`

- [x] Built
- [ ] Hardware-verified

Built 2026-07-23 — `examples/01-CoreTick` implements the seven-line tick trace over `FTickFunction`/`FEsp32TimeSource`/`FVersion`; `pio run` `[SUCCESS]` (RAM 20,220 B / 6.2%, Flash 193,281 B / 4.6%), example code warning-clean (only ESP-IDF's downgraded `#include_next` pedantic warnings), root `ctest` 11/11 unchanged. Hardware-verified pending the human-gated §1.2 checkpoint.

**Feature:** bounded per-object tick scheduling (`FTickFunction`) driven by
caller-supplied real time — the engine's "no hidden clock" contract, made
visible with a real clock.

**MicroWorld APIs:** `FTickConfiguration::EnabledEvery`, `FTickFunction`
(`BeginPlay` / `Advance` / `EndPlay`), `FTickDecision`, `TimePointMilliseconds`,
`FEsp32TimeSource::Now`, `FVersion` (print once at boot).

**Reference code:** `Modules/Core/include/MicroWorld/TickFunction.h` doc
comments; `Modules/Core/tests/` tick behavior tests;
`Modules/Core/tests/consumer/src/CoreConsumerProbe.h`.

**Program behavior:**

1. Print the MicroWorld version: `[ex01] microworld <major>.<minor>.<patch>`.
2. Construct a static `FTickFunction` with a 500 ms interval;
   `BeginPlay(GTimeSource.Now())`.
3. Loop (with 10 ms `vTaskDelay` pacing): call `Advance(GTimeSource.Now())`;
   each time the decision reports a tick, print
   `[ex01] tick n=<count> delta=<ms>` — `delta` comes from the decision, not
   from your own subtraction.
4. After 5 ticks: `EndPlay()`, print `[ex01] done ticks=5`, return.

Trace shape: exactly 7 lines; `n` runs 1–5; `delta` ≈ 500 (500–520 typical —
real clock, so approximate is expected).

**Steps:** copy the scaffold (§3.1–§3.6) into `examples/01-CoreTick`; implement
`Main.cpp`; add the catalog row status in `examples/README.md`; Build Verify.

**Done when:** every listed API appears in the source; trace shape documented
in README's Expected output; Build Verify passes; README + AGENTS.md complete
per §3.7.

**Verify:** §1.1 now; §1.2 checkpoint when the human is ready (expect the
7-line trace).

#### Task 1.2 — `02-CoreLifecycle`

- [ ] Built
- [ ] Hardware-verified

**Feature:** the forward-only begin/advance/end lifecycle with typed results —
wrong-order calls are rejected values, not crashes or exceptions.

**MicroWorld APIs:** `FApplication` (subclass overriding `OnBeginPlay`,
`OnBeginPlayFailed`, `OnAdvance`, `OnEndPlay`), `ERuntimeResult`,
`FLifecycleGuard` (`Begin` / `RequirePlaying` / `End` / `GetState`),
`ELifecycleState`.

**Reference code:** `Modules/Core/include/MicroWorld/Application.h` and
`Lifecycle.h` doc comments; `Modules/Core/tests/` lifecycle tests;
`Modules/Core/examples/HostLifecycle/`.

**Program behavior:**

1. Part A — bare guard: `Begin` (print result), `Begin` again (print the
   rejection), `RequirePlaying` (print), `End` (print), `End` again (print the
   rejection). A tiny local `const char* ToText(ERuntimeResult)` helper makes
   results readable; document it as an observation aid.
2. Part B — an `FExampleApplication` subclass whose hooks each print one line;
   drive `BeginPlay` → `Advance` ×3 → `EndPlay` with real time, then call
   `Advance` once more and print its rejection.

Trace shape: fixed line count, byte-identical across runs (no timing values
printed) — this example must be fully deterministic.

**Steps / Done when / Verify:** as task 1.1, tag `[ex02]`.

#### Task 1.3 — `03-CoreLog`

- [ ] Built
- [ ] Hardware-verified

**Feature:** the log-sink seam — portable `MW_LOG` calls surface through the
platform's `ESP_LOG*` backend only after a sink is installed.

**MicroWorld APIs:** `FLogSink`, the sink-install function in
`MicroWorld/Log.h` (locate with `rg -n "SetLogSink" Modules/Core`),
`MW_LOG` / `MW_LOG_MSG`, `ELogLevel`, the ESP32 sink symbol in
`MicroWorld/PlatformEsp32/Esp32LogSink.h`.

**Reference code:** `Modules/Core/include/MicroWorld/Log.h`;
`Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32LogSink.h` and
`src/Esp32LogSink.cpp`.

**Program behavior:**

1. Before installing any sink, emit one `MW_LOG_MSG` and then print
   `[ex03] pre-install message produced no output (default sink is null)`.
2. Install the ESP32 sink; emit one message per `ELogLevel` value (formatted
   variant included) under category `"ex03"`.
3. Print `[ex03] done`.

Trace shape: the `[ex03]` frame lines plus one ESP-IDF-formatted log line per
level (color codes/timestamps come from `ESP_LOG` — nondeterministic prefix,
deterministic message text).

**Steps / Done when / Verify:** as task 1.1, tag `[ex03]`.

---

### Phase 2 — Memory

#### Task 2.1 — `04-MemoryArena`

- [ ] Built
- [ ] Hardware-verified

**Feature:** explicit allocation with observable failure — a fixed caller-owned
arena that returns typed results instead of throwing or falling back.

**MicroWorld APIs:** `IMemoryResource`, `TFixedArena<Bytes, Alignment>`,
`FMemoryBlock`, `EMemoryResult`, the arena's capacity/usage observers (locate
the exact names in `MemoryResource.h` / `FixedArena.h`).

**Reference code:** `Modules/Memory/include/MicroWorld/Memory/FixedArena.h`
doc comments; `Modules/Memory/tests/` arena behavior tests;
`Modules/Core/tests/consumer/src/MemoryConsumerProbe.h`.

**Program behavior:**

1. Static `TFixedArena<256, alignof(std::max_align_t)>`.
2. Allocate 64-byte blocks until the arena reports exhaustion; print one line
   per attempt: `[ex04] alloc n=<i> result=<text> used=<bytes>`.
3. Deallocate one block, show usage falling, allocate again to prove reuse.
4. Attempt one malformed deallocation (tampered size) and print the typed
   rejection — nothing crashes.
5. `[ex04] done`.

Trace shape: fully deterministic.

**Steps / Done when / Verify:** as task 1.1, tag `[ex04]`.

#### Task 2.2 — `05-MemorySmartPointers`

- [ ] Built
- [ ] Hardware-verified

**Feature:** explicit smart-pointer ownership over a caller-owned resource —
deterministic destruction, shared counts, and weak expiry without a heap.

**MicroWorld APIs:** `MakeUnique` / `TUniquePtr`, `MakeShared` / `TSharedPtr` /
`TWeakPtr`, `ESharedPointerResult`, with a `TFixedArena` as the
`IMemoryResource`.

**Reference code:** `Modules/Memory/include/MicroWorld/Memory/UniquePtr.h` and
`SharedPtr.h`; `Modules/Memory/tests/` pointer tests;
`docs/decisions/0002a-smart-pointer-foundation.md`.

**Program behavior:**

1. A small `FProbe` type (noexcept ctor/dtor) prints `[ex05] probe +<id>` /
   `[ex05] probe -<id>` so lifetimes are visible.
2. Unique: `MakeUnique<FProbe>` in an inner scope — destruction prints at scope
   exit, arena usage returns to its pre-allocation value (print both).
3. Shared: `MakeShared<FProbe>`, copy the pointer (print use count), reset one
   copy (count drops), reset the last (probe destructor prints).
4. Weak: take a `TWeakPtr` from a live shared pointer, lock it (success),
   reset the shared pointer, lock again (expired — print the typed result).
5. `[ex05] done`.

Trace shape: fully deterministic.

**Steps / Done when / Verify:** as task 1.1, tag `[ex05]`.

#### Task 2.3 — `06-MemoryContainers`

- [ ] Built
- [ ] Hardware-verified

**Feature:** bounded containers — compile-time capacity with typed saturation,
and non-owning views over caller-kept storage.

**MicroWorld APIs:** `TStaticVector` (append/iterate/saturation via
`ERuntimeResult`), `TSpan` (view over the vector's elements; empty view with
null data is valid).

**Reference code:** `Modules/Memory/include/MicroWorld/Containers/StaticVector.h`
and `Span.h`; `Modules/Memory/tests/` container tests.

**Program behavior:**

1. `TStaticVector<std::uint32_t, 4>`: append 4 values (print each result),
   append a 5th (print the saturation result and that size is still 4).
2. Iterate and print elements — order is index order.
3. Build a `TSpan<const std::uint32_t>` over the elements, pass it to a helper
   that sums it, print the sum.
4. Show an empty span (`{nullptr, 0}`) is observable without dereferencing:
   print its size.
5. `[ex06] done`.

Trace shape: fully deterministic.

**Steps / Done when / Verify:** as task 1.1, tag `[ex06]`.

#### Task 2.4 — `07-MemoryDelegates`

- [ ] Built
- [ ] Hardware-verified

**Feature:** allocation-free callable dispatch — inline-stored delegates and
fixed-capacity multicast with generation-checked handles.

**MicroWorld APIs:** `TDelegate<void(...), InlineCallableBytes>`,
`TMulticastDelegate<void(...), MaxBindings, InlineCallableBytes>`
(add/remove/`Broadcast`), `FDelegateHandle`, `EDelegateResult`.

**Reference code:** `Modules/Memory/include/MicroWorld/Delegates/Delegate.h`
doc comments; `Modules/Memory/tests/` delegate tests.

**Program behavior:**

1. Single delegate: bind a lambda that prints its argument; invoke it twice.
2. Multicast: add three bindings labeled a/b/c; `Broadcast(1)` — insertion
   order prints a, b, c.
3. Remove b via its handle; `Broadcast(2)` — prints a, c.
4. Remove b again with the stale handle; print the typed rejection.
5. Fill the multicast to `MaxBindings`; print the typed saturation on the next
   add. `[ex07] done`.

Trace shape: fully deterministic.

**Steps / Done when / Verify:** as task 1.1, tag `[ex07]`.

---

### Phase 3 — Object

#### Task 3.1 — `08-ObjectStore`

- [ ] Built
- [ ] Hardware-verified

**Feature:** managed identity — stable `{slot, generation}` handles over
caller-owned storage, with strong/weak object pointers that expire safely.

**MicroWorld APIs:** `FClassDescriptor`, `TClassRegistry`, `FObjectStore` +
`FObjectStoreStorage` (caller-owned slots/metadata/roots), `CreateObject`,
`FObjectHandle`, `EObjectResult`, `TObjectPtr`, `TWeakObjectPtr`,
`TStrongObjectPtr`, `MarkPendingDestroy`.

**Reference code:** `Modules/Core/tests/consumer/src/ObjectConsumerProbe.h`
(mirror its composition order — registry and storage outlive the store);
`Modules/Object/include/MicroWorld/Object/*.h` doc comments;
`Modules/Object/tests/`.

**Program behavior:**

1. Compose (all static, declared in reverse destruction order per
   `Modules/Object/AGENTS.md`): class registry with one user class, slot/root
   storage, store.
2. Create one object; print its handle's slot and generation.
3. Dereference through `TObjectPtr` (print a field), take a `TWeakObjectPtr`
   (lock succeeds — print).
4. Destroy the object (mark + reclaim per the store's documented sequence);
   lock the weak pointer again — expired, print the typed result.
5. Create a new object; print that the slot's generation advanced, proving the
   old handle can never resurrect. `[ex08] done`.

Trace shape: fully deterministic.

**Steps / Done when / Verify:** as task 1.1, tag `[ex08]`.

#### Task 3.2 — `09-ObjectGarbageCollector`

- [ ] Built
- [ ] Hardware-verified

**Feature:** bounded incremental garbage collection — explicit roots, traced
references, and a per-slice budget the caller controls.

**MicroWorld APIs:** `FGarbageCollector` + `FGarbageCollectorStorage`,
`FGarbageCollectionBudget`, `RequestCollection`, the budgeted slice-advance
call (locate in `GarbageCollector.h`), `CollectFull`,
`FGarbageCollectionResult` / `FGarbageCollectionStats`,
`EGarbageCollectionPhase`, store roots (`AddRoot` / `RemoveRoot`).

**Reference code:** `Modules/Core/tests/consumer/src/ObjectConsumerProbe.h`;
`Modules/Object/tests/` collector tests;
`Modules/Core/tests/consumer/src/Esp32BenchmarkMain.cpp` (GC slice probe with
budget `{root=1, mark=1, sweep=8}`).

**Program behavior:**

1. Reuse the 08 composition plus a collector with a small budget
   (`{root=1, mark=1, sweep=8}` — the hardware-measured configuration).
2. Build a rooted object that references a child (traced), plus two unrooted
   garbage objects.
3. `RequestCollection`, then advance slice by slice; print one line per slice
   with the phase reported by the result.
4. After the cycle: print stats — rooted parent and traced child survive,
   the two unrooted objects were reclaimed; a weak pointer to reclaimed
   garbage is expired.
5. Create fresh garbage and show `CollectFull` as the one-call alternative.
   `[ex09] done`.

Trace shape: deterministic (slice count is fixed by the budget and object
count).

**Steps / Done when / Verify:** as task 1.1, tag `[ex09]`.

---

### Phase 4 — Engine

#### Task 4.1 — `10-EngineWorld`

- [ ] Built
- [ ] Hardware-verified

**Feature:** the managed Actor model — `UWorld` / `AActor` / `UActorComponent`
lifecycle and dispatch order, driven by one `TEngineHost` on real time.

**MicroWorld APIs:** `TEngineHost` (`RegisterClass`, `CreateWorld`,
`CreateObject`, `BeginPlay`, `Tick`, `EndPlay`), `UWorld::SpawnActor`,
`AActor::RegisterComponent`, `UActorComponent` hook overrides,
`bStartWithTickEnabled`, `TInlineActor` / `TInlineWorld` (if the host's
world/actor creation path uses them — mirror the reference),
`FGarbageCollectionBudget`, `EEngineResult` / `EObjectResult`.

**Reference code:** `Modules/Engine/examples/HostLifecycle/Main.cpp` — this
task is that example ported to ESP32 real time;
`Modules/Core/tests/consumer/src/EngineConsumerProbe.h`.

**Program behavior:** the host example's contract on hardware:

1. One tick-disabled device Actor owning one 100 ms-cadence sensor Component,
   inside a static `TEngineHost`.
2. `BeginPlay` prints component-before-actor begin lines; drive
   `Tick(GTimeSource.Now())` for ~500 ms so the component ticks 5 times
   (print `[ex10] sensor tick n=<i>`); `EndPlay` prints reverse-order end
   lines.
3. Print that the actor itself never ticked (its counter is 0) while the
   component ticked 5 times — actor and component schedules are independent.
   `[ex10] done`.

Trace shape: fixed line count; tick timing approximate, order exact.

**Steps / Done when / Verify:** as task 1.1, tag `[ex10]`.

#### Task 4.2 — `11-EngineTimers`

- [ ] Built
- [ ] Hardware-verified

**Feature:** the bounded caller-time timer facility — one-shot and looping
schedules with generation-checked handles, independent of any world.

**MicroWorld APIs:** `TTimerManager<MaxTimers, InlineTimerCallbackBytes>`
(`Schedule` / `Advance` / `Cancel`), `ETimerMode` (`OneShot`, `Looping`, and
the transactional rejection of any other value), `ETimerResult`,
`FTimerHandle`.

**Reference code:** `Modules/Engine/include/MicroWorld/Engine/Timer.h` doc
comments; `Modules/Engine/tests/` timer tests;
`Modules/Core/tests/consumer/src/EngineConsumerProbe.h`.

**Program behavior:**

1. Static `TTimerManager<8, 64>` seeded with `GTimeSource.Now()`.
2. Schedule a 250 ms looping timer (callback prints `[ex11] loop n=<i>`) and a
   1000 ms one-shot (callback prints `[ex11] oneshot fired`).
3. Drive `Advance(GTimeSource.Now())` in the paced loop. After the one-shot
   fires (~4 loop prints), `Cancel` the looping timer from the main loop and
   print the success result.
4. `Cancel` the one-shot's now-stale handle — print the typed rejection.
5. `Schedule` with an invalid `ETimerMode` raw value — print the transactional
   rejection. `[ex11] done`.

Trace shape: loop count may be 3–5 (real clock); every result line exact.

**Steps / Done when / Verify:** as task 1.1, tag `[ex11]`.

---

### Phase 5 — Net without radio

#### Task 5.1 — `12-NetBytes`

- [ ] Built
- [ ] Hardware-verified

**Feature:** transactional byte I/O — writes and reads that either fully
succeed or leave every cursor and output untouched.

**MicroWorld APIs:** `FByteWriter` (`WriteByte` / `Write`), `FByteReader`
(`ReadByte` / `Read` / `PeekByte`), `ENetResult`, `TSpan<std::uint8_t>` /
`TSpan<const std::uint8_t>`.

**Reference code:** `Modules/Net/include/MicroWorld/Net/ByteWriter.h` and
`ByteReader.h`; `Modules/Net/tests/` byte tests;
`Modules/Core/tests/consumer/src/NetConsumerProbe.h`.

**Program behavior:**

1. Writer over a 16-byte buffer: write a byte and a 4-byte span (print results
   and written count); attempt a 20-byte write — `Full`, written count
   unchanged (print both).
2. Reader over the written prefix: peek, read back the values, then request
   more bytes than remain — `Invalid`, outputs and cursor unchanged (print).
3. Bind a writer to `{nullptr, 8}` — every operation `Invalid` without
   crashing (print one). `[ex12] done`.

Trace shape: fully deterministic.

**Steps / Done when / Verify:** as task 1.1, tag `[ex12]`.

#### Task 5.2 — `13-NetLoopback`

- [ ] Built
- [ ] Hardware-verified

**Feature:** the manager/driver split — `TNetManager`'s deterministic outbound
FIFO and transactional receive over the in-memory `THostLoopback` driver (no
radio, no WiFi — the whole network runs inside one chip).

**MicroWorld APIs:** `TNetManager<MaxPackets, MaxPacketBytes>` (`QueueSend` /
`AdvanceSend` / `Receive`), `TNetPacketStorage`, `THostLoopback` and its port
drivers, `INetDriver`, `FNetAddress`, `FNetReceiveResult`, `ENetResult`.

**Reference code:** `Modules/Net/include/MicroWorld/Net/HostLoopback.h` and
`NetManager.h` doc comments; `Modules/Net/tests/`;
`Modules/Core/tests/consumer/src/NetConsumerProbe.h`.

**Program behavior:**

1. Static loopback with two ports A and B; a manager (storage + driver port A).
2. Queue two packets to B, print each result; `AdvanceSend` twice; receive
   both at B — FIFO order proven by payload contents.
3. Queue until `Full` (print), receive on empty — `Unavailable` (print),
   receive into a too-small destination — `Full` and the head packet is
   retained, then received intact into a big-enough buffer (print).
   `[ex13] done`.

Trace shape: fully deterministic.

**Steps / Done when / Verify:** as task 1.1, tag `[ex13]`.

#### Task 5.3 — `14-NetFrameCodec`

- [ ] Built
- [ ] Hardware-verified

**Feature:** the portable wire framing LoRa rides on — magic, source node id,
big-endian length, CRC-16/CCITT-FALSE, and a decoder that resynchronizes after
corruption.

**MicroWorld APIs:** `EncodeFrame`, `TFrameDecoder<MaxPayloadBytes>` (byte-pump
API per its header), `ENetResult`.

**Reference code:** `Modules/Net/include/MicroWorld/Net/FrameCodec.h` doc
comments; `Modules/Net/tests/` codec tests.

**Program behavior:**

1. Encode payload `"hello"` from node id 7; print the frame as hex.
2. Pump the frame into a decoder byte by byte; on completion print payload and
   source node id (must match).
3. Corrupt one CRC byte of a second frame, pump it — decoder rejects (print),
   then pump a clean third frame and show it decodes — resync proven.
4. Pump a frame whose length field exceeds the decoder capacity — rejected,
   then a clean frame again decodes. `[ex14] done`.

Trace shape: fully deterministic.

**Steps / Done when / Verify:** as task 1.1, tag `[ex14]`.

---

### Phase 6 — WiFi UDP, one board

#### Task 6.1 — `15-UdpEcho`

- [x] Built
- [x] Hardware-verified

Built 2026-07-23 — `pio run` `[SUCCESS]` both role envs (echo/probe). Design deviates from
the original single-board-PC-echo spec below: reworked to a **two-board SoftAP** demo (one
board hosts the AP and echoes, a second joins and probes) so no router and no real
credentials are needed. See `examples/15-UdpEcho/README.md`.

Hardware-verified 2026-07-23 — two ESP32-S3 boards, no router. Echo board console showed the
SoftAP up (`wifi ip=192.168.4.1`), the probe joining, a normal 16-byte round trip
(`rx bytes=16` → `echo result=0`), and the oversize probe arriving as `rx bytes=1200`
Success. Finding: `MSG_TRUNC` is not exposed on this ESP-IDF 6.0.1/lwIP build, so an oversize
UDP receive **silently truncates** to `UdpMaxPacketBytes` (1200) rather than reporting `Full`
or wedging — the previously-`UNVERIFIED` branch, now resolved (recorded, not patched;
`Modules/` is read-only).

**Feature:** a real transport behind the same `INetDriver` seam — lwIP UDP via
`FEsp32UdpDriver`, echoing datagrams to a PC on the same network.

**MicroWorld APIs:** `FEsp32UdpDriver` (`TrySend` / `TryReceive` /
`PollReadable` / `IsOpen` / `BoundPort`), UDP address helpers in
`MicroWorld/PlatformEsp32/UdpAddress.h` (`UdpAddressPort` on the sender
address), `ENetResult`, `FNetReceiveResult`.

**Reference code:**
`Modules/Core/tests/consumer/src/PlatformEsp32Main.cpp` (full-stack ESP32
composition); `Modules/PlatformEsp32/src/Esp32UdpDriver.cpp`;
`docs/Porting.md` seam 2.

**Program behavior:**

1. `ConnectWifiStation("ex15")` (scaffold §3.8) — prints the board's IP; then
   construct the static driver bound to `kServerPort` and print
   `[ex15] listening port=40404`.
2. Loop forever: `PollReadable(250)`; when readable, `TryReceive` into a
   1200-byte static buffer, print `[ex15] rx bytes=<n> from_port=<p>`, then
   `TrySend` the same bytes back to `OutFrom` and print the result.
3. PC side — commit `tools/EchoClient.py` inside the example:

   ```python
   """Sends one line to the board and prints the echo (usage: EchoClient.py <board-ip>)."""
   import socket, sys

   sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
   sock.settimeout(5.0)
   sock.sendto(b"hello microworld", (sys.argv[1], 40404))
   data, addr = sock.recvfrom(2048)
   print(f"echo from {addr}: {data!r}")
   ```

**Extra files:** `src/NetworkConfig.example.h`, `src/WifiStation.h/.cpp`
(§3.8), `tools/EchoClient.py`; `src/CMakeLists.txt` gets the
`PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event` variant (§3.5).

**Hardware checkpoint:** human copies `NetworkConfig.example.h` →
`NetworkConfig.h` with real credentials, flashes, runs the Python client, and
captures both the board trace and the client's echo line. Nondeterministic
parts: IP addresses, sender ports.

**Steps / Done when / Verify:** as task 1.1 plus the extra files, tag `[ex15]`.

---

### Phase 7 — Two boards

#### Task 7.1 — `16-TwoBoardUdp`

- [x] Built
- [x] Hardware-verified

Built 2026-07-23 — `pio run` `[SUCCESS]` both role envs (server/client). Design deviates
from the station-mode spec below: the server hosts a **SoftAP** the client joins, so no
router and no real credentials are needed and the server address is the fixed gateway
`192.168.4.1` (no IP-copy step). See `examples/16-TwoBoardUdp/README.md`.

Hardware-verified 2026-07-23 — two ESP32-S3 boards, no router. The server console (SoftAP
host) showed the client associating (`station join`, DHCP `192.168.4.2`) and then
`server spawned actor -> world actor count=1`, `=2`, `done` — the remote client's `TNetHost`
Hello/Welcome admission and two channel-1 spawn requests all crossed WiFi UDP and drove the
server's engine world. Runtime-verifies the full `TNetHost` + `TEngineHost` message design
over WiFi UDP across two boards — the WiFi twin of example 19 over UART. **With this, both
WiFi examples (15, 16) are hardware-verified.**

**Feature:** the full networked engine across two real machines — a dedicated
server `TEngineHost` bound to `TNetHost` through the `TNetHostFrame` seam, and
a bare `TNetHost` client, exchanging channel messages over WiFi UDP. This is
`Modules/PlatformHost/examples/TwoNodeDemo` split across two physical boards.

**MicroWorld APIs:** `TNetHost` (`Configure` / `Start` / `PumpReceive` /
`PumpSend` / `SendTo` / `Broadcast`, message-handler multicast), `ENetMode`,
`FNetHostConfig`, `FPeerId`, `TNetHostFrame` / `INetworkFrame`, the
`TEngineHost` network-frame constructor (frame inbound = Tick step 1, outbound
= step 7), `NetProtocol.h` message read/write, `UWorld::SpawnActor` on request,
`MakeUdpAddress` (client building the server address from `kServerIpv4`).

**Reference code:** `Modules/PlatformHost/examples/TwoNodeDemo/Main.cpp` —
**read it first and port its protocol**: client channel-1 spawn requests,
server per-tick channel-2 actor-count broadcast, the same reserved opcode.
`Modules/Net/include/MicroWorld/Net/NetHost.h` doc comments;
`Modules/Core/tests/consumer/src/PlatformEsp32Main.cpp`.

**Roles:** one source tree; `Main.cpp`'s `app_main` calls `RunServer()` or
`RunClient()` selected by a compile definition. `platformio.ini` replaces
`[env:esp32-s3]` with two environments, both extending the same base values:

```ini
[env:esp32-s3-server]
; ...all [env:esp32-s3] lines from §3.3, plus:
build_flags = ${microworld.build_flags} -DMICROWORLD_EXAMPLE_SERVER=1

[env:esp32-s3-client]
; ...all [env:esp32-s3] lines from §3.3, plus:
build_flags = ${microworld.build_flags} -DMICROWORLD_EXAMPLE_SERVER=0
```

`src/CMakeLists.txt` registers `Main.cpp ServerMain.cpp ClientMain.cpp
WifiStation.cpp` with the WiFi `PRIV_REQUIRES` variant — both roles always
compile; the define only selects which one runs.

**Program behavior:**

1. Server board: WiFi up → print `[ex16] server ip=<a.b.c.d>` → static
   composition (engine host + net frame + net host in `DedicatedServer` mode
   over a `FEsp32UdpDriver` bound to `kServerPort`) → drive
   `TEngineHost::Tick(GTimeSource.Now())` forever; per tick, broadcast the
   world actor count on channel 2; print one line per spawn performed.
2. Client board: WiFi up → configure `Client` mode against
   `MakeUdpAddress(kServerIpv4..., kServerPort)` → `Start` → pump; once
   connected, send two channel-1 spawn requests 1 s apart; print every
   channel-2 count received: the sequence must show the count reaching 2.
3. Both sides print a `[ex16] done` line after the client observes count 2
   (server keeps running).

**Hardware checkpoint:** needs both boards flashed (one env each), same WiFi,
`NetworkConfig.h` on the client carrying the server IP printed at server boot.
Captured traces from both boards go into the README.

**Steps / Done when / Verify:** as task 1.1 plus role environments, tag
`[ex16]`.

#### Task 7.2 — `17-TwoBoardLora`

- [ ] Built
- [ ] Hardware-verified

**Feature:** the same `INetDriver` seam over a radio — E32 LoRa UART transport
with the portable frame codec on the wire, two boards ping-ponging a counter
with no WiFi at all.

**MicroWorld APIs:** `FEsp32E32LoraDriver` + `FEsp32E32LoraConfig`,
`E32MaxPayloadBytes` (payloads must stay ≤ 58 bytes), the LoRa address helpers
in `MicroWorld/PlatformEsp32/LoraAddress.h` (locate the make/inspect helpers
with `rg -n "LoraAddress" Modules/PlatformEsp32`), `ENetResult`,
`FNetReceiveResult`.

**Reference code:**
`Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32E32LoraDriver.h`
doc comments; `Modules/PlatformEsp32/src/Esp32E32LoraDriver.cpp` and
`E32UartPlatformImplementation.h`; example `14-NetFrameCodec` (same framing,
now on air).

**Wiring (both boards identical):**

| E32 pin | Connect to | Why |
| --- | --- | --- |
| VCC | 5 V (module-dependent; check your E32 datasheet) | radio power |
| GND | GND | common ground |
| TXD | ESP32 GPIO 18 (`RxGpio`) | module → board bytes |
| RXD | ESP32 GPIO 17 (`TxGpio`) | board → module bytes |
| M0, M1 | GND | transparent (normal) mode — the driver assumes it |
| AUX | unconnected | not used by this driver |

Config: `FEsp32E32LoraConfig{ UartPort = 1, TxGpio = 17, RxGpio = 18,
BaudRate = 9600, LocalNodeId = <1 on node A, 2 on node B> }`. Both modules
must still be on factory channel/address defaults; reconfiguring E32 registers
is out of scope for this example.

**Roles:** symmetric firmware; two environments
`[env:esp32-s3-node-a]` / `[env:esp32-s3-node-b]` differing only in
`-DMICROWORLD_EXAMPLE_NODE_ID=1` / `=2` (same pattern as task 7.1).

**Program behavior:**

1. Construct the static driver; print `[ex17] node=<id> open=<0|1>` (if the
   UART failed to open, stop with a clear line instead of looping).
2. Node 1 starts the volley: every 2 s send a 5-byte payload carrying a
   `std::uint32_t` counter to the broadcast LoRa address; print
   `[ex17] tx n=<counter> result=<text>`.
3. Both nodes poll `TryReceive` in the paced loop; on `Success` print
   `[ex17] rx n=<counter> from=<node-id>`, then reply with counter + 1 after
   a 2 s delay. The counter climbs alternately across the two serial monitors.
4. Corrupted or partial frames surface only as quiet `Unavailable` polls — the
   decoder's resync (example 14) is doing its job on real noise.

**Hardware checkpoint:** needs both boards with wired E32 modules. Capture ~10
lines from each monitor showing the alternating counter. Radio traffic is
inherently lossy — the counter may stall and resume; that is expected and the
README must say so.

**Steps / Done when / Verify:** as task 1.1 plus role environments and the
wiring table in the README, tag `[ex17]`.

---

## 7. Feature-coverage matrix

Every public MicroWorld feature maps to at least one example. Auditors: a row
whose example column is empty is a plan defect — file it.

| Module | Feature | Example(s) |
| --- | --- | --- |
| Core | `FTickFunction` / `FTickConfiguration` / `FTickDecision` | 01 |
| Core | `FApplication`, `FLifecycleGuard`, `ELifecycleState` | 02 |
| Core | `FTickable` contract | 01 (direct), 10 (via Actor/Component) |
| Core | `ERuntimeResult` typed results | 02, 06 |
| Core | `Log.h` sink seam (`MW_LOG`, `FLogSink`) | 03 |
| Core | `TimePointMilliseconds` caller-time contract | 01 and every later example |
| Core | `FVersion` | 01 |
| Memory | `IMemoryResource`, `TFixedArena`, `FMemoryBlock`, `EMemoryResult` | 04 |
| Memory | `TUniquePtr` / `MakeUnique` | 05 |
| Memory | `TSharedPtr` / `TWeakPtr` / `MakeShared` | 05 |
| Memory | `TStaticVector` | 06 |
| Memory | `TSpan` | 06 (direct), 12+ (as I/O currency) |
| Memory | `TDelegate` / `TMulticastDelegate` / `FDelegateHandle` | 07 |
| Object | registry, store, handles, `TObjectPtr` family | 08 |
| Object | roots, tracing, budgeted GC, `CollectFull` | 09 |
| Engine | `UWorld` / `AActor` / `UActorComponent` lifecycle + dispatch order | 10 |
| Engine | `TEngineHost` composition | 10, 16 |
| Engine | `TTimerManager` / `FTimerHandle` / `ETimerMode` | 11 |
| Engine | `TNetHostFrame` / `INetworkFrame` seam | 16 |
| Net | `FByteWriter` / `FByteReader` | 12 |
| Net | `ENetResult` normalized outcomes | 12–17 |
| Net | `INetDriver` contract | 13 (loopback), 15/16 (UDP), 17 (LoRa) |
| Net | `TNetManager` / `TNetPacketStorage` | 13 |
| Net | `THostLoopback` | 13 |
| Net | `FrameCodec` / `TFrameDecoder` | 14 (host logic), 17 (on air) |
| Net | `UdpAddressCodec` (`MakeUdpAddress` etc.) | 15, 16 |
| Net | `NetProtocol.h` messages, `TNetHost` channels/peers | 16 |
| PlatformEsp32 | `FEsp32TimeSource` | 01 and every later example |
| PlatformEsp32 | `Esp32LogSink` | 03 |
| PlatformEsp32 | `FEsp32UdpDriver` + UDP address helpers | 15, 16 |
| PlatformEsp32 | `FEsp32E32LoraDriver` + `LoraAddress` | 17 |

Deliberately out of scope: `PlatformHost` adapters (`FHostUdpDriver`,
`FHostTimeSource`, `WinSockScope` — host-only, already exercised by the module
examples and tests), `RawSlot` (internal storage helper, not a user-facing
feature), and E32 register/parameter configuration (vendor concern).

---

## 8. Completion

The roadmap is complete when every **Built** box and every **Hardware-verified**
box is checked. Then, in one closing commit:

1. Append a `CHANGELOG.md` entry: ESP32 example suite added (17 examples).
2. Add the closing evidence section to `PROGRESS.md` (per-phase lines already
   exist; the closer records the final full-suite hardware session).
3. Update the root `README.md` to point students at `examples/README.md` as
   the entry into MicroWorld.
