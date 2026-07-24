# MicroWorld — Radio Transports Roadmap (E32 LoRa & Bluetooth LE)

**Version:** 1.0 · **Date:** 2026-07-24 · **Owner:** Mykola
**Baseline:** `main` at `b4973be` (clean tree), Windows 11 root superbuild + PlatformIO for examples, ESP-IDF 6.0.1 via PlatformIO.
**Scope:** `Modules/PlatformEsp32`, `examples/`, `docs/decisions/`. Portable
packages (`Core`, `Memory`, `Object`, `Engine`, `Net`) are **untouched** —
a radio is just another `INetDriver` (ADR 0003 logic applies unchanged).

**Mission.** Give MicroWorld two working radio links and prove them the same
way the wired links were proven:

1. **E32 LoRa** — the driver (`FEsp32E32LoraDriver`) shipped long ago but no
   example exists and it has never run on hardware. Finish it: volley example,
   hardware verification, full client/server messaging example.
2. **Bluetooth LE** — nothing exists. Design spike (ADR), a
   central/peripheral `INetDriver` pair, volley + messaging examples.
3. **Capstone** — a fully wireless two-board world: BLE + LoRa as two
   channels of one actor-messaging world (gated on the messaging roadmap).

This document is the active plan and progress tracker for that work, written
so that any LLM (including a weak one) can pick it up, find the next task,
complete it, and record progress without extra context. Companions:
`docs/MESSAGING_ROADMAP.md` (active, independent except the Phase 5 gate),
`docs/WIRED_TRANSPORTS_ROADMAP.md` (frozen precedent this plan imitates),
`docs/EXAMPLES_ROADMAP.md` (owns the example scaffold + hardware checkpoint),
`PROGRESS.md` (live evidence record).

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
9. When a phase reaches ✅: add one short evidence entry to `PROGRESS.md`.

Status legend: ⬜ not started · 🟨 in progress · ✅ done · ⛔ blocked/gated

### 1.1 Standard Verify (host edition)

Same as `docs/MESSAGING_ROADMAP.md` §1.1 — run from the repo root for every
task touching `Modules/`:

```sh
clang-format --style=file:clang-format -i <every .h/.cpp file you touched>
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python tools/CheckClassDocumentation.py --root Modules --require-doxygen
```

PlatformEsp32 has no host build: for its files the gate is a line-by-line
re-read plus the ESP32 consumer compile probe (`pio run` in
`Modules/Core/tests/consumer` for the relevant env), exactly as the wired plan
used.

### 1.2 Example Build Verify

```sh
pio run -d examples/<NN-Name>
```

Every environment must compile clean. Compile success is never a runtime
claim.

### 1.3 Hardware checkpoint (owner-gated — never self-serve)

Reuses `docs/EXAMPLES_ROADMAP.md` §1.2 verbatim: building never flashes;
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

- `docs/ROADMAP.md`, `docs/SIMPLICITY_ROADMAP.md`,
  `docs/WIRED_TRANSPORTS_ROADMAP.md` — frozen history.
- The task/catalog sections of `docs/EXAMPLES_ROADMAP.md` and
  `docs/MESSAGING_ROADMAP.md` (each plan tracks its own tasks; shared example
  registration happens **only** in `examples/README.md`).
- Existing `CHANGELOG.md` entries; `Modules/*/benchmarks/Results/*.md`;
  `examples/esp32-common/sdkconfig.defaults` and `partitions.csv` (frozen
  board profile — D11 shows how BLE builds extend it without editing it);
  `LICENSE`; anything under `build/`, `.pio/`, `.git/`.

---

## 2. Ground rules (invariants — never violate)

### 2.1 Inherited embedded invariants (unchanged)

Same as `docs/MESSAGING_ROADMAP.md` §2.1: C++17, no exceptions/RTTI, no
steady-state allocation, no hidden clock (only platform adapters read real
clocks), enum errors with transactional failure, determinism (no RNG),
dependency direction enforced, frozen identity, Doxygen `/** */` on every
declaration and persistent member, `AGENTS.md` in every new folder,
plain-English names (no metaphors, no new abbreviations; `Ble` and `Lora`
join the allowed industry vocabulary alongside `Udp`/`Uart`), format with
`clang-format --style=file:clang-format` (policy file has **no dot**).

### 2.2 Radio-edge rules (new, this plan)

- **Edge-only.** New code lives in `Modules/PlatformEsp32`, `examples/`, and
  `docs/decisions/`. If a task seems to need a portable-package change, stop
  and write `⛔ BLOCKED` — that is a design error in this plan, not a license
  to edit `Net`.
- **All vendor headers stay private.** ESP-IDF/NimBLE includes are confined
  to `src/*PlatformImplementation.h` and `.cpp` files; public headers carry
  only config structs and plain types
  (`rg -n "esp_|nimble|freertos|host/ble" Modules/PlatformEsp32/include` must
  stay 0).
- **Drivers implement the full `INetDriver` contract** (`NetDriver.h:40`):
  non-blocking, at most one transport operation per call, transactional
  receives, `IsOpen()` guard after a `noexcept` constructor — mirror
  `Esp32UartDriver.h` / `Esp32I2cDriver.h` shape for shape.
- **Antenna rule (safety, goes in every LoRa README):** never power an E32
  module without its antenna attached — transmitting into no load can damage
  the RF stage. Keep the two antennas ≥ 0.5 m apart on the bench.
- **Radio-legal note (goes in LoRa READMEs):** bench tests run at the
  module's factory default power on its factory default channel; regional
  regulations are the operator's responsibility.
- **BLE security posture (goes in BLE READMEs):** v1 links are unencrypted,
  unauthenticated Just-Works connections for bench use only (D6).
- Engine-first example rule of `docs/MESSAGING_ROADMAP.md` §2.2 applies to
  every example this plan creates **from Phase 1 onward** — with one
  amendment: until MESSAGING Phase 1 ships the log/sleep facades, examples
  here may use the same `std::printf`/`vTaskDelay` baseline as examples
  18–21, and task 6.1 sweeps them onto the facades if those exist by then.

### 2.3 Decisions record (settled — do not relitigate while executing)

- **D1 — "Bluetooth" means Bluetooth LE.** The ESP32-S3 has **no Bluetooth
  Classic radio** — BLE 5.0 only. Classic (SPP, A2DP…) is permanently out of
  scope for this target. Anyone asking for "Bluetooth serial" gets BLE.
- **D2 — A radio is just another `INetDriver`** (ADR 0003 reasoning).
  Framing reuses the shipped `FrameCodec` byte-pump decoder; sessions reuse
  `TNetHost`; nothing portable changes. That reuse is the entire payoff.
- **D3 — NimBLE is the working assumption for the BLE host stack** (lighter
  than Bluedroid, BLE-only fits D1). The Phase 2 spike confirms or overturns
  this with header/size evidence in ADR 0004; only the ADR may change it.
- **D4 — BLE topology v1 is point-to-point**: one central ↔ one peripheral,
  as a driver *pair* (`FEsp32BleCentralDriver` / `FEsp32BlePeripheralDriver`)
  mirroring the I2C/SPI master/slave precedent — the role asymmetry enters
  only at the platform edge. Multi-peripheral centrals: revisit trigger is a
  real 3-board example need.
- **D5 — One frame = one GATT operation.** The central writes whole encoded
  frames to the RX characteristic (write-without-response); the peripheral
  notifies whole frames on the TX characteristic. The connection MTU is
  negotiated at connect and must cover `BleMaxPayloadBytes + FrameOverheadBytes + 3`;
  if negotiation lands lower, the driver reports `Unavailable` rather than
  fragmenting. No fragmentation layer in v1.
- **D6 — No pairing, no bonding, no encryption in v1.** Bench link only.
  Revisit trigger: any deployment beyond the bench.
- **D7 — E32 modules run factory defaults in transparent mode**, M0 = M1 =
  GND, UART 9600 8N1, factory channel/address. A module-configuration tool
  (AT/command mode) is out of scope; if a module was reconfigured, restore
  factory defaults manually before blaming the driver.
- **D8 — LoRa session profile is `TNetHost<2, 58>` with relaxed timing**:
  `HeartbeatIntervalMilliseconds = 3000`, `PeerTimeoutMilliseconds = 15000`.
  At the E32's default air rate a full frame costs hundreds of milliseconds
  of airtime — the UDP-era 1000/5000 defaults would congest the channel.
- **D9 — Example numbering:** this plan builds the long-reserved
  `17-TwoBoardLora` catalog slot, then takes `26`–`29`
  (26 LoraMessaging, 27 TwoBoardBle, 28 BleMessaging, 29 WirelessWorld).
  Registration happens only in `examples/README.md` (§1.5).
- **D10 — Phase 5 is gated** on `docs/MESSAGING_ROADMAP.md` tracker showing
  **Phases 3 and 4 ✅** (actor messaging over a wire, plus `TNetworkFrameSet`
  — example 29 always composes two channels). Do not start 5.x before that;
  everything in Phases 0–4 here uses only shipped API (`TNetHost`,
  `TNetHostFrame`, `TEngineHost`).
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
| UART-attached radio driver (this is the LoRa driver's own shape) | `Modules/PlatformEsp32/.../Esp32E32LoraDriver.h` + `src/E32UartPlatformImplementation.h` |
| Role-asymmetric driver pair + ISR-side inbox ring | `Modules/PlatformEsp32/.../Esp32I2cDriver.h` (`FI2cReceiveInbox`) |
| Per-driver 1-byte address codec | `Modules/PlatformEsp32/.../LoraAddress.h`, `UartAddress.h` |
| Design-spike ADR with header-derived answers | `docs/decisions/0003-wired-transports.md` Appendices A/B |
| Driver volley example | `examples/18-TwoBoardUart` |
| Full TNetHost + engine messaging example | `examples/19-UartMessaging` |
| Two-link, one-world composition (Phase 5 only) | `docs/MESSAGING_ROADMAP.md` §4.4 recipes |

---

## 3. What exists today (verified at `b4973be` — the map)

**LoRa: driver yes, proof no.** `FEsp32E32LoraDriver`
(`Esp32E32LoraDriver.h:51`): config `{UartPort, TxGpio, RxGpio,
BaudRate{9600}, LocalNodeId}`, `E32MaxPayloadBytes = 58`, frames via
`TFrameDecoder<58>`, address codec `LoraAddress.h`
(`MakeLoraAddress`/`IsLoraAddress`/`LoraAddressNodeId`). Compile-verified
only: **no example uses it, it has never been flashed**, and the catalog row
`17-TwoBoardLora` has sat ⬜ since the examples plan was written.

**Bluetooth: nothing.** No BLE code anywhere in the repo; the shared
`sdkconfig.defaults` does not enable the BT controller. The ESP32-S3-WROOM-1
modules on the rig support BLE 5.0 (and only BLE — D1). No extra hardware is
needed for BLE.

**Everything above the driver seam is ready and battle-tested** on UDP, UART,
I2C, and SPI: `FrameCodec` (CRC-16, resync), `TNetManager`, `TNetHost`
(roles/peers/channels/heartbeats), `TNetHostFrame` → `TEngineHost`, and four
verified two-board examples (18–21) plus two WiFi ones (15–16) to copy from.

**Hardware on the desk:** 2 × ESP32-S3-WROOM-1-N16R8 boards (USB-JTAG on
COM5/COM7), 2 × E32 LoRa modules with antennas. BLE needs nothing extra.

---

## 4. Target design (read before Phase 2)

### 4.1 LoRa — nothing new to design

The driver, address codec, and payload cap exist. The work is examples +
hardware proof + the D8 session profile. Payload budget (mirrors the
messaging plan's table):

| Layer | LoRa budget |
| --- | --- |
| Driver frame payload (`E32MaxPayloadBytes`) | 58 |
| `TNetHost` message payload (−4 header) | 54 |
| Encoded actor message, best-effort (Phase 5) | 54 (payload ≤ 48) |
| Encoded actor message, guaranteed (−3) | 51 (payload ≤ 45) |

Phase 5 therefore uses `MaxMessageBytes = 48` on any router whose world
includes a LoRa channel.

### 4.2 BLE — target shape (spike-confirmed; only ADR 0004 may change it)

**Link model (D4/D5):** one custom 128-bit GATT service (the Nordic-UART
pattern, own UUIDs fixed by the spike) with two characteristics: **RX**
(central writes encoded frames, write-without-response) and **TX**
(peripheral notifies encoded frames). Both directions carry the same
`FrameCodec` frames as every other transport; received bytes are pumped
through `TFrameDecoder` exactly like the UART driver, so partial deliveries
are already handled. MTU is requested at connect to cover one whole frame.

**Public header target** (`Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32BleDriver.h`):

```cpp
/** Largest payload one BLE frame carries; uniform with the wired transports. */
inline constexpr std::size_t BleMaxPayloadBytes = 120;

/** Settings for the advertising (peripheral) side of the point-to-point link. */
struct FEsp32BlePeripheralConfig
{
    const char* DeviceName;          // advertised name the central scans for
    std::uint8_t LocalNodeId;        // frame identity, mirrors the wired drivers
};

/** Settings for the scanning (central) side of the point-to-point link. */
struct FEsp32BleCentralConfig
{
    const char* PeerDeviceName;      // peripheral to connect to
    DurationMilliseconds ConnectTimeoutMilliseconds{15000};
    std::uint8_t LocalNodeId;
};

/** Peripheral-role BLE transport: advertises, accepts one central. */
class FEsp32BlePeripheralDriver final : public INetDriver
{
public:
    explicit FEsp32BlePeripheralDriver(const FEsp32BlePeripheralConfig& Config) noexcept;
    bool IsOpen() const noexcept;        // stack up, service registered, advertising
    bool IsConnected() const noexcept;   // a central is connected and MTU covers one frame
    // INetDriver: TrySend = one notify of one encoded frame (Unavailable until connected);
    //             TryReceive = drain inbox ring through the decoder, one frame max;
    //             MaxPacketBytes = BleMaxPayloadBytes.
};

/** Central-role BLE transport: scans, connects to one named peripheral. */
class FEsp32BleCentralDriver final : public INetDriver
{
public:
    explicit FEsp32BleCentralDriver(const FEsp32BleCentralConfig& Config) noexcept;
    bool IsOpen() const noexcept;
    bool IsConnected() const noexcept;
    // INetDriver: TrySend = one write-without-response; TryReceive as above.
};
```

Plus `BleAddress.h` (`MakeBleAddress`/`IsBleAddress`/`BleAddressNodeId`,
1-byte node id, per-driver codec duplication is the package precedent).
NimBLE callbacks run on the host task → they push raw bytes into an SPSC
inbox ring (imitate `FI2cReceiveInbox`); `TryReceive` pops and pumps the
decoder on the caller's thread. Reconnect policy v1: a dropped connection
flips `IsConnected()` false and the drivers resume
advertising/scanning internally; sends meanwhile return `Unavailable`
(callers already handle that — it is the standard driver contract).

**Spike questions ADR 0004 must answer with evidence** (ESP-IDF 6.0.1
headers/docs, imitate ADR 0003's appendices): NimBLE vs Bluedroid final call
(flash/RAM cost figures); exact sdkconfig flag set for D11's
`sdkconfig.ble.defaults`; service/characteristic UUIDs; achievable MTU
between two S3s and the request sequence; write-without-response vs write
throughput/backpressure (what does "driver `Full`" map to); connection
supervision timeout vs `TNetHost` heartbeat profile for BLE; how the consumer
compile probe builds with BT enabled without touching the shared profile.

### 4.3 Session profiles per radio

| | LoRa (D8) | BLE |
| --- | --- | --- |
| `TNetHost` | `TNetHost<2, 58>` | `TNetHost<2, 120>` |
| Heartbeat / timeout ms | 3000 / 15000 | 1000 / 5000 (spike may adjust) |
| Server address | `MakeLoraAddress(<node>)` | `MakeBleAddress(<node>)` |

---

## 5. Progress tracker

| Phase | Title | Tasks | Status |
| --- | --- | --- | --- |
| 0 | Baseline & governance | 2 | ✅ |
| 1 | LoRa proven on hardware | 4 | 🟨 |
| 2 | Bluetooth LE design spike (ADR 0004) | 1 | ⬜ |
| 3 | BLE driver pair | 2 | ⬜ |
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

Goal: the four-year-old catalog hole closes — the E32 driver runs on the
bench, first as a raw volley, then under the full engine.

- [x] **1.1 Example `17-TwoBoardLora` (driver volley).** Copy example 18's
  entire shape (`examples/18-TwoBoardUart` — one `Main.cpp`, two role envs
  via `-DMICROWORLD_EXAMPLE_NODE_ID=1|2`, counter ping-pong, `[ex17]` tag)
  with these substitutions: driver `FEsp32E32LoraDriver`, config
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
  swapped to `FEsp32E32LoraDriver` (config `UartPort 1 / TxGpio 17 / RxGpio 18 /
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

- [ ] **1.2 (owner-gated) LoRa volley hardware checkpoint.** Flash node 1 and
  node 2, capture both consoles per §1.3 (`mwlog.py`, USB-JTAG). Expect the
  example-18-shaped alternating counter trace. Paste both traces into the
  README's "Verified output"; record image sizes. If the link is dead, check
  in order: antennas, M0/M1 strapping, factory-default channel/address (D7),
  TX/RX swap.

  **Done when:** README carries real captured traces from both boards.

- [ ] **1.3 Example `26-LoraMessaging` (the payoff demo).** Copy example 19's
  shape (`examples/19-UartMessaging` — server board: `TEngineHost` +
  `TNetHost` DedicatedServer + `TNetHostFrame`, channel-1 message spawns an
  actor, channel-2 state broadcast; client board: bare client `TNetHost`)
  with the D8/§4.3 LoRa profile: `TNetHost<2, 58>`, heartbeat 3000 /
  timeout 15000, `ServerAddress = MakeLoraAddress(1)`, payloads trimmed to
  the §4.1 budget (state broadcast stays 2 bytes — fits trivially), pacing
  ≥ 1000 ms, tag `[ex26]`. README: same wiring/safety blocks as 17 plus the
  airtime paragraph (why the relaxed profile); catalog row appended.

  **Done when:** both envs compile; README/AGENTS complete; catalog updated.
  **Verify:** `pio run -d examples/26-LoraMessaging` + ctest.

- [ ] **1.4 (owner-gated) LoRa messaging hardware checkpoint.** Flash, capture
  both consoles: expect Hello/Welcome admission, a channel-1 spawn on the
  server world, periodic channel-2 state lines on the client, heartbeat
  survival over ≥ 60 s. Paste traces; record sizes.

  **Done when:** README carries the captured two-console trace.

---

### Phase 2 — Bluetooth LE design spike ⬜

- [ ] **2.1 Write `docs/decisions/0004-bluetooth-le-transport.md`.** Status
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

### Phase 3 — BLE driver pair ⬜

- [ ] **3.1 `FEsp32BlePeripheralDriver` + `FEsp32BleCentralDriver` +
  `BleAddress.h`.** Implement §4.2 as confirmed by ADR 0004: public header
  `Esp32BleDriver.h` (configs + both driver classes, full Doxygen), private
  `src/BlePlatformImplementation.h` + `src/Esp32BleDriver.cpp` (all
  NimBLE/ESP-IDF includes confined there), inbox ring imitating
  `FI2cReceiveInbox`, decoder pump imitating the UART driver's receive path,
  `BleAddress.h` mirroring `LoraAddress.h`'s three functions. Non-copy,
  non-move, `noexcept` constructors, `IsOpen()`/`IsConnected()` guards,
  transactional receives. Update `library.json`/CMake lists if they
  enumerate sources; folder `AGENTS.md` guides.

  **Done when:** vendor-include gate 0 (`rg -n "esp_|nimble|freertos|host/ble"
  Modules/PlatformEsp32/include` → 0); every declaration documented;
  line-by-line re-read recorded in the evidence line (no host build exists).
  **Verify:** Standard Verify + the include-gate grep. Compile proof
  deliberately lands in task 3.2 (the probe needs 3.2's sdkconfig plumbing) —
  a driver nobody compiles is not done, so the phase closes only when 3.2's
  probe builds these classes.

- [ ] **3.2 BLE build plumbing + compile probe.** Create
  `examples/esp32-common/sdkconfig.ble.defaults` with exactly the ADR 0004
  flag set (D11 — the shared `sdkconfig.defaults` is untouched). Extend the
  ESP32 consumer project (`Modules/Core/tests/consumer`) with a `ble` env
  that layers both defaults files and compiles a minimal probe instantiating
  both drivers (imitate how the existing Net/Engine ESP32 probes are wired).
  Document the two-file `SDKCONFIG_DEFAULTS` mechanism in
  `examples/esp32-common/AGENTS.md`.

  **Done when:** the probe env compiles both drivers under the strict flags;
  the frozen shared profile is byte-identical to before.
  **Verify:** `pio run` (consumer `ble` env) + `git -C . diff --stat
  examples/esp32-common/sdkconfig.defaults` shows no change + Standard
  Verify.

---

### Phase 4 — BLE examples on hardware ⬜

- [ ] **4.1 Example `27-TwoBoardBle` (driver volley).** Example 18's shape
  again: peripheral env (`-DMICROWORLD_EXAMPLE_PERIPHERAL=1`, node id 1,
  `DeviceName "microworld-ex27"`) and central env (`=0`, node id 2,
  `PeerDeviceName` matching). Volley starts only when both sides report
  `IsConnected()`; pacing 500 ms; tag `[ex27]`. `platformio.ini` layers the
  D11 defaults pair. README: no wiring at all (first wireless wired-shaped
  example — say so), the §2.2 BLE security posture verbatim, connection
  troubleshooting (name mismatch, MTU too low → driver `Unavailable`).
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
  client `TNetHost` on the central, `TNetHost<2, 120>` both sides, §4.3
  profile, tag `[ex28]`. README + AGENTS + catalog row.

  **Done when:** both envs compile.
  **Verify:** `pio run -d examples/28-BleMessaging` + ctest.

- [ ] **4.4 (owner-gated) BLE messaging hardware checkpoint.** Flash, capture:
  admission, channel-1 spawn, channel-2 state stream, ≥ 60 s heartbeat
  survival. Paste traces + sizes.

  **Done when:** README carries the captured two-console trace.

---

### Phase 5 — Wireless actor-messaging world ⛔ (gated — D10)

**Gate:** do not start until `docs/MESSAGING_ROADMAP.md` §5 tracker shows
Phase 3 ✅ (actor messaging over one wire shipped: `TMessageRouter`,
`TMessageChannelBinding`, and — if its Phase 4 is also ✅ —
`TNetworkFrameSet`; if Phase 4 is not done yet, this phase is blocked on it
too, since two channels need the frame set). Record the gate check in the
evidence line.

- [ ] **5.1 Example `29-WirelessWorld` (capstone: two radios, zero wires).**
  Two boards, no wire between them (D12 keeps WiFi out): **channel 1
  telemetry over BLE** (`TNetHost<2, 120>`), **channel 2 commands over LoRa**
  (`TNetHost<2, 58>`, D8 profile). One world per board, one `TMessageRouter`
  per board with `MaxMessageBytes = 48` (§4.1 — the LoRa channel is the
  binding constraint), frame-set composition per
  `docs/MESSAGING_ROADMAP.md` §4.4 (net frames first, router last). Client
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

- [ ] **6.1 Documentation sweep.** `Modules/PlatformEsp32` README/AGENTS: BLE
  driver pair + BleAddress rows next to the existing transport tables.
  `docs/Porting.md` (locate the seam-2 driver list): add BLE. `docs/UE5ConceptMap.md`:
  one row (BLE/LoRa drivers ≈ more `UNetDriver` transports — reuse the
  existing wording). `examples/AGENTS.md`: note that radio examples carry
  mandatory safety blocks. If MESSAGING Phase 1 has shipped the log/sleep
  facades by now, sweep examples 17/26/27/28 onto them (§2.2 amendment);
  otherwise record that as a deferred line in section 7.

  **Done when:** `CheckFolderAgents.py` passes; no doc lists the transports
  without BLE/LoRa.
  **Verify:** Standard Verify + folder-agents checker.

- [ ] **6.2 Release bookkeeping.** `CHANGELOG.md` entry (added: BLE driver
  pair, BleAddress, sdkconfig.ble.defaults, ADR 0004, examples 17, 26–29;
  proven: E32 LoRa on hardware). `examples/README.md` final statuses.
  `PROGRESS.md` phase evidence lines. Final full run: Standard Verify +
  folder-agents + `pio run` for every example this plan touched.

  **Done when:** everything green; tracker rows all ✅ (Phase 5 may read
  ⛔ gated if the gate never opened — then say so in PROGRESS.md and close
  the plan without it; reopening later is a checkbox, not a new plan).

---

## 7. Appendix — common mistakes (read before writing code)

1. **The S3 has no Bluetooth Classic** (D1). Do not plan SPP/serial-profile
   anything; BLE GATT is the only door.
2. **Do not fragment BLE frames** (D5). If the MTU negotiation lands too low,
   the driver reports `Unavailable` — fix the sdkconfig/MTU request, not the
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
