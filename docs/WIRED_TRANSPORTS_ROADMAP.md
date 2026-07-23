# MicroWorld — Wired Transports Roadmap

**Version:** 1.0 · **Date:** 2026-07-23 · **Owner:** Mykola
**Baseline:** commit `050f466` plus `docs/EXAMPLES_ROADMAP.md`.
**Target hardware:** 2 × ESP32-S3-DevKitC-1, jumper wires; Phase 2 additionally
wants two ~4.7 kΩ pull-up resistors (I2C).
**Prerequisite:** `docs/EXAMPLES_ROADMAP.md` Phase 0 and task 1.1
(`01-CoreTick`) are ✅ — they provide the proven PlatformIO example scaffold
**and** the ESP32 compile gate this roadmap relies on (§1.1).

MicroWorld should make board-to-board communication over plain wires — UART,
I2C, SPI — as easy as it already is over WiFi UDP and E32 LoRa. This document
is the active plan and progress tracker for that.

**The design decision this plan implements** (Phase 0 records it as an ADR):

- Wired board-to-board links are **network transports**. Each one is a new
  `INetDriver` implementation inside `Modules/PlatformEsp32`, exactly like
  `FEsp32UdpDriver` and `FEsp32E32LoraDriver`. Nothing portable changes: the
  byte I/O, frame codec, `TNetManager` FIFO, and `TNetHost` channel/message
  design run over a wire **unchanged** — that is the entire payoff.
- Peripheral-bus **device access** (reading a sensor over I2C, driving a
  display over SPI) is a different problem — master-driven register traffic,
  not peer messaging. It is explicitly **out of scope** here; if MicroWorld
  ever needs it, it becomes its own clean system behind its own seam, designed
  then, not now.

What "easy" looks like when this roadmap is done — the only line that changes
between a WiFi app and a wired app is the driver construction:

```cpp
// WiFi UDP (today):
static MicroWorld::FEsp32UdpDriver Driver{40404};
// Wired UART (Phase 1):
static MicroWorld::FEsp32UartDriver Driver{{.UartPort = 1, .TxGpio = 17, .RxGpio = 18, .BaudRate = 115200, .LocalNodeId = 1}};

// Everything above the driver is identical either way:
static MicroWorld::TNetPacketStorage<8, 120> Storage{};
static MicroWorld::TNetManager<8, 120> Manager{Driver, Storage};
```

It is written so that any LLM (including a weak one) can pick it up, find the
next task, complete it, and record progress without extra context. Companion
documents: `docs/EXAMPLES_ROADMAP.md` (scaffold §3 and hardware-checkpoint
protocol §1.2 are reused by reference), `docs/Porting.md` (seam 2 — the
`INetDriver` contract every driver here implements), `PROGRESS.md` (live
evidence record), `docs/SIMPLICITY_ROADMAP.md` §1 (the protocol style this
document follows).

---

## 1. How to use this document (protocol for LLM workers)

Rules 1–9 of `docs/EXAMPLES_ROADMAP.md` §1 apply verbatim, with two changes:

- **This roadmap MAY edit `Modules/PlatformEsp32` and `docs/`** — that is its
  job. It must NOT edit any portable package (`Core`, `Memory`, `Object`,
  `Engine`, `Net`) or `Modules/PlatformHost`. If a task appears to need a
  portable change, that is a design defect: write `⛔ BLOCKED`, describe the
  needed change in one sentence, and stop for human review.
- Driver tasks and example tasks alternate: a driver task must be **Built**
  before its example task starts; an example must be **Built** before its
  hardware checkpoint. Hardware checkpoints stay human-gated
  (`docs/EXAMPLES_ROADMAP.md` §1.2) and may be batched per phase.

Status legend: ⬜ not started · 🟨 in progress · ✅ done · ⛔ blocked

### 1.1 Standard Verify (wired edition)

Run from the repository root after every task that touches code:

```sh
clang-format --style=file:clang-format -i <every .h/.cpp file you touched>
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python tools/CheckClassDocumentation.py --root Modules --require-doxygen
python tools/CheckFolderAgents.py --root Modules --exclude build --exclude .pio --exclude __pycache__
pio run -d examples/01-CoreTick
```

**The `pio run` line is the load-bearing compile gate.** The root CMake
superbuild deliberately excludes `PlatformEsp32` (it is PlatformIO/ESP-IDF
only), so `ctest` alone never compiles a new driver. Example `01-CoreTick`
links the whole `PlatformEsp32` package (`lib_deps` + `chain+`), so rebuilding
it compiles every driver source in the package — including the one you just
added. Expect `[SUCCESS]`; treat any warning as a defect.

### 1.2 Runtime claims

Compile success is a compile-only proof (`Modules/PlatformEsp32/AGENTS.md`).
Every new driver carries "UNVERIFIED at runtime" wording in its
platform-implementation comments until its example's hardware checkpoint
passes, then that wording is updated in the same commit that records the
checkpoint — the E32 driver's Phase 5.3 → 6.2 history is the model.

---

## 2. Ground rules (invariants — never violate)

### 2.1 Edge-only, seam-shaped

- New code lives in `Modules/PlatformEsp32` (`include/MicroWorld/PlatformEsp32/`
  and `src/`) and in `examples/`. Portable packages are frozen.
- ESP-IDF headers (`driver/uart.h`, `driver/i2c_*.h`, `driver/spi_*.h`) are
  confined to private `src/*PlatformImplementation.h` headers — the pattern
  `E32UartPlatformImplementation.h` establishes. Public headers stay
  platform-neutral (plain-integer configs, exactly like `FEsp32E32LoraConfig`).
- Every driver implements the full `INetDriver` contract of
  `docs/Porting.md` seam 2: non-blocking, transactional (`Full` / `Invalid` /
  `Unavailable` leave caller outputs unchanged), one transport operation per
  call, bounded receive pumps, no clock, no thread, no retry, no session.

### 2.2 Package identity is frozen

CMake/PlatformIO names, `library.json` identity, and the existing header
layout stay untouched. New headers join `include/MicroWorld/PlatformEsp32/`;
new sources join `src/`. The scoped `AGENTS.md` files that enumerate the
package's adapters (`Modules/PlatformEsp32/AGENTS.md` and the `include/…/`
and `src/` guides) must gain the new drivers in the same commit that adds
them — `CheckFolderAgents.py` keeps the guides present, humans keep them true.

### 2.3 Style, docs, formatting

Same contract as all module code: C++17, no exceptions, no RTTI, no heap in
steady state, fixed-width value types, every function and persistent variable
documented with its *why*, `clang-format --style=file:clang-format`. Driver
public headers must pass `CheckClassDocumentation.py`.

### 2.4 Wiring safety (goes in every wired example README)

- ESP32-S3 GPIO is **3.3 V logic** — never feed 5 V into a data pin.
- Always connect **GND↔GND first**; two boards without a common ground do not
  have a signal.
- Rewire only with both boards unpowered.

### 2.5 Naming

Driver names mirror the shipped pattern: `FEsp32UartDriver`,
`FEsp32I2cMasterDriver`, `FEsp32I2cSlaveDriver`, `FEsp32SpiMasterDriver`,
`FEsp32SpiSlaveDriver`; config structs `FEsp32<X>Config`; address helpers
`<X>Address.h` mirroring `LoraAddress.h`/`UdpAddress.h`. Plain language
everywhere; no metaphor names.

### 2.6 Files you must never edit

Everything in `docs/EXAMPLES_ROADMAP.md` §2.7 **except** the `Modules/`
blanket rule, which this roadmap narrows to: portable packages and
`PlatformHost` are read-only; `PlatformEsp32` is writable. `docs/EXAMPLES_ROADMAP.md`
itself is also read-only (its own protocol forbids structural edits) — new
example rows go into the living catalog `examples/README.md` instead.

---

## 3. Design (read before Phase 1)

### 3.1 What already exists and is reused as-is

| Piece | Where | Reused how |
| --- | --- | --- |
| Frame codec (magic, node id, length, CRC-16, resync) | `Net/FrameCodec.h` | every wired driver's wire format |
| Bounded byte-pump decoder | `TFrameDecoder<MaxPayloadBytes>` | held by value in each driver |
| UART open/write/read/close toolkit | `src/E32UartPlatformImplementation.h` (`OpenConfiguredUartPort`, `WriteUart`, `ReadUartByte`, `CloseUart`) | shared by the new UART driver (task 1.1 updates its header comment naming both consumers) |
| One-byte node-id address shape | `LoraAddress.h` | pattern for `UartAddress.h` (per-driver codec, duplication at the edge is the package's precedent) |
| Everything above the driver | `TNetManager`, `TNetHost`, `NetProtocol.h` | unchanged — the point of the plan |

### 3.2 Per-transport contract summary

| | UART (Phase 1) | I2C (Phase 2) | SPI (Phase 3) |
| --- | --- | --- | --- |
| Topology | point-to-point, full duplex | master + slave, master-driven | master + slave, master-clocked |
| Symmetric firmware? | yes — one driver class | no — master/slave driver pair | no — master/slave driver pair |
| Wire format | FrameCodec frames on the byte stream | FrameCodec frames inside bus transactions | FrameCodec frames inside DMA transactions |
| Address encoding | 1-byte node id (`UartAddress.h`) | 7-bit slave address (`I2cAddress.h`, spike decides layout) | 1-byte node id (`SpiAddress.h`, spike decides) |
| Payload cap | `UartMaxPayloadBytes = 120` | spike decides | spike decides |
| Design risk | low — E32 minus the radio | medium — slave TX queuing semantics | medium — slave must pre-queue DMA transactions |

Why 120 bytes for UART: one whole frame (payload + ~6 framing bytes) must fit
inside the RX ring buffer the shared open helper installs (2 × the hardware
FIFO ≈ 256 bytes) with room for a second frame arriving while the first is
pumped at the 10 ms example pacing; at the default 115200 baud that is ~115
raw bytes per 10 ms, so 120 leaves honest headroom instead of a tight fit.

### 3.3 Why I2C and SPI get a design-spike task first

Their ESP-IDF 6 slave-side APIs (transaction queuing, buffer semantics,
callback context) have **not** been exercised anywhere in this repository, so
task specs written today would be guesses. Each phase therefore opens with a
bounded spike whose deliverable is an ADR appendix answering the listed
questions — after that, the driver tasks are ordinary work. This is the same
honesty rule the repo applies everywhere: never claim what was not verified.

---

## 4. Progress tracker

| Phase | Content | Status |
| --- | --- | --- |
| 0 | ADR: wired links are Net transports; device-bus access is out of scope | ✅ |
| 1 | UART link driver + examples 18, 19 | ✅ |
| 2 | I2C spike, master/slave drivers + example 20 | ✅ |
| 3 | SPI spike, master/slave drivers + example 21 | ⬜ |
| 4 | Close-out: docs, catalog, changelog | ⬜ |

Each phase after 0 delivers standalone value — the plan can pause after any ✅
phase with nothing half-built.

---

## 5. Phases and tasks

### Phase 0 — Decision record

#### Task 0.1 — Write `docs/decisions/0003-wired-transports.md`

- [x] Done

Done 2026-07-23 — ADR 0003 written in the `0001`/`0002` format (context, decision, consequences, alternatives, revisit triggers), referencing `docs/Porting.md` seam 2; it names the boundary in one quotable sentence ("a wire between two boards is just another `INetDriver`; talking to a chip on a bus is a different system we have not built"). `PROGRESS.md` records the wired plan start; docs-only, superbuild + `ctest` stay green at 11/11.

**Steps:** write the ADR in the same format as `0001`/`0002`: context (two
boards, wired links wanted, message design exists), decision (the two bullets
from this document's header — transports in `PlatformEsp32` behind
`INetDriver`; device-bus access explicitly deferred as a future separate
system), consequences (portable packages untouched; master/slave asymmetry
enters at the platform edge only; each wired example needs two boards).
Reference `docs/Porting.md` seam 2. Add the `PROGRESS.md` line (same-commit
rule) recording that the wired plan started.

**Done when:** ADR exists and names the boundary in one sentence a student can
quote. **Verify:** §1.1 (docs-only — the build/gates simply stay green).

---

### Phase 1 — UART link

#### Task 1.1 — `FEsp32UartDriver` + `UartAddress.h`

- [x] Built (compile gate)
- [ ] Hardware-verified (via task 1.2's checkpoint)

Built 2026-07-23 — `UartAddress.h` (1-byte point-to-point node id, mirroring `LoraAddress.h`), `Esp32UartDriver.h` (`UartMaxPayloadBytes = 120`, `FEsp32UartConfig` with `BaudRate` default 115200, `FEsp32UartDriver final : INetDriver`), and `Esp32UartDriver.cpp` (mirrors `Esp32E32LoraDriver.cpp` one-for-one; same `ENetResult` mapping; reuses the shared `E32UartPlatformImplementation.h` toolkit). Shared toolkit header comment now names both consumers; the three scoped `AGENTS.md` guides enumerate the new driver. Standard Verify §1.1 green: `pio run` `[SUCCESS]` (`Esp32UartDriver.o` compiled, warning-clean save the vendor `#include_next` pedantic warnings), ctest 11/11, class-doc 128 files, folder-agents 63. Send/receive path UNVERIFIED at runtime until task 1.2's checkpoint (§1.2). Hardware-verified pending that checkpoint.

**Goal:** a wired point-to-point `INetDriver` over one UART — functionally
"the E32 driver minus the radio".

**Files to create/edit:**

- `include/MicroWorld/PlatformEsp32/UartAddress.h` — `MakeUartAddress` /
  `IsUartAddress` / `UartAddressNodeId`, mirroring `LoraAddress.h`'s three
  constexpr helpers and doc style (1-byte sender node id; point-to-point wire,
  so the address never routes — it stamps and reports identity).
- `include/MicroWorld/PlatformEsp32/Esp32UartDriver.h` —
  `UartMaxPayloadBytes = 120` (§3.2 why), `FEsp32UartConfig` (same five
  plain-integer fields as `FEsp32E32LoraConfig`, `BaudRate` defaulting to
  115200), `class FEsp32UartDriver final : public INetDriver` with the exact
  member/documentation shape of `Esp32E32LoraDriver.h` (constructor
  opens/rolls back, non-copyable/non-movable, `TrySend`, `TryReceive`,
  `MaxPacketBytes`, `IsOpen`, held-by-value `TFrameDecoder<UartMaxPayloadBytes>`).
- `src/Esp32UartDriver.cpp` — mirror `Esp32E32LoraDriver.cpp`'s flow
  one-for-one (validate → `EncodeFrame` → `WriteUart`; bounded byte pump →
  decoder → deliver/hold), including its `ENetResult` mapping. Include the
  shared `E32UartPlatformImplementation.h` toolkit.
- `src/E32UartPlatformImplementation.h` — update the header comment: it is no
  longer included by one driver; name both consumers and keep the
  UNVERIFIED-at-runtime wording accurate per §1.2.
- `Modules/PlatformEsp32/AGENTS.md` + the `include/…`/`src/` scoped guides —
  add the UART driver to the adapter enumerations (§2.2).

**Reference code:** `Esp32E32LoraDriver.h/.cpp` (the template),
`E32UartPlatformImplementation.h` (the toolkit), `LoraAddress.h` (the codec
shape).

**Done when:** all files exist; every public symbol documented; scoped guides
updated; Standard Verify §1.1 passes (including the `pio run` compile gate).

#### Task 1.2 — Example `18-TwoBoardUart` (ping-pong counter)

- [x] Built
- [ ] Hardware-verified

Built 2026-07-23 — `examples/18-TwoBoardUart` ping-pongs a `std::uint32_t` counter over the wired UART driver directly (no `TNetManager`): node 1 seeds, both poll `TryReceive` and reply `counter + 1` every 500 ms, 5-byte payload `[senderId][counter BE]`, roles by `-DMICROWORLD_EXAMPLE_NODE_ID=1|2`, destinations via `MakeUartAddress`. Build Verify §1.1 green: `pio run` builds **both** role envs `[SUCCESS]` (example warning-clean; identical images RAM 20,892 B / 6.4%, Flash 219,941 B / 5.2%), ctest 11/11, class-doc 128, folder-agents 63. Hardware checkpoint pending (human-gated §1.2); it also flips task 1.1's Hardware-verified box.

**Feature:** the same volley as example `17-TwoBoardLora`, over a wire — proof
that swapping the driver line swaps the transport.

**Spec:** copy the `17-TwoBoardLora` task spec from `docs/EXAMPLES_ROADMAP.md`
(task 7.2) with these substitutions — driver `FEsp32UartDriver`, config
`{UartPort = 1, TxGpio = 17, RxGpio = 18, BaudRate = 115200, LocalNodeId = 1|2}`,
tag `[ex18]`, volley period 500 ms (wire is fast and lossless), payloads via
`MakeUartAddress`. Role environments `-DMICROWORLD_EXAMPLE_NODE_ID=1|2` as in
task 7.1's pattern. Scaffold, README template, and Build Verify come from
`docs/EXAMPLES_ROADMAP.md` §3/§1.1; add the catalog row to
`examples/README.md`.

**Wiring (README table, plus §2.4 safety lines):**

| Board A | Board B | Why |
| --- | --- | --- |
| GND | GND | common ground first |
| GPIO 17 (TX) | GPIO 18 (RX) | A talks to B |
| GPIO 18 (RX) | GPIO 17 (TX) | B talks to A |

**Hardware checkpoint:** both monitors show the counter climbing alternately
with no stalls (unlike LoRa, a wired link losing frames at 30 cm is a defect,
not weather — investigate before checking the box). This checkpoint also
flips task 1.1's Hardware-verified box and its UNVERIFIED comment wording
(§1.2).

#### Task 1.3 — Example `19-UartMessaging` (the payoff demo)

- [x] Built
- [ ] Hardware-verified

Built 2026-07-23 — `examples/19-UartMessaging` runs example 16's full `TNetHost` client/server message design over `FEsp32UartDriver` with zero WiFi (no `WifiStation`/`NetworkConfig`/`esp_netif_init`): server = `TEngineHost` + `TNetHostFrame` + `TNetHost` (DedicatedServer, node 1) broadcasting the world actor count on channel 2 and spawning on channel-1 requests; client = bare `TNetHost` (Client, node 2) greeting `MakeUartAddress(1)`, sending two spawn requests, observing the count reach 2. Roles by `-DMICROWORLD_EXAMPLE_SERVER=1|0`; split into `Main.cpp` dispatcher + always-compiled `ServerMain.cpp`/`ClientMain.cpp` + shared `UartMessagingShared.h` (example-16 structure, each file under the ~200-line cap). `TNetHost` sized `MaxPacketBytes = 120` to stay within the UART driver cap. Build Verify §1.1 green: `pio run` builds **both** role envs `[SUCCESS]` (warning-clean; server RAM 25,820 B / 7.9%, Flash 235,761 B / 5.6%; client RAM 21,980 B / 6.7%, Flash 224,997 B / 5.4%), ctest 11/11, class-doc 128, folder-agents 63. Hardware checkpoint pending (§1.2): the client trace must show the actor count reaching 2.

**Feature:** the full message design over the wire — example 16's
`TNetHost` client/server spawn protocol with **zero WiFi**: server board runs
`TEngineHost` + `TNetHostFrame` + `TNetHost` (`DedicatedServer`) over
`FEsp32UartDriver`; client board runs the bare client `TNetHost` over the same
driver type.

**Spec:** copy the `16-TwoBoardUdp` task spec (task 7.1) with these
substitutions — no `WifiStation`/`NetworkConfig` files at all (delete those
steps; that is the demonstration), drivers constructed as in task 1.2, server
address on the client = `MakeUartAddress(1)`, tag `[ex19]`, roles selected by
`-DMICROWORLD_EXAMPLE_SERVER=1|0`. The README's headline sentence: *"same
application protocol as example 16 — only the driver construction changed."*

**Hardware checkpoint:** client trace shows the actor count reaching 2, as in
example 16, with the WiFi router switched off if you want to make the point.

---

### Phase 2 — I2C link

#### Task 2.1 — I2C design spike (ADR appendix)

- [x] Done

Done 2026-07-23 — Appendix A appended to `docs/decisions/0003-wired-transports.md` answering all six questions from the installed ESP-IDF 6.0.1 headers (`i2c_master.h`, `i2c_slave.h`, `i2c_types.h`): the slave pre-queues TX via `i2c_slave_write` (no ISR needed) and receives **only** through the `on_receive` ISR callback (no poll API in 6.0.1); master reads are clock-driven, so `Unavailable` is decided by the decoder finding no frame, not by the API; 100 kHz with mandatory external ~4.7 kΩ pull-ups; `I2cAddress.h` = 1-byte node id like `UartAddress.h`, with the 7-bit `SlaveAddress` a separate config field; a fixed whole-frame transaction window pumped through the shared `TFrameDecoder`; `I2cMaxPayloadBytes = 120`, uniform with UART. The `SPIKE:` blanks in tasks 2.2/2.3 are filled from these answers; SDA = GPIO 8 / SCL = GPIO 9 confirmed clear of the S3 strapping pins (0, 3, 45, 46). Header-derived and runtime-UNVERIFIED until example 20's checkpoint (§1.2); docs-only, superbuild + ctest stay green at 11/11.

**Goal:** replace guesses with verified answers before any driver code.
Deliverable: an appendix in `docs/decisions/0003-wired-transports.md` +
updated specs in tasks 2.2/2.3 (filling their marked `SPIKE:` blanks) — that
statuses/blanks update is the one sanctioned edit to this document's task
text.

**Questions the spike must answer (from ESP-IDF 6.0.1 docs/headers and, if
authorized, a scratch probe on hardware):**

1. Which slave-side API the installed IDF offers (`driver/i2c_slave.h` v2?)
   and whether a slave can pre-queue TX data so the master's read finds bytes
   without callbacks in ISR context.
2. How a master read of N bytes behaves when the slave has nothing queued
   (NACK? zeros?) — this decides how `TryReceive` maps to `Unavailable`.
3. Sensible bus speed (100 kHz default?) and whether internal pull-ups
   suffice at that speed over ~20 cm wires, or the two external 4.7 kΩ
   resistors are mandatory.
4. `I2cAddress.h` layout (7-bit address in one byte is the default candidate;
   confirm no conflict concerns — per-driver validation follows the
   `LoraAddress.h` precedent).
5. Frame-in-transaction shape: one FrameCodec frame per I2C transaction
   (length-prefixed read), or the decoder byte-pump over a fixed-size
   transaction window.
6. `I2cMaxPayloadBytes` given the chosen transaction shape and slave buffer
   sizes.

#### Task 2.2 — `FEsp32I2cMasterDriver` + `FEsp32I2cSlaveDriver` + `I2cAddress.h`

- [x] Built
- [ ] Hardware-verified (via task 2.3's checkpoint)

Built 2026-07-23 — `I2cAddress.h` (1-byte node id mirroring `UartAddress.h`), `Esp32I2cDriver.h` (`I2cMaxPayloadBytes = 120`, `FEsp32I2cMasterConfig`/`FEsp32I2cSlaveConfig`, the ISR-filled `FI2cReceiveInbox`, and `FEsp32I2cMasterDriver`/`FEsp32I2cSlaveDriver final : INetDriver`), `src/I2cPlatformImplementation.h` (sole home of `<driver/i2c_master.h>`/`<driver/i2c_slave.h>`: master transmit/receive, slave `i2c_slave_write` with partial-discard, and the `on_receive` ISR callback that fills the inbox), and `src/Esp32I2cDriver.cpp` (both classes; master clocks one whole-frame window and pumps it, slave drains the ISR inbox — file-local `ValidateOutgoingI2cPacket`/`DeliverFrameFromDecoder`/`MapI2cWriteOutcome` shared by both, mirroring the UART driver). The three scoped `AGENTS.md` guides enumerate the two drivers. Standard Verify §1.1 green: `pio run` `[SUCCESS]` (`Esp32I2cDriver.o` compiled; 2 warnings, both vendor `#include_next`, none from this code; image unchanged at RAM 20220 / Flash 193281 since 01-CoreTick links none of it), ctest 11/11, class-doc 132 files, folder-agents 63. Send/receive path UNVERIFIED at runtime until example 20's checkpoint (§1.2). Hardware-verified pending that checkpoint.

**Spec skeleton (spike-filled — see ADR Appendix A):** two `INetDriver` classes
with the task 1.1 file/documentation shape; ESP-IDF confined to a new
`src/I2cPlatformImplementation.h`; `FEsp32I2cMasterConfig` = {`I2cPort`,
`SdaGpio`, `SclGpio`, `SclSpeedHz` (100000), `SlaveAddress` (0x28), `LocalNodeId`}
and `FEsp32I2cSlaveConfig` = {`I2cPort`, `SdaGpio`, `SclGpio`, `SlaveAddress`,
`LocalNodeId`} (the 7-bit bus address is a config field, not folded into
`I2cAddress.h`). Master `TrySend` = one `i2c_master_transmit` of one encoded frame;
master `TryReceive` = one `i2c_master_receive` of a whole-frame window
(`I2cMaxPayloadBytes + FrameOverheadBytes`) pumped through the decoder, mapping
`Unavailable` when the window yields no frame (Appendix A2). Slave `TrySend` = one
`i2c_slave_write` (`Full` when the TX ring cannot take the frame); slave
`TryReceive` drains the ISR-filled inbox through the decoder (Appendix A1). Scoped
`AGENTS.md` guides updated (§2.2). Standard Verify §1.1.

#### Task 2.3 — Example `20-TwoBoardI2c`

- [x] Built
- [ ] Hardware-verified

Built 2026-07-23 — `examples/20-TwoBoardI2c` ping-pongs a counter over the I2C driver pair with the master-clocked asymmetry: the master (`FEsp32I2cMasterDriver`, node 1) paces each volley — sends the counter, then polls reads until the slave's staged reply arrives — while the slave (`FEsp32I2cSlaveDriver`, node 2) is purely reactive, staging `counter + 1` on each receive. Roles by `-DMICROWORLD_EXAMPLE_I2C_MASTER=1|0` (one `Main.cpp`, each env compiles only its role), tag `[ex20]`, 5-byte payload `[senderId][counter BE]`, destinations via `MakeI2cAddress`. Build Verify §1.1 green: `pio run` builds **both** role envs `[SUCCESS]` (example warning-clean; master RAM 20924 B / 6.4%, Flash 218521 B / 5.2%; slave RAM 21164 B / 6.5%, Flash 213101 B / 5.1%), ctest 11/11, class-doc 132, folder-agents 63. Catalog row 20 added to `examples/README.md`. Hardware checkpoint pending (human-gated §1.2; needs two external ~4.7 kΩ pull-ups).

**Spec:** task 1.2's ping-pong with the master/slave asymmetry: board A =
master env, board B = slave env (`-DMICROWORLD_EXAMPLE_I2C_MASTER=1|0`), tag
`[ex20]`. Wiring: GND↔GND, SDA↔SDA (GPIO 8), SCL↔SCL (GPIO 9), plus two external
~4.7 kΩ pull-ups to 3V3 (Appendix A3 — GPIO 8/9 are ordinary DevKitC-1 pins, clear
of the S3 strapping pins 0/3/45/46). Master paces the volley since only it can
clock the bus; README must state that asymmetry in one sentence.

---

### Phase 3 — SPI link

#### Task 3.1 — SPI design spike (ADR appendix)

- [ ] Done

**Questions (same deliverable shape as task 2.1):**

1. Slave transaction queuing in `driver/spi_slave.h`: how many DMA
   transactions can be pre-queued; what happens when the master clocks a
   transfer while the slave queue is empty (garbage bytes? stale buffer?) —
   decides the framing/validation strategy (FrameCodec CRC already rejects
   garbage; confirm that suffices).
2. Transaction size: fixed-size window (decoder pump) vs. exact-frame
   transfers; resulting `SpiMaxPayloadBytes`.
3. Bus speed sensible over jumper wires (start low, e.g. 1 MHz).
4. Pin set on the DevKitC-1 free of strapping/PSRAM conflicts (`SPIKE:`
   propose MOSI/MISO/SCLK/CS).
5. `SpiAddress.h` layout (1-byte node id candidate).

#### Task 3.2 — `FEsp32SpiMasterDriver` + `FEsp32SpiSlaveDriver` + `SpiAddress.h`

- [ ] Built
- [ ] Hardware-verified (via task 3.3's checkpoint)

**Spec skeleton:** task 2.2's shape with `src/SpiPlatformImplementation.h`,
configs per spike, master-clocked semantics: master `TryReceive` performs one
transaction to harvest slave bytes; slave `TrySend` = queue one encoded frame
as a DMA transaction (`Full` when the queue is saturated). Scoped guides
updated. Standard Verify §1.1.

#### Task 3.3 — Example `21-TwoBoardSpi`

- [ ] Built
- [ ] Hardware-verified

**Spec:** task 2.3's asymmetric ping-pong, tag `[ex21]`, roles
`-DMICROWORLD_EXAMPLE_SPI_MASTER=1|0`, wiring GND + MOSI/MISO/SCLK/CS per
spike answer 4, README states the master-clocked asymmetry.

---

### Phase 4 — Close-out

#### Task 4.1 — Documentation sweep

- [ ] Done

**Steps:**

1. `docs/Porting.md` seam 2: add the wired drivers to the reference list
   (UART beside the E32 entry; I2C/SPI with their master/slave note).
2. Root `README.md` module table: extend the PlatformEsp32 role cell to name
   the wired transports.
3. `Modules/PlatformEsp32/library.json`: extend `description`/`keywords`
   (identity fields stay frozen per §2.2).
4. `CHANGELOG.md`: one entry — wired UART/I2C/SPI transports + examples 18–21.
5. `PROGRESS.md`: closing evidence section for the wired plan.

**Done when:** all five updated in one commit; Standard Verify §1.1 green.

---

## 6. Explicitly out of scope

- **Peripheral/device access** (sensors, displays, register maps) over I2C or
  SPI — different problem, future separate system; the ADR owns the boundary.
- **CAN (TWAI) and RS-485** — both need transceiver chips not on hand; add a
  phase when the hardware exists.
- **ESP-NOW / BLE / TCP** — not wired; separate decision if ever wanted.
- **E32 register configuration** — vendor concern, unchanged from the
  examples roadmap.
- **Multi-drop topologies** (one master, many slaves on one bus) — the driver
  pair is designed point-to-point first; multi-drop is a later capacity
  question for `TNetHost`, not a Phase 2/3 requirement.

## 7. Acceptance

The roadmap is complete when every box through Phase 4 is checked, examples
18–21 have hardware-verified traces in their READMEs, and a student can read
`examples/README.md` and see the same ping-pong volley running over LoRa,
UART, I2C, and SPI with the driver construction as the only difference.
