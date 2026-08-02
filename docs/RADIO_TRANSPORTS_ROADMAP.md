# MicroWorld — Radio Transports Roadmap (E32 LoRa & Bluetooth LE)

**Version:** 1.0 · **Date:** 2026-07-24 · **Owner:** Mykola
**Baseline:** `main` at `b4973be` (clean tree), Windows 11 root superbuild + PlatformIO for examples, ESP-IDF 6.0.1 via PlatformIO.
**Scope:** `Modules/MicroWorld/Platform/Esp32`, `examples/`, `docs/architecture/decisions/`. Portable
packages (`Core`, `Engine`, `Messaging`, `Transport`, `Application`) are **untouched** —
a radio is just another `Core::ITransportDevice` (ADR 0003 logic applies unchanged).

**Mission.** Give MicroWorld two working radio links and prove them the same
way the wired links were proven:

1. **E32 LoRa** — the device (`FEsp32LoraDevice`) shipped long ago but no
   example exists and it has never run on hardware. Finish it: volley example,
   hardware verification, full client/server messaging example.
2. **Bluetooth LE** — nothing exists. Design spike (ADR), a
   central/peripheral `Core::ITransportDevice` pair, volley + messaging examples.
3. **Capstone** — a fully wireless two-board world: BLE + LoRa as two
   channels of one actor-messaging world (gated on the messaging roadmap).

This document is the active plan and progress tracker for that work, written
so that any LLM (including a weak one) can pick it up, find the next task,
complete it, and record progress without extra context. Companions:
`examples/AGENTS.md` (owns the build and hardware-verification procedure),
`Modules/MicroWorld/Messaging/AGENTS.md` (owns the composition recipes), and examples
18–21 (the wired device approach this plan imitates for radios).

Completed tasks below still cite `PROGRESS.md` and `CHANGELOG.md`. Both files
were deleted on 2026-07-26 because they had become a third and fourth record of
facts that already had owners, and both had drifted out of date. What changed
now lives in git history, what is next in this document, hardware evidence in
each example's `README.md`, and measured margins in
`Modules/*/benchmarks/Results/`. Read those citations as history, not as
instructions.

---

## 1. How to use this document (protocol for LLM workers)

Follow these rules exactly:

1. Read section **2 (Ground rules)** and section **4 (Target design)** before
   touching any code.
2. Open section **5 (Progress tracker)**. Find the first phase whose status is
   not ✅. Inside that phase, find the first unchecked `[ ]` task.
3. Work on **exactly one task at a time**, in order. Never start a later phase
   while an earlier phase has unchecked tasks — except that ⛔-gated Phase 5
   may be skipped over if its gate is closed (see the phase header).
4. Every task has **Steps**, a **Done when** checklist, and a **Verify**
   instruction. A task is complete only when every "Done when" item is true
   and every Verify command passes.
5. When a task is complete: change its `[ ]` to `[x]`, append one evidence
   line directly under it (`Done YYYY-MM-DD — <one sentence of proof>`), and
   update the phase status in the tracker (⬜ → 🟨 on first task, 🟨 → ✅ on
   last).
6. Tasks marked **(owner-gated)** require explicit human authorization and
   hardware at the desk. If the owner is not available, write
   `⛔ WAITING FOR OWNER` under the task and stop.
7. If you are blocked, write `⛔ BLOCKED:` plus one sentence under the task
   and stop. Do not skip ahead.
8. Never delete or rewrite this document's structure. Only update statuses,
   checkboxes, evidence lines, and BLOCKED/WAITING notes.
9. A phase's evidence is the per-task `Done YYYY-MM-DD` lines from rule 5 plus
   the commit that carried them. There is no separate status file to update.

Status legend: ⬜ not started · 🟨 in progress · ✅ done · ⛔ blocked/gated

### 1.1 Standard Verify (host edition)

Run from the repo root, in this order, for every task touching `Modules/`:

```sh
clang-format --style=file:clang-format -i <every .h/.cpp file you touched>
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python tools/CheckDocumentationStyle.py --root Modules
```

PlatformEsp32 has no host build: for its files the gate is a line-by-line
re-read plus the ESP32 consumer compile probe (`pio run` in
`Modules/tests/Core/consumer` for the relevant env), exactly as the wired plan
used.

### 1.2 Example Build Verify

```sh
pio run -d examples/<NN-Name>
```

Every environment must compile clean. Compile success is never a runtime
claim.

### 1.3 Hardware checkpoint (owner-gated — never self-serve)

Reuses the `examples/AGENTS.md` hardware checkpoint verbatim: building never flashes;
upload/monitor requires explicit human authorization; READMEs carry
"not yet verified on hardware" until a captured trace is pasted in. Rig
notes that are already paid for:

- Both boards attach via **native USB-JTAG** (the examples' console default);
  capture traces with the repo's reconnecting reader (`tools/`, `mwlog.py`) —
  **never** `pio device monitor` (it wedges on reconnect).
- The rig is asymmetric — put the role that prints the decisive trace on the
  CH343 board when serial features differ.

### 1.4 How to locate code

Every `file:line` was verified at baseline `b4973be`. Locate code by the
quoted symbol (`rg -n "SymbolName" Modules`), never by remembered offsets.

### 1.5 Files you must never edit

- `Modules/*/benchmarks/Results/*.md` — measured evidence, not prose to edit.
- `examples/README.md` is the one place a new example is registered; this plan
  tracks only its own tasks.
- `Modules/*/benchmarks/Results/*.md`;
  `examples/esp32-common/sdkconfig.defaults` and `partitions.csv` (frozen
  board profile — D11 shows how BLE builds extend it without editing it);
  `LICENSE`; anything under `build/`, `.pio/`, `.git/`.

---

## 2. Ground rules (invariants — never violate)

### 2.1 Inherited embedded invariants (unchanged)

The portable-code rules of the root `AGENTS.md`: C++17, no exceptions/RTTI, no
steady-state allocation, no hidden clock (only platform adapters read real
clocks), enum errors with transactional failure, determinism (no RNG),
dependency direction enforced, frozen identity, Doxygen `/** */` on every
declaration and persistent member, `AGENTS.md` in every new folder,
plain-English names (no metaphors, no new abbreviations; `Ble` and `Lora`
join the allowed industry vocabulary alongside `Udp`/`Uart`), format with
`clang-format --style=file:clang-format` (policy file has **no dot**).

### 2.2 Radio-edge rules (new, this plan)

- **Edge-only.** New code lives in `Modules/MicroWorld/Platform/Esp32`, `examples/`, and
  `docs/architecture/decisions/`. If a task seems to need a portable-package change, stop
  and write `⛔ BLOCKED` — that is a design error in this plan, not a license
  to edit `Transport`.
- **All vendor headers stay private.** ESP-IDF/NimBLE includes are confined
  to `src/*PlatformImplementation.h` and `.cpp` files; public headers carry
  only config structs and plain types
  (`rg -n "esp_|nimble|freertos|host/ble" Modules/MicroWorld/Platform/Esp32/include` must
  stay 0).
- **Devices implement the full `Core::ITransportDevice` contract** (`Core/IO/TransportDevice.h`):
  non-blocking, at most one transport operation per call, transactional
  receives, `IsOpen()` guard after a `noexcept` constructor — mirror
  `Esp32UartDevice.h` / `Esp32I2cMasterDevice.h` shape for shape.
- **Antenna rule (safety, goes in every LoRa README):** never power an E32
  module without its antenna attached — transmitting into no load can damage
  the RF stage. Keep the two antennas ≥ 0.5 m apart on the bench.
- **Radio-legal note (goes in LoRa READMEs):** bench tests run at the
  module's factory default power on its factory default channel; regional
  regulations are the operator's responsibility.
- **BLE security posture (goes in BLE READMEs):** v1 links are unencrypted,
  unauthenticated Just-Works connections for bench use only (D6).
- The engine-first example rule in `examples/AGENTS.md` applies to every example
  this plan creates. Its facade amendment has expired: `WriteEsp32LogRecord` and
  `SleepMilliseconds` shipped, so no new example may fall back to the old
  `std::printf`/`vTaskDelay` baseline used by examples
  18–21, and task 6.1 sweeps them onto the facades if those exist by then.

### 2.3 Decisions record (settled — do not relitigate while executing)

- **D1 — "Bluetooth" means Bluetooth LE.** The ESP32-S3 has **no Bluetooth
  Classic radio** — BLE 5.0 only. Classic (SPP, A2DP…) is permanently out of
  scope for this target. Anyone asking for "Bluetooth serial" gets BLE.
- **D2 — A radio is just another `Core::ITransportDevice`** (ADR 0003 reasoning).
  Framing reuses the shipped `FrameCodec` byte-pump decoder; sessions reuse
  `TTransportHost`; nothing portable changes. That reuse is the entire payoff.
- **D3 — NimBLE is the working assumption for the BLE host stack** (lighter
  than Bluedroid, BLE-only fits D1). The Phase 2 spike confirms or overturns
  this with header/size evidence in ADR 0004; only the ADR may change it.
- **D4 — BLE topology v1 is point-to-point**: one central ↔ one peripheral,
  as a device *pair* (`FEsp32BleCentralDevice` / `FEsp32BlePeripheralDevice`)
  mirroring the I2C/SPI master/slave precedent — the role asymmetry enters
  only at the platform edge. Multi-peripheral centrals: revisit trigger is a
  real 3-board example need.
- **D5 — One frame = one GATT operation.** The central writes whole encoded
  frames to the RX characteristic (write-without-response); the peripheral
  notifies whole frames on the TX characteristic. The connection MTU is
  negotiated at connect and must cover `BleMaxPayloadBytes + FrameOverheadBytes + 3`;
  if negotiation lands lower, the device reports `Unavailable` rather than
  fragmenting. No fragmentation layer in v1.
- **D6 — No pairing, no bonding, no encryption in v1.** Bench link only.
  Revisit trigger: any deployment beyond the bench.
- **D7 — E32 modules run factory defaults in transparent mode**, M0 = M1 =
  GND, UART 9600 8N1, factory channel/address. A module-configuration tool
  (AT/command mode) is out of scope; if a module was reconfigured, restore
  factory defaults manually before blaming the device.
- **D8 — LoRa session profile is `TTransportHost<2, 58>` with relaxed timing**:
  `HeartbeatIntervalMilliseconds = 3000`, `PeerTimeoutMilliseconds = 15000`.
  At the E32's default air rate a full frame costs hundreds of milliseconds
  of airtime — the UDP-era 1000/5000 defaults would congest the channel.
- **D9 — Example numbering:** this plan builds the long-reserved
  `17-TwoBoardLora` catalog slot, then takes `26`–`29`
  (26 LoraMessaging, 27 TwoBoardBle, 28 BleMessaging, 29 WirelessWorld).
  Registration happens only in `examples/README.md` (§1.5).
- **D10 — Phase 5's messaging gate is satisfied.** It required actor messaging
  over a wire, because example 29 always composes two channels.
  `FMessagingSystem` ships that as two channels on one engine-owned system. The
  router, bindings, reliable wrapper and play-system set this plan originally
  named were deleted in the Networking dissolution; examples 22–25 exercise the
  replacement. Everything in Phases 0–4 here uses only shipped API
  (`TTransportHost`, `THostPlaySystem`, `TEngine`).
- **D11 — BLE builds extend sdkconfig additively.** The shared
  `examples/esp32-common/sdkconfig.defaults` is frozen; BLE examples pass a
  **second** defaults file (`examples/esp32-common/sdkconfig.ble.defaults`,
  created once in task 3.2) via PlatformIO's semicolon-separated
  `SDKCONFIG_DEFAULTS` list. The spike confirms the exact flag set
  (`CONFIG_BT_ENABLED`, NimBLE selection, MTU).
- **D12 — WiFi + BLE coexistence is out of scope v1.** No example runs both
  radios of the S3's shared RF path at once; example 29 pairs BLE with LoRa
  (an external radio) precisely to avoid it. Revisit trigger: a real need,
  plus the coexistence sdkconfig flags in a new ADR.

### 2.4 Reference files (imitate them)

| Concern | Imitate |
| --- | --- |
| UART-attached radio transport | `Modules/MicroWorld/Transport/.../E32LoraDevice.h` + `Modules/MicroWorld/Platform/Esp32/.../Esp32LoraDevice.h` facade + `Internal/Esp32UartByteStream.h` |
| Role-asymmetric device pair + ISR-side inbox ring | `Modules/MicroWorld/Platform/Esp32/.../Esp32I2cMasterDevice.h` / `Esp32I2cSlaveDevice.h` + `I2cReceiveInbox.h` (`FI2cReceiveInbox`) |
| Per-device 1-byte address codec | `Modules/MicroWorld/Platform/Esp32/.../LoraAddress.h`, `UartAddress.h` |
| Design-spike ADR with header-derived answers | `docs/architecture/decisions/0003-wired-transports.md` Appendices A/B |
| Device volley example | `examples/18-TwoBoardUart` |
| Full TTransportHost + engine messaging example | `examples/19-UartMessaging` |
| Two-link, one-world composition (Phase 5 only) | `Modules/MicroWorld/Messaging/AGENTS.md` composition recipes |

---

## 3. What exists today (verified at `b4973be` — the map)

**LoRa historical baseline: device yes, proof no.** The current architecture
places portable E32 framing/state in optional `Transport/Lora`, with ESP32 and Pico
compatibility facades owning UART SDK lifetime. `IUartByteStream` is a narrow
byte-transfer interface, not a universal HAL. `FEsp32LoraDevice`
(`Esp32LoraDevice.h:51`): config `{UartPort, TxGpio, RxGpio,
BaudRate{9600}, LocalNodeId}`, `E32MaxPayloadBytes = 58`, frames via
`TFrameDecoder<58>`, address codec `LoraAddress.h`
(`MakeLoraAddress`/`IsLoraAddress`/`LoraAddressNodeId`). Compile-verified
only: **no example uses it, it has never been flashed**, and the catalog row
`17-TwoBoardLora` has sat ⬜ since the examples plan was written.

**Bluetooth: nothing.** No BLE code anywhere in the repo; the shared
`sdkconfig.defaults` does not enable the BT controller. The ESP32-S3-WROOM-1
modules on the rig support BLE 5.0 (and only BLE — D1). No extra hardware is
needed for BLE.

**Everything above the `Core::ITransportDevice` interface is ready and battle-tested** on UDP, UART,
I2C, and SPI: `FrameCodec` (CRC-16, resync), `TTransportManager`, `TTransportHost`
(roles/peers/channels/heartbeats), `THostPlaySystem` → `TEngine`, and four
verified two-board examples (18–21) plus two WiFi ones (15–16) to copy from.

**Hardware on the desk:** 2 × ESP32-S3-WROOM-1-N16R8 boards (USB-JTAG on
COM5/COM7), 2 × E32 LoRa modules with antennas. BLE needs nothing extra.

---

## 4. Target design (read before Phase 2)

### 4.1 LoRa — nothing new to design

The device, address codec, and payload cap exist. The work is examples +
hardware proof + the D8 session profile. Payload budget (mirrors the
messaging plan's table):

| Layer | LoRa budget |
| --- | --- |
| Device frame payload (`E32MaxPayloadBytes`) | 58 |
| `TTransportHost` message payload (−4 header) | 54 |
| Encoded actor message, best-effort (Phase 5) | 54 (payload ≤ 48) |
| Encoded actor message, guaranteed (−3) | 51 (payload ≤ 45) |

Phase 5 therefore uses `MaxMessageBytes = 48` on any router whose world
includes a LoRa channel.

### 4.2 BLE — target shape (spike-confirmed; only ADR 0004 may change it)

**Link model (D4/D5):** one custom 128-bit GATT service (the Nordic-UART
convention, own UUIDs fixed by the spike) with two characteristics: **RX**
(central writes encoded frames, write-without-response) and **TX**
(peripheral notifies encoded frames). Both directions carry the same
`FrameCodec` frames as every other transport; received bytes are pumped
through `TFrameDecoder` exactly like the UART device, so partial deliveries
are already handled. MTU is requested at connect to cover one whole frame.

**Public header target** (`Modules/MicroWorld/Platform/Esp32/Esp32BleDevice.h`):

```cpp
/** Largest payload one BLE frame carries; uniform with the wired transports. */
inline constexpr std::size_t BleMaxPayloadBytes = 120;

/** Settings for the advertising (peripheral) side of the point-to-point link. */
struct FEsp32BlePeripheralConfig
{
    const char* DeviceName;          // advertised name the central scans for
    std::uint8_t LocalNodeId;        // frame identity, mirrors the wired devices
};

/** Settings for the scanning (central) side of the point-to-point link. */
struct FEsp32BleCentralConfig
{
    const char* PeerDeviceName;      // peripheral to connect to
    DurationMilliseconds ConnectTimeoutMilliseconds{15000};
    std::uint8_t LocalNodeId;
};

/** Peripheral-role BLE transport: advertises, accepts one central. */
class FEsp32BlePeripheralDevice final : public Core::ITransportDevice
{
public:
    explicit FEsp32BlePeripheralDevice(const FEsp32BlePeripheralConfig& Config) noexcept;
    bool IsOpen() const noexcept;        // stack up, service registered, advertising
    bool IsConnected() const noexcept;   // a central is connected and MTU covers one frame
    // ITransportDevice: TrySend = one notify of one encoded frame (Unavailable until connected);
    //             TryReceive = drain inbox ring through the decoder, one frame max;
    //             MaxPacketBytes = BleMaxPayloadBytes.
};

/** Central-role BLE transport: scans, connects to one named peripheral. */
class FEsp32BleCentralDevice final : public Core::ITransportDevice
{
public:
    explicit FEsp32BleCentralDevice(const FEsp32BleCentralConfig& Config) noexcept;
    bool IsOpen() const noexcept;
    bool IsConnected() const noexcept;
    // ITransportDevice: TrySend = one write-without-response; TryReceive as above.
};
```

Plus `BleAddress.h` (`MakeBleAddress`/`IsBleAddress`/`BleAddressNodeId`,
1-byte node id, per-device codec duplication is the package precedent).
NimBLE callbacks run on the host task → they push raw bytes into an SPSC
inbox ring (imitate `FI2cReceiveInbox`); `TryReceive` pops and pumps the
decoder on the caller's thread. Reconnect policy v1: a dropped connection
flips `IsConnected()` false and the devices resume
advertising/scanning internally; sends meanwhile return `Unavailable`
(callers already handle that — it is the standard device contract).

**Spike questions ADR 0004 must answer with evidence** (ESP-IDF 6.0.1
headers/docs, imitate ADR 0003's appendices): NimBLE vs Bluedroid final call
(flash/RAM cost figures); exact sdkconfig flag set for D11's
`sdkconfig.ble.defaults`; service/characteristic UUIDs; achievable MTU
between two S3s and the request sequence; write-without-response vs write
throughput/backpressure (what does "device `Full`" map to); connection
supervision timeout vs `TTransportHost` heartbeat profile for BLE; how the consumer
compile probe builds with BT enabled without touching the shared profile.

### 4.3 Session profiles per radio

| | LoRa (D8) | BLE |
| --- | --- | --- |
| `TTransportHost` | `TTransportHost<2, 58>` | `TTransportHost<2, 120>` |
| Heartbeat / timeout ms | 3000 / 15000 | 1000 / 5000 (spike may adjust) |
| Server address | `MakeLoraAddress(<node>)` | `MakeBleAddress(<node>)` |

---

## 5. Progress tracker

| Phase | Title | Tasks | Status |
| --- | --- | --- | --- |
| 0 | Baseline & governance | 2 | ✅ |
| 1 | LoRa proven on hardware | 4 | ✅ |
| 2 | Bluetooth LE design spike (ADR 0004) | 1 | ⬜ |
| 3 | BLE device pair | 2 | ⬜ |
| 4 | BLE examples on hardware | 4 | ⬜ |
| 5 | Wireless actor-messaging world | 2 | ⛔ gated (D10) |
| 6 | Documentation & close-out | 2 | ⬜ |

---

## 6. Phases and tasks

### Phase 0 — Baseline & governance ✅

- [x] **0.1 Record a green baseline.** Clean tree check, Standard Verify
  (§1.1), `CheckFolderAgents.py`, and `pio run` for the seven existing
  examples. Record ctest count + doc-checker file count as the evidence line.
  Fix nothing.

  **Done when:** all gates recorded.
  **Verify:** the commands above.

  Done 2026-07-24 — clean tree at `b4973be` (only this plan doc +
  `.claude/prompts/radio-lead.md` untracked); MSVC/VS2022 Release build
  exit 0; `ctest -C Release` 11/11 passed, 0 failed;
  `CheckClassDocumentation.py --require-doxygen` 157 files;
  `CheckFolderAgents.py` (Modules) 63 guides; all 11 existing examples
  (19 envs across 3 single-env + 8 two-env projects) `pio run` exit 0,
  0 `[FAILED]` — the task's "seven" is inherited text that predates
  examples 22–25. Nothing fixed.

- [x] **0.2 Register this plan.** One sentence each in root `AGENTS.md` and
  root `README.md`: `docs/RADIO_TRANSPORTS_ROADMAP.md` is the active plan for
  LoRa and Bluetooth LE. One `PROGRESS.md` line with the baseline counts.

  **Done when:** three files mention this plan; Standard Verify still green.

  Done 2026-07-24 — root `AGENTS.md`, root `README.md`, and `PROGRESS.md` each
  now name `docs/RADIO_TRANSPORTS_ROADMAP.md` as the active E32 LoRa / Bluetooth
  LE plan (one sentence each in AGENTS/README; one `PROGRESS.md` baseline row
  carrying the 0.1 counts). Docs-only; `ctest -C Release` still 11/11, nothing
  else changed.

---

### Phase 1 — LoRa proven on hardware 🟨

Goal: the four-year-old catalog hole closes — the E32 device runs on the
bench, first as a raw volley, then under the full engine.

- [x] **1.1 Example `17-TwoBoardLora` (device volley).** Copy example 18's
  entire shape (`examples/18-TwoBoardUart` — one `Main.cpp`, two role envs
  via `-DMICROWORLD_EXAMPLE_NODE_ID=1|2`, counter ping-pong, `[ex17]` tag)
  with these substitutions: device `FEsp32LoraDevice`, config
  `{.UartPort = 1, .TxGpio = 17, .RxGpio = 18, .BaudRate = 9600,
  .LocalNodeId = 1|2}` (same pins as example 18 — the E32 replaces the null
  wire), addresses via `MakeLoraAddress`, volley pacing ≥ 1000 ms (airtime,
  D8 rationale). README wiring section: ESP TX17→E32 RXD, ESP RX18→E32 TXD,
  M0+M1→GND (transparent mode, D7), VCC→3V3 with common ground, AUX
  unconnected; **the §2.2 antenna rule and radio-legal note verbatim**; the
  "not yet verified on hardware" sentence. Folder `AGENTS.md`; catalog row
  status update in `examples/README.md` (the row already exists).

  **Done when:** both envs compile; README/AGENTS complete; catalog updated.
  **Verify:** `pio run -d examples/17-TwoBoardLora` + repo ctest (format gate).

  Done 2026-07-24 — `examples/17-TwoBoardLora` created (6 tracked files:
  `src/Main.cpp`, `src/CMakeLists.txt`, `CMakeLists.txt`, `platformio.ini`,
  `README.md`, `AGENTS.md`) as example 18's volley with only the transport
  swapped to `FEsp32LoraDevice` (config `UartPort 1 / TxGpio 17 / RxGpio 18 /
  BaudRate 9600 / LocalNodeId 1|2`, `MakeLoraAddress` destination,
  `LoraAddressNodeId` sender, `E32MaxPayloadBytes` RX buffer, 1000 ms airtime
  pacing per D8, `ex17` tag, engine-first `MW_LOG`/`SleepMilliseconds`/
  `Esp32LogSink`). README carries the per-board ESP↔E32 wiring table (common
  ground, `M0=M1=GND` transparent mode D7, `AUX` unconnected), the §2.2 antenna
  rule and radio-legal note verbatim, and the "not yet verified on hardware"
  status; catalog row 17 ⬜→🟨. Lead-verified: `pio run` both envs `[SUCCESS]`
  (RAM 20604 B, Flash 216637/216633 B), `ctest -C Release` 11/11, clang-format
  clean, no vendor/`printf`/`vTaskDelay` includes in `Main.cpp`. Lead fixed the
  README wiring table (added the ESP↔E32 common-ground row, corrected the VCC
  row's rationale from "common ground" to "module power").

- [x] **1.2 (owner-gated) LoRa volley hardware checkpoint.** Flash node 1 and
  node 2, capture both consoles per §1.3 (`mwlog.py`, USB-JTAG). Expect the
  example-18-shaped alternating counter trace. Paste both traces into the
  README's "Verified output"; record image sizes. If the link is dead, check
  in order: antennas, M0/M1 strapping, factory-default channel/address (D7),
  TX/RX swap.

  **Done when:** README carries real captured traces from both boards.

  ✅ HARDWARE-VERIFIED 2026-07-24 — two ESP32-S3-DevKitC-1 boards, each with an
  EBYTE **E32-433T20D** (433 MHz, 20 dBm, transparent mode, FCC ID 2ALPH-E32) on
  UART1 (TX GPIO 17 / RX GPIO 18, M0 = M1 = GND, common ground, antenna
  attached). The owner attached the antennas and wired both boards and
  authorized the run; the lead then flashed both roles and captured both
  consoles on the connected rig (COM5/COM7, native USB-JTAG via `mwlog.py`).
  Both nodes logged `node=<id> open=1`, then the counter climbed alternately at
  the 1 s cadence with `result=Success` on every send and no stalls over ~29 s:
  node 1 `tx n=1 / rx n=2 from=2 / tx n=3 / … / tx n=19`, node 2
  `rx n=1 from=1 / tx n=2 / … / rx n=19`. The real capture is written verbatim
  into `examples/17-TwoBoardLora/README.md` "Verified output"; image sizes are
  unchanged from 1.1 (RAM 20604 B / Flash 216637 B). Catalog row 17 🟨 → ✅.

  *Start-ordering note (diagnosed on the rig):* node 1 seeds `tx n=1` exactly
  once, so node 2 must already be booted and listening when node 1 seeds — the
  seed is otherwise lost to the air and both boards sit silent (a mid-stream
  `mw log` then shows nothing, which is a start-order artifact, not a link
  fault). Reset/boot node 2 first, then node 1.

  *RadioE32 refactor closure (owner-approved 2026-07-28):* the current ESP32
  and Pico facades exchanged empty, typical, and maximum payloads in both
  directions; the detailed observed evidence lives only in
  [`examples/17-TwoBoardLora/README.md`](../examples/17-TwoBoardLora/README.md#payload-boundary-regression-hardware-verified-2026-07-28).
  The post-refactor two-ESP32 example-17 volley and example-26 messaging
  reruns were not performed because the second ESP32 has no E32 LoRa module.
  The owner accepted those unavailable reruns as waivers, not passes; the
  2026-07-24 two-ESP32 traces remain historical pre-refactor evidence.

- [x] **1.3 Example `26-MessagingOverLora` (the payoff demo).** Copy example 19's
  shape (`examples/19-UartMessaging` — server board: `TEngine` +
  `TTransportHost` DedicatedServer + `THostPlaySystem`, channel-1 message spawns an
  actor, channel-2 state broadcast; client board: bare client `TTransportHost`)
  with the D8/§4.3 LoRa profile: `TTransportHost<2, 58>`, heartbeat 3000 /
  timeout 15000, `ServerAddress = MakeLoraAddress(1)`, payloads trimmed to
  the §4.1 budget (state broadcast stays 2 bytes — fits trivially), pacing
  ≥ 1000 ms, tag `[ex26]`. README: same wiring/safety blocks as 17 plus the
  airtime paragraph (why the relaxed profile); catalog row appended.

  *Naming note:* planned as `26-LoraMessaging`, renamed to
  `26-MessagingOverLora` — PlatformIO's espidf builder corrupts any project
  path containing the substring `-L` while extracting linker search paths
  (`extract_link_args` in `builder/frameworks/espidf.py` strips every `-L`
  occurrence, mangling the path to `…\26oraMessaging\…`), which breaks the
  bootloader link deterministically. Folder names must avoid `-L`.

  **Done when:** both envs compile; README/AGENTS complete; catalog updated.
  **Verify:** `pio run -d examples/26-MessagingOverLora` + ctest.

  Done 2026-07-24 — `examples/26-MessagingOverLora` created (9 tracked files:
  `src/Main.cpp`, `src/ServerMain.cpp`, `src/ClientMain.cpp`,
  `src/LoraMessagingShared.h`, `src/CMakeLists.txt`, `CMakeLists.txt`,
  `platformio.ini`, `README.md`, `AGENTS.md`) as example 19's `TTransportHost`
  client/server protocol with only the transport swapped to
  `FEsp32LoraDevice` at the D8 airtime profile: `TTransportHost<2, 58>`, heartbeat
  3000 / timeout 15000, `ServerAddress = MakeLoraAddress(1)`, 2-byte state
  broadcast paced 1000 ms (server ticks the engine every poll but gates the
  radio broadcast on a deadline — a full E32 frame costs hundreds of ms of
  airtime), `ex26` tag. README carries the ESP↔E32 wiring table (common ground,
  `M0=M1=GND` transparent mode D7), the §2.2 antenna rule and radio-legal note
  verbatim, the airtime paragraph, and the "not yet verified on hardware"
  status; catalog row 26 ⬜→🟨. Lead-verified on 2026-07-24: `pio run` both envs
  `[SUCCESS]` (fullclean cold build server 72.2 s / client 73.3 s; server RAM
  25084 B / Flash 232341 B, client RAM 21388 B / Flash 221493 B — real numbers
  written into the README, replacing the pre-link estimates), `ctest -C Release`
  11/11, clang-format clean (all four `src/` files), no regression in the
  `Modules/MicroWorld/Platform/Esp32/include` vendor-include gate.

  *Blocker root-caused and fixed:* the first build failed with
  `ld: cannot open linker script file bootloader.ld` on every `pio run` while
  examples 17–25 built fine. Deterministic (7.6 s incremental repro), not a
  ninja race: PlatformIO's espidf builder mangles the linker `-L` search path
  for any project whose absolute path contains the substring `-L`
  (`extract_link_args` in `builder/frameworks/espidf.py` runs
  `fragment.replace("-L", "")`, which strips *every* `-L`, turning
  `…\26-LoraMessaging\…` into `…\26oraMessaging\…`). Renaming the folder to
  `26-MessagingOverLora` (no `-L` substring) + fullclean fixed it. Folder names
  must avoid `-L`; examples 27–29 planned names are already clear.

- [x] **1.4 (owner-gated) LoRa messaging hardware checkpoint.** Flash, capture
  both consoles: expect Hello/Welcome admission, a channel-1 spawn on the
  server world, periodic channel-2 state lines on the client, heartbeat
  survival over ≥ 60 s. Paste traces; record sizes.

  **Done when:** README carries the captured two-console trace.

  ✅ HARDWARE-VERIFIED 2026-07-24 — same two boards + E32-433T20D rig as 1.2
  (server on COM5, client on COM7; owner wired/antenna'd, lead flashed and
  captured). All four acceptance criteria met: the client logged
  `connecting` → `connected` (Hello/Welcome admission); it `sent spawn
  request 1`/`2` on channel 1 and the **server** logged
  `spawned actor -> world actor count=1` then `=2` and `done (server spawned 2
  actors)`; the client received the channel-2 state broadcast every second with
  the actor count climbing `0 → 1 → 2` (`done (observed actor count 2)`); and
  the state broadcasts continued unbroken to **tick 104 (~100 s uptime, still
  connected)** — heartbeat survival at 6.5× the 15 s peer timeout. The real
  two-console capture is written into
  `examples/26-MessagingOverLora/README.md` "Verified output"; image sizes are
  unchanged from 1.3 (server RAM 25084 B / Flash 232341 B, client RAM 21388 B /
  Flash 221493 B). Catalog row 26 🟨 → ✅. **Phase 1 complete.**

---

### Phase 2 — Bluetooth LE design spike ⬜

- [ ] **2.1 Write `docs/architecture/decisions/0005-bluetooth-le-transport.md`.** Status
  "Accepted" after owner sign-off. Content: D1 (S3 = BLE only, evidence),
  the D2 edge-only architecture, and an evidence-based answer to **every**
  spike question in §4.2 (imitate ADR 0003's appendix style: quote the
  ESP-IDF 6.0.1 / NimBLE headers you derived each answer from). Fix the
  GATT UUIDs, the sdkconfig flag set for D11, the MTU request sequence, and
  the final `FEsp32Ble*Config` field lists (start from §4.2; deviations must
  be argued in the ADR). If any answer overturns a D-decision, stop —
  **owner decision required** before Phase 3.

  **Done when:** ADR exists, every §4.2 question answered with a cited
  header/doc source; owner has approved (record the date).
  **Verify:** Standard Verify (docs only — format gate unaffected), plus
  `python tools/CheckFolderAgents.py` if a folder was added.

---

### Phase 3 — BLE device pair ⬜

- [ ] **3.1 `FEsp32BlePeripheralDevice` + `FEsp32BleCentralDevice` +
  `BleAddress.h`.** Implement §4.2 as confirmed by ADR 0004: public header
  `Esp32BleDevice.h` (configs + both device classes, full Doxygen), private
  `src/BlePlatformImplementation.h` + `src/Esp32BleDevice.cpp` (all
  NimBLE/ESP-IDF includes confined there), inbox ring imitating
  `FI2cReceiveInbox`, decoder pump imitating the UART device's receive path,
  `BleAddress.h` mirroring `LoraAddress.h`'s three functions. Non-copy,
  non-move, `noexcept` constructors, `IsOpen()`/`IsConnected()` guards,
  transactional receives. Update `library.json`/CMake lists if they
  enumerate sources; folder `AGENTS.md` guides.

  **Done when:** vendor-include gate 0 (`rg -n "esp_|nimble|freertos|host/ble"
  Modules/MicroWorld/Platform/Esp32/include` → 0); every declaration documented;
  line-by-line re-read recorded in the evidence line (no host build exists).
  **Verify:** Standard Verify + the include-gate grep. Compile proof
  deliberately lands in task 3.2 (the probe needs 3.2's sdkconfig plumbing) —
  a device nobody compiles is not done, so the phase closes only when 3.2's
  probe builds these classes.

- [ ] **3.2 BLE build plumbing + compile probe.** Create
  `examples/esp32-common/sdkconfig.ble.defaults` with exactly the ADR 0004
  flag set (D11 — the shared `sdkconfig.defaults` is untouched). Extend the
  ESP32 consumer project (`Modules/tests/Core/consumer`) with a `ble` env
  that layers both defaults files and compiles a minimal probe instantiating
  both devices (imitate how the existing Transport/Engine ESP32 probes are wired).
  Document the two-file `SDKCONFIG_DEFAULTS` mechanism in
  `examples/esp32-common/AGENTS.md`.

  **Done when:** the probe env compiles both devices under the strict flags;
  the frozen shared profile is byte-identical to before.
  **Verify:** `pio run` (consumer `ble` env) + `git -C . diff --stat
  examples/esp32-common/sdkconfig.defaults` shows no change + Standard
  Verify.

---

### Phase 4 — BLE examples on hardware ⬜

- [ ] **4.1 Example `27-TwoBoardBle` (device volley).** Example 18's shape
  again: peripheral env (`-DMICROWORLD_EXAMPLE_PERIPHERAL=1`, node id 1,
  `DeviceName "microworld-ex27"`) and central env (`=0`, node id 2,
  `PeerDeviceName` matching). Volley starts only when both sides report
  `IsConnected()`; pacing 500 ms; tag `[ex27]`. `platformio.ini` layers the
  D11 defaults pair. README: no wiring at all (first wireless wired-shaped
  example — say so), the §2.2 BLE security posture verbatim, connection
  troubleshooting (name mismatch, MTU too low → device `Unavailable`).
  AGENTS.md; catalog row.

  **Done when:** both envs compile; README/AGENTS/catalog complete.
  **Verify:** `pio run -d examples/27-TwoBoardBle` + ctest.

- [ ] **4.2 (owner-gated) BLE volley hardware checkpoint.** Flash both roles,
  capture per §1.3: expect connect handshake lines then the alternating
  counter. Unplug one board mid-run and replug: expect `Unavailable` lines,
  automatic reconnect, volley resumes (the §4.2 reconnect contract's first
  real proof). Paste traces + sizes.

  **Done when:** README carries traces including the reconnect episode.

- [ ] **4.3 Example `28-BleMessaging`.** Example 19's shape over BLE:
  server world on the **peripheral** board (it is the stationary role),
  client `TTransportHost` on the central, `TTransportHost<2, 120>` both sides, §4.3
  profile, tag `[ex28]`. README + AGENTS + catalog row.

  **Done when:** both envs compile.
  **Verify:** `pio run -d examples/28-BleMessaging` + ctest.

- [ ] **4.4 (owner-gated) BLE messaging hardware checkpoint.** Flash, capture:
  admission, channel-1 spawn, channel-2 state stream, ≥ 60 s heartbeat
  survival. Paste traces + sizes.

  **Done when:** README carries the captured two-console trace.

---

### Phase 5 — Wireless actor-messaging world ⬜

**Gate (open):** the messaging API this phase needs has shipped —
`FMessagingSystem`, its named channels and its `bIsReliable` flag, all exercised
by examples 22–25. Nothing blocks 5.x but the BLE work in Phases 2–4.

- [ ] **5.1 Example `29-WirelessWorld` (capstone: two radios, zero wires).**
  Two boards, no wire between them (D12 keeps WiFi out): **channel 1
  telemetry over BLE** (`TTransportHost<2, 120>`), **channel 2 commands over LoRa**
  (`TTransportHost<2, 58>`, D8 profile). One world per board, one `FMessagingSystem`
  per board with `MaxMessageBytes = 48` (§4.1 — the LoRa channel is the
  binding constraint), channel composition per the `Modules/MicroWorld/Messaging/AGENTS.md`
  recipes. Client
  board: sensor actor streams readings on telemetry; server board: control
  actor sends a targeted rate-change command on the LoRa channel every 15 s.
  Tag `[ex29]`. README: both radios' safety/security blocks, the
  budget table row explaining `48`, catalog row.

  **Done when:** both envs compile; engine-first grep gate of
  MESSAGING §2.2 returns 0.
  **Verify:** `pio run -d examples/29-WirelessWorld` + ctest.

- [ ] **5.2 (owner-gated) Wireless world hardware checkpoint.** Flash,
  capture both consoles: both channels admitted, telemetry flowing on BLE,
  a LoRa command visibly changing the telemetry rate, ≥ 120 s survival.
  Paste traces + sizes.

  **Done when:** README carries the captured proof.

---

### Phase 6 — Documentation & close-out ⬜

- [ ] **6.1 Documentation sweep.** `Modules/MicroWorld/Platform/Esp32` README/AGENTS: BLE
  device pair + BleAddress rows next to the existing transport tables.
  `docs/Porting.md` (locate the Device section's device list): add BLE. `docs/UE5ConceptMap.md`:
  one row (BLE/LoRa devices ≈ more `UNetDevice` transports — reuse the
  existing wording). `examples/AGENTS.md`: note that radio examples carry
  mandatory safety blocks. If MESSAGING Phase 1 has shipped the log/sleep
  facades by now, sweep examples 17/26/27/28 onto them (§2.2 amendment);
  otherwise record that as a deferred line in section 7.

  **Done when:** `CheckFolderAgents.py` passes; no doc lists the transports
  without BLE/LoRa.
  **Verify:** Standard Verify + folder-agents checker.

- [ ] **6.2 Close the plan.** `examples/README.md` final statuses. Final full
  run: Standard Verify + folder-agents + `pio run` for every example this plan
  touched. The commit that closes the plan is its release record — it names the
  BLE device pair, BleAddress, sdkconfig.ble.defaults, ADR 0004, examples 17 and
  26–29, and E32 LoRa proven on hardware.

  **Done when:** everything green; tracker rows all ✅ (Phase 5 may read
  ⛔ gated if the gate never opened — then say so in this document's tracker and
  close the plan without it; reopening later is a checkbox, not a new plan).

---

## 7. Appendix — common mistakes (read before writing code)

1. **The S3 has no Bluetooth Classic** (D1). Do not plan SPP/serial-profile
   anything; BLE GATT is the only door.
2. **Do not fragment BLE frames** (D5). If the MTU negotiation lands too low,
   the device reports `Unavailable` — fix the sdkconfig/MTU request, not the
   framing.
3. **Never power an E32 without its antenna** — and put the antenna rule in
   every LoRa README; it protects the next student's hardware.
4. **LoRa is slow.** Respect the D8 profile; if peers keep timing out, the
   heartbeat is congesting the channel — lengthen intervals, never shorten.
5. **The shared sdkconfig profile is frozen** (D11). BLE flags go in the
   additive second file; a diff on `esp32-common/sdkconfig.defaults` is an
   automatic review rejection.
6. **Vendor includes never reach public headers** — the include-gate grep is
   part of every Phase 3 verify.
7. **No RNG, no hidden clocks** — connection timeouts count caller-supplied
   milliseconds; retries are deterministic.
8. **`clang-format --style=file:clang-format`** — the policy file has no dot.
9. **Hardware tasks are owner-gated** — building never flashes; capture with
   the reconnecting reader, never `pio device monitor`.
