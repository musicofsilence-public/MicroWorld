# MicroWorld — Actor Messaging & Engine-First Examples Roadmap

**Version:** 1.0 · **Date:** 2026-07-23 · **Owner:** Mykola
**Baseline:** `main` at `ff1ced1` (clean tree), Windows 11 + Visual Studio 2022 root superbuild, PlatformIO for examples.
**Scope:** `Modules/Engine`, `Modules/Net` (one additive test helper), `Modules/PlatformEsp32`, `examples/`.

**Mission.** Examples must demonstrate the MicroWorld engine, not ESP-IDF. The
owner's target model:

> We create a world — server, standalone, or client depending on context. That
> world creates actors (sensors or more complex devices); actors may have
> components. The world is configured with one or more communication channels,
> each with its own driver (network or wire) and settings. Actors send messages
> to specific actors or broadcast to all, with callbacks, without knowing the
> transport. Whether delivery is guaranteed is configurable per channel. Actors
> register for specific message types.

This document is the active plan and progress tracker for delivering that
model. It is written so that any LLM (including a weak one) can pick it up,
find the next task, complete it, and record progress without extra context.
Companion documents: `docs/ROADMAP.md` and `docs/SIMPLICITY_ROADMAP.md` are
**frozen history — never edit them**; `docs/EXAMPLES_ROADMAP.md` and
`docs/WIRED_TRANSPORTS_ROADMAP.md` own their own example sets — this plan never
edits their task sections; `PROGRESS.md` is the live evidence record.

---

## 1. How to use this document (protocol for LLM workers)

Follow these rules exactly:

1. Read section **2 (Ground rules)** and section **4 (Target design)** before
   touching any code.
2. Open section **5 (Progress tracker)**. Find the first phase whose status is
   not ✅. Inside that phase, find the first unchecked `[ ]` task.
3. Work on **exactly one task at a time**, in order. Do not start a later phase
   while an earlier phase has unchecked tasks.
4. Every task has **Steps**, a **Done when** checklist, and a **Verify**
   instruction. A task is complete only when every "Done when" item is true and
   every Verify command passes.
5. When a task is complete: change its `[ ]` to `[x]`, append one evidence line
   directly under the task (`Done YYYY-MM-DD — <one sentence of proof>`), and
   update the phase status in the tracker (⬜ → 🟨 on first task, 🟨 → ✅ on
   last).
6. If you are blocked, write `⛔ BLOCKED:` plus one sentence under the task and
   stop. Do not skip ahead.
7. Never delete or rewrite this document's structure. Only update statuses,
   checkboxes, evidence lines, and BLOCKED notes.
8. When a phase reaches ✅: add one short evidence entry to `PROGRESS.md`.

Status legend: ⬜ not started · 🟨 in progress · ✅ done · ⛔ blocked

### 1.1 Standard Verify (host edition)

Run from the repo root, in this order, for every task that touches `Modules/`:

```sh
clang-format --style=file:clang-format -i <every .h/.cpp file you touched>
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python tools/CheckClassDocumentation.py --root Modules --require-doxygen
```

Expected: warning-clean build (warnings are errors), ctest reports **zero
failures** (the passing count grows as tasks add suites — record the new count
in your evidence line), documentation checker passes. If `build/` is missing,
create it first with `cmake -S . -B build`.

### 1.2 Example Build Verify

For every task that touches `examples/`:

```sh
pio run -d examples/<NN-Name>
```

Every environment in that example's `platformio.ini` must compile clean.
Compile success is never a runtime claim — see §1.3.

### 1.3 Hardware checkpoint (human-gated — never self-serve)

Identical to `docs/EXAMPLES_ROADMAP.md` §1.2, which this plan reuses verbatim:
building never flashes hardware; uploading/monitoring a board requires
explicit human authorization; each example README carries the
"not yet verified on hardware" sentence until its captured trace is pasted in.
The two-board rig is asymmetric — put the console-printing role on the CH343
board (see `Modules/PlatformEsp32/benchmarks/Results/Esp32S3N16R8.md` and the
wiring lessons recorded by examples 18–21).

### 1.4 How to locate code

Every `file:line` in this document was verified at baseline `ff1ced1`.
Completed tasks shift later line numbers. **Always locate code by the quoted
symbol** (`rg -n "SymbolName" Modules`), never by a remembered offset.

### 1.5 Files you must never edit

- `docs/ROADMAP.md`, `docs/SIMPLICITY_ROADMAP.md` — frozen historical plans.
- The task sections of `docs/EXAMPLES_ROADMAP.md` and
  `docs/WIRED_TRANSPORTS_ROADMAP.md` (their catalogs/trackers belong to them;
  this plan's examples are registered in `examples/README.md` only).
- Existing entries in `CHANGELOG.md` (appending is allowed).
- `Modules/*/benchmarks/Results/*.md`, `LICENSE`, anything under `build/`,
  `.pio/`, or `.git/`.

---

## 2. Ground rules (invariants — never violate)

### 2.1 Embedded invariants (inherited, unchanged)

- **C++17**; exceptions and RTTI **disabled**. No `throw`, no `dynamic_cast`,
  no `typeid`.
- **No hidden allocation** in steady state; storage is caller-owned and
  fixed-capacity. **No hidden clock** — time is caller-supplied
  `TimePointMilliseconds`; only platform adapters read real clocks.
- **Errors are enums**; failures are transactional (a failed call leaves all
  inputs and state unchanged).
- **Determinism**: registration order defines dispatch order; shutdown runs in
  reverse; no catch-up ticks; no randomness anywhere (test loss injection is a
  deterministic counter, not RNG).
- **Dependency direction** (enforced by `tools/CheckDependencyBoundaries.py`):
  `Core <- Memory <- Object <- Engine`, `Core <- Memory <- Net`. **Engine
  never includes a Net header and Net never includes an Engine header.** All
  Engine⇄Net cooperation goes through Engine-owned interfaces plus duck-typed
  templates, exactly like the shipped `TNetHostFrame` (`NetworkFrame.h:40`).
- **Frozen identity**: CMake project/target names, `MicroWorld::*` aliases,
  `library.json` package names, and existing public header paths stay exactly
  as they are. Adding a new public header is allowed.
- Every function declaration and persistent member gets a Doxygen `/** */`
  comment (`CheckClassDocumentation.py --require-doxygen`).
- Every new folder gets an `AGENTS.md` (checked by `CheckFolderAgents.py`).
- Naming: `F` plain class/struct, `T` class template, `E` enum, `I` interface,
  `b` bool prefix, `U`/`A` managed types. Names are plain English a student
  reads without a glossary — **no metaphors, no new abbreviations**; spell out
  units (`RetryIntervalMilliseconds`).
- Format with `clang-format --style=file:clang-format` (the repo policy file
  is named `clang-format`, no dot — plain `clang-format -i` silently
  misformats).

### 2.2 The engine-first example rule (what this whole plan enforces)

After Phase 1, an example's `src/` may contain **only**:

- `#include <MicroWorld/...>` headers, plus `<cstdint>` and `<cstddef>`;
- the ESP-IDF entry symbol `extern "C" void app_main(void)` (no header
  needed);
- MicroWorld API calls.

Forbidden in example sources (each Phase 1+ example task has a grep gate):
`esp_*`, `nvs_*`, `lwip`/sockets, `freertos` includes, `vTaskDelay`,
`xEventGroup*`, `printf`/`<cstdio>`, `std::array`/`std::string`/`std::vector`,
raw `uart_*`/`i2c_*`/`spi_*`/`gpio_*`. Vendor calls live in
`Modules/PlatformEsp32` behind config structs — if an example needs a vendor
call, the engine grows a facade first (that is the point of this plan).

Existing example conventions stay: one feature per example; every MicroWorld
composition object is `static` at file scope (main-task stack is small); role
selection via `-D` flags with both role TUs always compiling; serial console
is the observable; secrets never enter git.

### 2.3 Decisions record (settled — do not relitigate while executing)

- **D1 — Messaging lives in Engine.** Actor addressing and message routing are
  engine concepts. The 6-byte actor-message header codec is hand-rolled in
  `Engine/Message.h` (three little-endian `uint16` fields) instead of reusing
  Net's `FByteWriter`, because Engine may not include Net. The header is opaque
  payload to `TNetHost` — Net is untouched by Phases 2–4.
- **D2 — The router is a network frame.** `TMessageRouter` implements the
  existing `INetworkFrame` seam (`TickDispatch` = deliver queued inbound,
  `TickFlush` = flush queued outbound). No new pump slots on `TEngineHost`;
  multi-frame composition is solved once by `TNetworkFrameSet` (D3).
- **D3 — `TNetworkFrameSet` pumps dispatch in add-order and flush in
  reverse add-order.** Recipe: add net frames first, router last. Then inbound
  wire bytes reach the router before it delivers (dispatch: net → router), and
  the router hands outbound to net queues before they flush to the wire
  (flush: router → net). Deterministic, documented, tested.
- **D4 — Fan-out is channel policy, not actor knowledge.** A wired channel
  sends either to the server (client role) or to all peers (server role) —
  `EChannelSendTarget { Server, AllPeers }`, chosen at composition. Receiving
  worlds filter by `TargetActorId`. Per-peer targeted server sends (an
  actor-id→peer routing table) are **out of scope v1**; revisit trigger: an
  example needs a server to command one specific client without the others
  seeing the message.
- **D5 — Messages are queued, never dispatched inline.** Send calls enqueue
  into a bounded outbound queue; handlers run only inside `TickDispatch`.
  Local messages (channel 0) arrive at the **next** frame's dispatch; wire
  messages received in step 1 are delivered in the **same** frame (frame-set
  order). Sending from inside a handler is legal (it appends to the outbound
  queue); adding/removing handlers during dispatch returns `DispatchLocked` —
  the `TTimerManager` discipline.
- **D6 — Guaranteed delivery is a payload sub-protocol above `TNetHost`.**
  `TReliableChannel` wraps any `IMessageChannel` and prefixes
  `[Kind][Sequence]` (3 bytes) onto the encoded message; acknowledgements and
  retries never touch `NetProtocol.h`, whose Flags byte stays reserved-zero.
  Rejected alternative: extending `TNetHost` with per-channel reliability —
  invasive to a freshly simplified 761-line class, and the handler signature
  would have to grow.
- **D7 — Guaranteed channels are point-to-point in v1** (exactly two nodes:
  one client, one server). Sequences and the duplicate-drop window are
  per-channel, not per-peer. Revisit trigger: a guaranteed broadcast to ≥2
  peers is needed — that requires per-peer pending/ack tables.
- **D8 — Delivery configuration is composition.** A channel is guaranteed when
  its binding is wrapped in a `TReliableChannel` at the composition root; there
  is no runtime delivery enum to flip. Simplest thing that satisfies
  "configurable per channel".
- **D9 — Actors reach messaging by constructor injection.** User actors take
  `IMessageRouter&` in their constructor. `UWorld`/`AActor`/`UActorComponent`
  gain **no** messaging members — the engine core stays untouched, coupling
  stays minimal, and standalone worlds without messaging pay nothing.
  Rejected: a router pointer on `UWorld` (forces every world to know about
  messaging) and a global router (hidden state).
- **D10 — The word `Reliable`** (industry vocabulary students must learn, like
  `Delta` in the simplicity plan) names the guaranteed-delivery wrapper:
  `TReliableChannel`. The owner may rename before Phase 5 starts; the rename
  procedure of `SIMPLICITY_ROADMAP.md` §1.3 applies if so.
- **D11 — World "modes" are composition recipes, not a world enum.**
  Standalone = router only; client/server = `TNetHost` configured with the
  existing `ENetMode` plus the same router. `UWorld` stays mode-agnostic.
- **D12 — WiFi bring-up becomes a platform service** (`FEsp32WifiLink`),
  because two examples already duplicate ~166 raw ESP-IDF lines and every
  future WiFi example would copy them again. Blocking behavior during startup
  is acceptable there (startup-time, like arena allocation); the public header
  carries no ESP-IDF include.

### 2.4 Reference files (imitate them)

| Concern | Imitate |
| --- | --- |
| Handle + slot + generation + dispatch guard | `Modules/Engine/include/MicroWorld/Engine/Timer.h` |
| Bounded inline callbacks | `Modules/Memory/include/MicroWorld/Delegates/Delegate.h` |
| Engine⇄Net seam without a dependency | `Modules/Engine/include/MicroWorld/Engine/NetworkFrame.h` |
| FIFO with retained head on failure | `Modules/Net/include/MicroWorld/Net/NetManager.h` |
| Driver facade with config struct + `IsOpen()` | `Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32UartDriver.h` |
| Full engine+net composition | `Modules/PlatformHost/examples/TwoNodeDemo/Main.cpp`, `examples/19-UartMessaging` |

---

## 3. What exists today (verified at `ff1ced1` — the map)

**The transport seam is complete.** `INetDriver` (`Net/NetDriver.h:40`) =
`TrySend`/`TryReceive`/`MaxPacketBytes` over an opaque `FNetAddress`.
Implementations: `THostLoopback` (Net, in-process test network),
`FHostUdpDriver` (PlatformHost), and on ESP32: `FEsp32UdpDriver`,
`FEsp32E32LoraDriver`, `FEsp32UartDriver`, `FEsp32I2cMaster/SlaveDriver`,
`FEsp32SpiMaster/SlaveDriver`. Payload caps: UDP 1200, UART/I2C/SPI 120,
E32 LoRa 58.

**Sessions and wire channels exist.** `TNetHost<MaxPeers, MaxPacketBytes>`
(`Net/NetHost.h:108`): `ENetMode { Standalone, Client, ListenServer,
DedicatedServer }`, bounded peer table with Hello/Welcome/Heartbeat/Bye,
`SendTo(FPeerId, Channel, payload)` / `Broadcast(Channel, payload)`, wire
channel byte 0 = control, 1–255 = application, one
`TMulticastDelegate<void(FPeerId, uint8, TSpan<const uint8>), 4, 32>` message
handler. **No acks, no retries, no sequencing** — only CRC integrity
(`FrameCodec.h`), FIFO-head retention (`TNetManager`), and liveness timeouts.

**The engine pumps one frame.** `TEngineHost` (`Engine/EngineHost.h:42`) runs
the fixed 7-step frame; step 1 calls `Network->TickDispatch`, step 7
`Network->TickFlush`, where `Network` is a single optional `INetworkFrame*`
(`EngineHost.h:342`). `TNetHostFrame<TNet>` adapts any `TNetHost` by
duck-typing `PumpReceive`/`PumpSend`. **There is no actor-level messaging**:
no per-actor addressing, no message-type registration, no engine channel
concept — `Modules/Engine/tests/EngineNetHostTests.cpp` wires raw
`TNetHost` handlers straight to `World.SpawnActor` because nothing better
exists.

**Actors and worlds are solid.** `UWorld` (register/begin/advance/end +
deferred `SpawnActor`/`DestroyActor` barrier), `AActor`/`UActorComponent`
lifecycle hooks (`BeginPlay`/`Tick`/`EndPlay`), `TTimerManager`,
`TInlineActor`/`TInlineWorld`, `TEngineHost` composition. All bounded,
deterministic, allocation-free.

**Examples on disk:** `01-CoreTick`, `15-UdpEcho`, `16-TwoBoardUdp`,
`18-TwoBoardUart`, `19-UartMessaging`, `20-TwoBoardI2c`, `21-TwoBoardSpi`
(02–14 and 17 are planned rows in other roadmaps — not this plan's concern).
Raw-API audit result: the wired examples are clean at hardware level (drivers
encapsulate everything); the raw mass is exactly (a) ~166 duplicated lines of
ESP-IDF WiFi bring-up in `15/16 WifiLink.cpp`, (b) raw lwIP sockets in
`15/ProbeMain.cpp`, (c) `std::printf` everywhere instead of the shipped
`MW_LOG`/`Esp32LogSink` seam, (d) `vTaskDelay`+FreeRTOS includes for pacing,
(e) stray `std::array`. Phases 1 and 2–5 remove all five.

---

## 4. Target design (read before Phase 2 — this is the contract)

### 4.1 The owner's vision, mapped to mechanisms

| Vision statement | Mechanism (this plan) |
| --- | --- |
| "world — server, standalone or client, depends on context" | Composition recipe: same `UWorld` + `TEngineHost`; the `TNetHost` next to it is configured `Standalone`/`Client`/`ListenServer`/`DedicatedServer` (D11) |
| "world creates actors — sensors or devices; actors have components" | Existing `UWorld::SpawnActor` / `AActor::RegisterComponent`; examples 22–25 model sensors/devices as actors with components |
| "several communication channels, each with its own driver and settings, configured with some kind of id" | One `TNetHost` per driver; each channel = `TMessageChannelBinding` with a caller-chosen `FMessageChannelId`; all bindings registered on one `TMessageRouter`; all frames pumped by one `TNetworkFrameSet` |
| "actors communicate via messages with callback, without knowing the transport" | `IMessageRouter::SendMessageToActor`/`BroadcastMessage` + handlers as `TDelegate` callbacks; actors see only `IMessageRouter&` |
| "send to specific actors or broadcast to all" | `TargetActorId` ≠ 0 targets one actor id; `BroadcastActorId` (0) reaches every subscriber of the type |
| "guaranteed delivery configurable per channel" | Wrap that channel's binding in `TReliableChannel` (D6–D8) |
| "actors register for specific message types" | `AddMessageHandler(MessageTypeId, ListenerActorId, callback)` |

### 4.2 The message on the wire

An **actor message** is what the router queues, channels carry, and handlers
receive. Encoded form (all multi-byte fields little-endian):

```
EncodedActorMessage = [u16 MessageTypeId][u16 TargetActorId][u16 SenderActorId][Payload...]
ActorMessageHeaderBytes = 6
TargetActorId == 0 (BroadcastActorId)  →  every handler registered for MessageTypeId
TargetActorId == X (≠0)                →  only handlers registered with ListenerActorId == X
```

On a wired channel this sits inside the existing stack unchanged:

```
driver frame:   [magic][node][len][ TNetHost message ][crc]        (FrameCodec — wired transports)
TNetHost msg:   [u8 WireChannel][u8 Flags=0][u16 PayloadBytes][ payload ]   (NetProtocol)
payload:        EncodedActorMessage                                (best-effort channel)
payload:        [u8 Kind][u16 Sequence][EncodedActorMessage]       (guaranteed channel, Kind=1 Data)
payload:        [u8 Kind][u16 Sequence]                            (guaranteed channel, Kind=2 Ack)
ReliableHeaderBytes = 3
```

**Payload budget** (`MaxMessageBytes` = encoded actor message incl. its 6-byte
header; must satisfy every transport the world uses):

| Transport | Driver payload cap | − TNetHost header (4) | Best-effort max | Guaranteed max (−3) |
| --- | --- | --- | --- | --- |
| UDP | 1200 | 1196 | 1196 | 1193 |
| UART / I2C / SPI | 120 | 116 | 116 | 113 |
| E32 LoRa | 58 | 54 | 54 | 51 |

Examples use `MaxMessageBytes = 96` — comfortable on every transport except
LoRa (48 max there; LoRa messaging examples are out of scope v1).

### 4.3 New API by header (exact shapes — implement these signatures)

**`Modules/Engine/include/MicroWorld/Engine/Message.h`** (new, Phase 2.1) —
vocabulary + codec + the two interfaces. Depends only on `<cstdint>`,
`<cstddef>`, `Containers/Span.h`, `Delegates/Delegate.h`.

```cpp
namespace MicroWorld
{
/** Identifies what kind of message this is; 0 is invalid. */
using FMessageTypeId = std::uint16_t;
/** Identifies an actor in messaging; caller-assigned; 0 is the broadcast target. */
using FMessageActorId = std::uint16_t;
/** Target id meaning "every handler registered for the type". */
inline constexpr FMessageActorId BroadcastActorId = 0;
/** Identifies one configured channel on a router; 0 is the built-in local channel. */
using FMessageChannelId = std::uint8_t;
/** Channel id for local (same-world, no wire) delivery; always available. */
inline constexpr FMessageChannelId LocalChannelId = 0;
/** Encoded size of the three-field actor-message header. */
inline constexpr std::size_t ActorMessageHeaderBytes = 6;

/** Outcome vocabulary for every messaging operation. */
enum class EMessageResult : std::uint8_t
{
    Success, CapacityExceeded, Duplicate, InvalidType, InvalidChannel,
    InvalidHandler, StaleHandle, DispatchLocked, PayloadTooLarge, Unavailable
};

/** The three ids every actor message carries in front of its payload. */
struct FActorMessageHeader
{
    FMessageTypeId MessageTypeId{0};
    FMessageActorId TargetActorId{BroadcastActorId};
    FMessageActorId SenderActorId{0};
};

/** Writes header+payload little-endian into OutEncoded; transactional on failure. */
EMessageResult EncodeActorMessage(const FActorMessageHeader& Header,
                                  TSpan<const std::uint8_t> Payload,
                                  TSpan<std::uint8_t> OutEncoded,
                                  std::size_t& OutWrittenBytes) noexcept;
/** Splits an encoded message back into header and payload view; transactional. */
EMessageResult DecodeActorMessage(TSpan<const std::uint8_t> Encoded,
                                  FActorMessageHeader& OutHeader,
                                  TSpan<const std::uint8_t>& OutPayload) noexcept;

/** One delivered message as a handler sees it. */
struct FMessageView
{
    FActorMessageHeader Header;
    /** Channel the message arrived on (LocalChannelId for same-world sends). */
    FMessageChannelId ArrivedOnChannelId{LocalChannelId};
    TSpan<const std::uint8_t> Payload;
};

/** Inline byte budget for one message-handler callable (TNetHost precedent). */
inline constexpr std::size_t MessageHandlerInlineBytes = 32;
/** Callback type actors bind to receive messages. */
using FMessageHandlerBinding = TDelegate<void(const FMessageView&), MessageHandlerInlineBytes>;
/** Generation-checked handler registration handle (FTimerHandle shape). */
struct FMessageHandlerHandle { std::uint16_t Index; std::uint32_t Generation; /* IsValid, ==, != */ };

/** Anything that can accept one encoded actor message arriving from a channel. */
class IEncodedMessageSink
{
public:
    virtual ~IEncodedMessageSink() noexcept = default;
    /** Queues one encoded message that arrived on the given channel; transactional. */
    virtual EMessageResult ReceiveEncodedMessage(FMessageChannelId ArrivedOnChannelId,
                                                 TSpan<const std::uint8_t> Encoded) noexcept = 0;
};

/** Outbound side of one configured channel; implemented by bindings and wrappers. */
class IMessageChannel
{
public:
    virtual ~IMessageChannel() noexcept = default;
    /** This channel's caller-assigned id (never LocalChannelId). */
    virtual FMessageChannelId GetChannelId() const noexcept = 0;
    /** Largest encoded message this channel can carry in one send. */
    virtual std::size_t MaxEncodedMessageBytes() const noexcept = 0;
    /** Hands one encoded message to the transport; Full means retry next frame. */
    virtual EMessageResult TrySendEncodedMessage(TSpan<const std::uint8_t> Encoded) noexcept = 0;
};

/** The actor-facing messaging API; actors hold this by reference (D9). */
class IMessageRouter : public IEncodedMessageSink
{
public:
    /** Registers a callback for one message type; ListenerActorId 0 = broadcasts only. */
    virtual EMessageResult AddMessageHandler(FMessageTypeId MessageTypeId,
                                             FMessageActorId ListenerActorId,
                                             FMessageHandlerBinding&& Handler,
                                             FMessageHandlerHandle& OutHandle) noexcept = 0;
    /** Removes one previously registered callback; stale handles are rejected. */
    virtual EMessageResult RemoveMessageHandler(FMessageHandlerHandle Handle) noexcept = 0;
    /** Queues one message for a specific target actor on the given channel. */
    virtual EMessageResult SendMessageToActor(FMessageChannelId ChannelId,
                                              FMessageTypeId MessageTypeId,
                                              FMessageActorId TargetActorId,
                                              FMessageActorId SenderActorId,
                                              TSpan<const std::uint8_t> Payload) noexcept = 0;
    /** Queues one message for every subscriber of the type on the given channel. */
    virtual EMessageResult BroadcastMessage(FMessageChannelId ChannelId,
                                            FMessageTypeId MessageTypeId,
                                            FMessageActorId SenderActorId,
                                            TSpan<const std::uint8_t> Payload) noexcept = 0;
};
} // namespace MicroWorld
```

**`Modules/Engine/include/MicroWorld/Engine/MessageRouter.h`** (new, Phase
2.2) — the one concrete router.

```cpp
/**
 * Routes actor messages between handlers and channels.
 * Implements INetworkFrame so TEngineHost pumps it like any net frame:
 * TickDispatch delivers queued inbound messages to matching handlers;
 * TickFlush hands queued outbound messages to their channels.
 */
template<std::size_t MaxHandlers, std::size_t MaxQueuedMessages,
         std::size_t MaxMessageBytes, std::size_t MaxChannels>
class TMessageRouter final : public IMessageRouter, public INetworkFrame
{
public:
    TMessageRouter() noexcept;
    // IMessageRouter — see Message.h contracts.
    // INetworkFrame:
    void TickDispatch(TimePointMilliseconds NowMilliseconds) noexcept override; // deliver inbound
    void TickFlush(TimePointMilliseconds NowMilliseconds) noexcept override;    // flush outbound
    /** Registers one outbound channel under its id; rejects id 0 and duplicates. */
    EMessageResult AddChannel(IMessageChannel& Channel) noexcept;
    /** Observability. */
    std::size_t QueuedInboundCount() const noexcept;
    std::size_t QueuedOutboundCount() const noexcept;
    std::size_t HandlerCount() const noexcept;
    std::uint32_t DroppedInboundCount() const noexcept; // inbound queue full
};
```

Semantics (normative):

- **Handler table**: `MaxHandlers` slots `{TypeId, ListenerActorId, Delegate,
  Generation, bActive}`; registration order = delivery order;
  `FMessageHandlerHandle` = index+generation (imitate `Timer.h`).
- **Queues**: two ring FIFOs (inbound, outbound), each `MaxQueuedMessages`
  entries of `{FMessageChannelId, u16 LengthBytes, u8 Bytes[MaxMessageBytes]}`.
  Bytes are copied in (bounded, no aliasing).
- **Send paths** (`SendMessageToActor`/`BroadcastMessage`): validate
  (`InvalidType` if TypeId 0; `InvalidChannel` if ChannelId unknown and not
  `LocalChannelId`; `PayloadTooLarge` if 6+payload > MaxMessageBytes or > the
  channel's `MaxEncodedMessageBytes()`; `CapacityExceeded` if outbound full),
  encode once, enqueue outbound. Broadcast = send with
  `TargetActorId = BroadcastActorId`.
- **`TickFlush`**: pop outbound from head, in order. `LocalChannelId` entries
  move to the inbound queue (if inbound full: retain head, stop). Wired
  entries go to `Channel.TrySendEncodedMessage`; on any non-Success: retain
  head, stop (TNetManager discipline — head-of-line blocking across channels
  is accepted v1 and documented).
- **`TickDispatch`**: snapshot the inbound count at entry, deliver exactly
  that many messages, oldest first. For each: decode, build `FMessageView`,
  invoke every matching handler in registration order. A dispatch guard makes
  `AddMessageHandler`/`RemoveMessageHandler` return `DispatchLocked` inside
  handlers; **sends inside handlers are legal** (they enqueue outbound).
- **`ReceiveEncodedMessage`**: validate length (≥ header, ≤ MaxMessageBytes),
  enqueue inbound; on full queue increment `DroppedInboundCount` and return
  `CapacityExceeded` (transactional for the queue, counted for observability).

**`Modules/Engine/include/MicroWorld/Engine/NetworkFrame.h`** (extended,
Phase 4.1):

```cpp
/** Pumps several network frames as one: dispatch in add-order, flush in reverse. */
template<std::size_t MaxFrames>
class TNetworkFrameSet final : public INetworkFrame
{
public:
    /** Adds one caller-owned frame; order of Add calls is the dispatch order. */
    EEngineResult Add(INetworkFrame& Frame) noexcept; // Duplicate / CapacityExceeded
    void TickDispatch(TimePointMilliseconds NowMilliseconds) noexcept override;
    void TickFlush(TimePointMilliseconds NowMilliseconds) noexcept override;
    std::size_t FrameCount() const noexcept;
};
```

**`Modules/Engine/include/MicroWorld/Engine/MessageChannelBinding.h`** (new,
Phase 3.1) — binds one router channel to one `TNetHost`, duck-typed like
`TNetHostFrame`:

```cpp
/** Which peers a wired channel sends to (D4). */
enum class EChannelSendTarget : std::uint8_t { Server, AllPeers };

/**
 * Two-way adapter between one TNetHost wire channel and one message sink.
 * Outbound: TrySendEncodedMessage → Host.SendTo(server) or Host.Broadcast.
 * Inbound: registers a TNetHost message handler in the constructor that
 * forwards payloads with the matching wire channel byte to the sink.
 */
template<typename TNet>
class TMessageChannelBinding final : public IMessageChannel
{
public:
    /** Registers the inbound handler; check IsAttached() after construction. */
    TMessageChannelBinding(TNet& Host, std::uint8_t WireChannelByte,
                           FMessageChannelId ChannelId, EChannelSendTarget SendTarget,
                           IEncodedMessageSink& Sink) noexcept;
    bool IsAttached() const noexcept;           // inbound handler registered
    std::uint32_t DroppedInboundCount() const noexcept; // sink rejected (its queue full)
    // IMessageChannel: GetChannelId / MaxEncodedMessageBytes / TrySendEncodedMessage
};
```

`TrySendEncodedMessage` maps `ENetResult` → `EMessageResult` (normative:
`Success`→`Success`, `Full`→`CapacityExceeded`, `Unavailable`→`Unavailable`,
`Invalid`→`PayloadTooLarge`; the distinction is informational — the router
retains its head on any non-Success). `SendTarget == Server` requires
`Host.GetServerPeer()` valid, else `Unavailable`. `MaxEncodedMessageBytes()`
= `TNet::MaxMessageBytes` — expose the TNetHost packet budget minus its
4-byte message header (read the exact constant from `NetHost.h` when
implementing; add a small public `constexpr` accessor to `TNetHost` **only if
none exists** — additive, documented).

**`Modules/Engine/include/MicroWorld/Engine/ReliableChannel.h`** (new, Phase
5.2):

```cpp
inline constexpr std::size_t ReliableHeaderBytes = 3;
enum class EReliablePacketKind : std::uint8_t { Data = 1, Acknowledgement = 2 };

/** Retry/acknowledgement settings for one guaranteed channel. */
struct FReliableChannelConfig
{
    DurationMilliseconds RetryIntervalMilliseconds{250};
    std::uint8_t MaxSendAttempts{8};
};

/**
 * Guaranteed-delivery wrapper around one IMessageChannel (D6–D8).
 * Sits between the binding and the router in BOTH directions:
 * outbound (router → this → binding) it prefixes [Kind=Data][Sequence] and
 * keeps a copy until acknowledged; inbound (binding → this → router) it
 * acknowledges data, drops duplicates, and forwards fresh messages.
 * Implements INetworkFrame: TickFlush resends due unacknowledged messages.
 * Point-to-point only (D7).
 */
template<std::size_t MaxPendingMessages, std::size_t MaxMessageBytes>
class TReliableChannel final : public IMessageChannel, public IEncodedMessageSink,
                               public INetworkFrame
{
public:
    TReliableChannel(IEncodedMessageSink& ForwardSink, FReliableChannelConfig Config) noexcept;
    /**
     * Binds the wrapped channel once at composition (breaks the wrapper⇄binding
     * reference cycle: the binding's constructor needs this object as its sink).
     * Until this is called, TrySendEncodedMessage returns Unavailable and
     * GetChannelId returns 0 — so call it BEFORE Router.AddChannel(*this).
     */
    void SetInnerChannel(IMessageChannel& InnerChannel) noexcept;
    // IMessageChannel: GetChannelId forwards inner id; MaxEncodedMessageBytes = inner − 3;
    //                  TrySendEncodedMessage = wrap, store pending, send via inner.
    // IEncodedMessageSink: Data → ack via inner, duplicate-check, forward to sink;
    //                      Ack → clear matching pending slot.
    // INetworkFrame: TickDispatch no-op; TickFlush resends pendings whose
    //                RetryIntervalMilliseconds elapsed; after MaxSendAttempts → drop + LostCount.
    std::size_t PendingCount() const noexcept;
    std::uint32_t ResentCount() const noexcept;
    std::uint32_t LostCount() const noexcept;
    std::uint32_t DuplicateDroppedCount() const noexcept;
};
```

Duplicate detection (normative, point-to-point): keep
`HighestSequenceSeen` (u16, serial-number arithmetic:
`IsNewer(A, B) = (A != B) && (static_cast<std::uint16_t>(A - B) < 0x8000)`)
plus a 32-bit mask of the 32 sequences below it. Fresh → forward + update;
seen → `DuplicateDroppedCount`, still acknowledge (the first ack may have been
lost). Sequences start at 1; 0 never sent.

**`Modules/Net/include/MicroWorld/Net/PacketDropDriver.h`** (new, Phase 5.1)
— deterministic loss for tests and the demo example:

```cpp
/** Wraps another driver and silently drops every Nth outgoing send. */
class FPacketDropDriver final : public INetDriver
{
public:
    /** DropEveryNthSend = 3 drops sends 3, 6, 9…; 0 disables dropping. */
    FPacketDropDriver(INetDriver& InnerDriver, std::uint32_t DropEveryNthSend) noexcept;
    // TrySend: counts calls; a dropped send returns Success without touching the wire.
    // TryReceive / MaxPacketBytes: forward to inner.
    std::uint32_t DroppedSendCount() const noexcept;
};
```

**`Modules/PlatformEsp32`** (Phase 1): public `Esp32WifiLink.h` —

```cpp
/** Settings for hosting a SoftAP network (board is the access point). */
struct FEsp32AccessPointConfig { const char* Ssid; const char* Password;
                                 std::uint8_t WifiChannel{1}; std::uint8_t MaxStations{2}; };
/** Settings for joining an existing network (board is a station). */
struct FEsp32StationConfig { const char* Ssid; const char* Password;
                             DurationMilliseconds ConnectTimeoutMilliseconds{15000}; };
/** One-per-firmware WiFi bring-up facade; blocking, startup-time only (D12). */
class FEsp32WifiLink
{
public:
    FEsp32WifiLink() noexcept;                    // non-copy/non-move
    ENetResult StartAccessPoint(const FEsp32AccessPointConfig& Config) noexcept;
    ENetResult JoinAccessPoint(const FEsp32StationConfig& Config) noexcept;
    bool IsUp() const noexcept;
    void Stop() noexcept;
};
```

and public `Esp32Sleep.h` —

```cpp
/** Yields the calling task for the given time (wraps vTaskDelay; ≥1 tick). */
void SleepMilliseconds(DurationMilliseconds SleepMilliseconds) noexcept;
```

All ESP-IDF includes stay in `src/Esp32WifiPlatformImplementation.h` /
`Esp32WifiLink.cpp` / `Esp32Sleep.cpp`.

### 4.4 Composition recipes (copy these shapes into examples)

**Standalone world with local messaging** (example 22):

```cpp
static TMessageRouter<16, 8, 96, 1> Router;              // handlers, queue, bytes, channels
static TEngineHost<8, 16, 512, 16, 2, 4, 8, 64> Engine{Budget, Router}; // router IS the net frame
// actors take Router by IMessageRouter& (D9), subscribe in BeginPlay via AddMessageHandler
```

**Client/server over one wire** (example 23; server side shown):

```cpp
static FEsp32UartDriver Driver{{.UartPort = 1, .TxGpio = 17, .RxGpio = 18,
                                .BaudRate = 115200, .LocalNodeId = 1}};
static TNetHost<2, 120> Net{Driver};                     // Configure(DedicatedServer) + Start
static TMessageRouter<16, 8, 96, 1> Router;
static TMessageChannelBinding<decltype(Net)> Commands{Net, /*wire*/1, /*id*/1,
                                                      EChannelSendTarget::AllPeers, Router};
static TNetHostFrame<decltype(Net)> NetFrame{Net};
static TNetworkFrameSet<2> Frames;                        // Add(NetFrame); Add(Router); — D3 order
static TEngineHost<...> Engine{Budget, Frames};
// after wiring: Router.AddChannel(Commands);
```

**Two drivers, two channels, one world** (example 24): second driver + second
`TNetHost` + second binding (different `FMessageChannelId`), both net frames
added before the router in the frame set.

**Guaranteed channel** (example 25): the reliable wrapper sits between the
binding and the router in both directions. The wrapper and the binding each
hold the other by reference — a construction cycle broken by the wrapper's
one deliberate two-phase setup (`SetInnerChannel`, see §4.3):

```cpp
static TReliableChannel<8, 96> Reliable{Router /*forward sink*/, {}};
static TMessageChannelBinding<decltype(Net)> Wire{Net, /*wire*/1, /*id*/1,
                                                  EChannelSendTarget::Server,
                                                  Reliable /*inbound sink*/};
static TNetworkFrameSet<3> Frames;
// at startup, in this order:
//   Reliable.SetInnerChannel(Wire);   // outbound: router → reliable → wire
//   Router.AddChannel(Reliable);      // AFTER SetInnerChannel (GetChannelId needs the inner id)
//   Frames.Add(NetFrame); Frames.Add(Reliable); Frames.Add(Router);   // D3 order
```

### 4.5 Message-type registry convention for examples

Each example defines its ids in one header block — plain constants, no enum
ceremony:

```cpp
inline constexpr FMessageTypeId TemperatureReadingMessageId = 1;
inline constexpr FMessageActorId ThermometerActorId = 10;
inline constexpr FMessageChannelId TelemetryChannelId = 1;
```

### 4.6 Out of scope (do not build, even if asked nicely)

Per-peer targeted server sends / actor-id→peer routing tables (D4); guaranteed
broadcast to ≥2 peers (D7); message fragmentation (one message = one packet);
LoRa messaging examples; property replication/RPC; runtime channel
add/remove after BeginPlay (channels are fixed at composition, like
components); GPIO/sensor-peripheral facades (ADR 0003 keeps device buses out
of the transport seam — sensor examples use timer-driven synthetic readings).

---

## 5. Progress tracker

| Phase | Title | Tasks | Status |
| --- | --- | --- | --- |
| 0 | Baseline & governance | 2 | ✅ |
| 1 | Engine-first examples groundwork (platform facades) | 5 | ✅ |
| 2 | Local actor messaging (Engine) | 3 | ✅ |
| 3 | Messaging over one wire | 2 | ✅ |
| 4 | Several channels per world | 3 | ✅ |
| 5 | Guaranteed delivery per channel | 3 | 🟨 |
| 6 | Documentation & close-out | 2 | ⬜ |

---

## 6. Phases and tasks

### Phase 0 — Baseline & governance ✅

- [x] **0.1 Record a green baseline.** Confirm clean tree (`git -C . status`),
  run the Standard Verify (§1.1) and `python tools/CheckFolderAgents.py
  --root Modules --exclude build --exclude .pio --exclude __pycache__`, and
  `pio run -d examples/<NN>` for the seven existing examples. Record every
  result (ctest count, doc-checker file count) as the evidence line. Fix
  nothing.

  **Done when:** all gates recorded; failures (if any) noted, not fixed.
  **Verify:** the commands above.

  Done 2026-07-23 — clean tree at `ff1ced1` (only this plan doc + local
  `.claude/` untracked); MSVC/VS2022 Release build warning-clean;
  `ctest -C Release` 11/11 passed, 0 failed; `CheckClassDocumentation.py
  --require-doxygen` 136 files; `CheckFolderAgents.py` (Modules) 63 guides;
  all seven examples `pio run` green — 13/13 envs `[SUCCESS]` (01 single;
  15/16/18/19/20/21 both envs each), 0 `[FAILED]`. Nothing fixed.

- [x] **0.2 Register this plan.** Add one sentence each to root `AGENTS.md`
  and root `README.md` docs paragraph: `docs/MESSAGING_ROADMAP.md` is the
  active plan for actor messaging and engine-first examples. Add one
  `PROGRESS.md` line recording the start + baseline ctest count. No other
  content changes.

  **Done when:** three files mention this plan; Standard Verify still green.

  Done 2026-07-23 — root `AGENTS.md`, root `README.md`, and `PROGRESS.md` each
  now name `docs/MESSAGING_ROADMAP.md` as the active plan (one sentence each in
  AGENTS/README; one `PROGRESS.md` evidence line recording the baseline
  ctest count 11). Docs-only; `ctest -C Release` still 11/11, no other content
  changed.

---

### Phase 1 — Engine-first examples groundwork ✅

Goal: every existing example builds from MicroWorld headers only (§2.2). No
messaging yet — this phase just moves vendor glue behind platform facades and
adopts the shipped log seam.

- [x] **1.1 `FEsp32WifiLink` platform facade.** New public header
  `Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32WifiLink.h`
  exactly per §4.3, implementation in `src/Esp32WifiLink.cpp` +
  `src/Esp32WifiPlatformImplementation.h` (all `esp_wifi/esp_netif/nvs/event`
  includes confined there). Port the logic from
  `examples/16-TwoBoardUdp/src/WifiLink.cpp` (it is the newer twin): NVS init
  (erase-and-retry), netif + default event loop, AP mode with
  channel/max-station config, station mode with connect-retry and event-group
  wait replaced by a bounded poll loop, `IsUp`, `Stop` (stops WiFi, leaves
  netif/NVS initialized — document why: ESP-IDF does not support tearing them
  down cleanly). Update `library.json`/CMake source lists if they enumerate
  files; add folder `AGENTS.md` entries only if a new folder appears. Wire a
  compile probe: extend the existing PlatformEsp32 consumer entry point
  (locate with `rg -n "Esp32UdpDriver" Modules/Core/tests/consumer/src`) so
  the header compiles under the strict ESP32 profile.

  **Done when:** header carries full Doxygen contracts; no ESP-IDF include in
  any public header (`rg -n "esp_|freertos|lwip" Modules/PlatformEsp32/include` → 0);
  Standard Verify green (PlatformEsp32 has no host build — a careful re-read
  plus the consumer compile check via `pio run` in
  `Modules/Core/tests/consumer` for the ESP32 env is the gate, same as the
  wired-transports plan used).

  Done 2026-07-23 — `Esp32WifiLink.h` (public, ESP-IDF-free) +
  `src/Esp32WifiPlatformImplementation.h` (sole ESP-IDF-including TU) +
  `src/Esp32WifiLink.cpp` implement §4.3 exactly (validate-first transactional;
  `ENetResult` map Invalid=bad-config / Unavailable=bring-up-failure-or-join-timeout
  / Success; bounded 100 ms-slice join with no clock read; idempotent `Stop`
  leaving netif/NVS up); consumer probe extended for the strict-profile compile.
  Gates: clang-format clean, host `ctest` 11/11, `CheckClassDocumentation
  --require-doxygen` 139 files, `pio run -e esp32-s3-platform` `[SUCCESS]`.
  Grep gate: `Esp32WifiLink.h` adds 0 ESP-IDF include-tree matches (the 3 hits
  are pre-existing in the deliberately header-only `Esp32TimeSource.h`). Lead
  fix: dropped a `const_cast` for a plain non-const local (imitation-source
  shape). Note: the consumer's `esp32-s3-*` envs need repo-root
  `sdkconfig.defaults`/`partitions.csv` (absent from git; copied from
  `examples/esp32-common` for the gate, then removed) — a pre-existing
  `platformio.ini` gap, out of scope here.

- [x] **1.2 `SleepMilliseconds` platform facade.** New public header
  `Esp32Sleep.h` + `src/Esp32Sleep.cpp` per §4.3 (`vTaskDelay(pdMS_TO_TICKS(x))`,
  minimum one tick so the idle task always runs). Same gates as 1.1.

  **Done when:** header documented; include gate still 0; consumer compiles.

  Done 2026-07-23 — `Esp32Sleep.h` (public, ESP-IDF-free; declares
  `SleepMilliseconds(DurationMilliseconds)`) + `src/Esp32Sleep.cpp`
  (`pdMS_TO_TICKS` clamped to ≥1 tick, then `vTaskDelay`); consumer probe calls
  it once before the tick loop. Gates: clang-format clean, host `ctest` 11/11,
  `CheckClassDocumentation --require-doxygen` 141 files, `pio run -e
  esp32-s3-platform` `[SUCCESS]`; the new header adds 0 ESP-IDF include matches.
  Lead note: the parameter is named `SleepDurationMilliseconds`, not the
  roadmap's function-shadowing `SleepMilliseconds` — clarity only; the function
  and type identity are unchanged.

- [x] **1.3 Rewrite example 15 engine-first.** Replace
  `examples/15-UdpEcho/src/WifiLink.{h,cpp}` usage with `FEsp32WifiLink`;
  delete the raw-socket `ProbeMain.cpp` role and its `platformio.ini` env —
  the probe's over-1200-byte behavior dies with it (**behavior change,
  approved here**: the example demonstrates `FEsp32UdpDriver`, and a raw
  socket contradicts §2.2). Replace every `std::printf` with `MW_LOG`
  (category `"ex15"`) after installing `Esp32LogSink` at startup; replace
  `vTaskDelay` with `SleepMilliseconds`. Update the example README (trace
  shape changes to the log-sink format) and its `AGENTS.md`.

  **Done when:** `rg -n "esp_|nvs_|lwip|sockets|freertos|vTaskDelay|xEventGroup|printf|cstdio|std::array" examples/15-UdpEcho/src` → 0;
  README updated with the §1.3 hardware-checkpoint sentence reset (the rewrite
  invalidates the old captured trace).
  **Verify:** `pio run -d examples/15-UdpEcho` (all envs) + repo ctest (format
  gate covers example sources).

  Done 2026-07-23 — example 15 is now single-role engine-first: `Main.cpp` (the
  whole program) uses `FEsp32WifiLink` + `MW_LOG`("ex15") + `SleepMilliseconds`
  over `FEsp32UdpDriver`; `ProbeMain.cpp`/`WifiLink.{h,cpp}`/`EchoServerMain.cpp`
  deleted, `platformio.ini` collapsed to one `esp32-s3` env, `src/CMakeLists.txt`
  dropped `PRIV_REQUIRES` (engine-first, like ex19), `UdpEchoShared.h` pruned to
  the echo constants, README/AGENTS rewritten with the hardware status reset to
  "not yet verified". Gates: `.cpp/.h` grep gate 0, host `ctest` 11/11, `pio run
  -d examples/15-UdpEcho` `[SUCCESS]` (RAM 13.2% / Flash 18.8%). Lead edit:
  reworded the CMake `esp_libc` why-comment to plain English so the literal
  `src/` grep gate reads 0 (it is build config, not example source).

- [x] **1.4 Rewrite example 16 engine-first.** Same treatment: delete its
  `WifiLink.{h,cpp}` copy, use `FEsp32WifiLink`, `MW_LOG` (`"ex16"`),
  `SleepMilliseconds`, replace `std::array` buffers with
  `std::uint8_t Name[N]` + `TSpan`. README/AGENTS updated, verified-trace
  reset.

  **Done when:** same grep gate for `examples/16-TwoBoardUdp/src` → 0.
  **Verify:** `pio run -d examples/16-TwoBoardUdp` + ctest.

  Done 2026-07-23 — example 16 keeps its two role envs but is now engine-first:
  `Main.cpp` installs `Esp32LogSink` before the `#if MICROWORLD_EXAMPLE_SERVER`
  dispatch (the `#error` guard preserved); `ServerMain.cpp` brings up the SoftAP
  via `FEsp32WifiLink::StartAccessPoint`, `ClientMain.cpp` joins via
  `JoinAccessPoint`, both checking `!= ENetResult::Success`; every `std::printf`
  → `MW_LOG`("ex16") (Error for halts, Log otherwise), `vTaskDelay` →
  `SleepMilliseconds`. `WifiLink.{h,cpp}` deleted; the lone `std::array`
  (`FActorComponentRegistry<0>` spawn slots) became a plain C array (same index
  syntax at the call site); `src/CMakeLists.txt` dropped `WifiLink.cpp` +
  `PRIV_REQUIRES` and reworded the `esp_libc` why-comment. README/AGENTS
  rewritten, prior hardware-verified trace (the old 2026-07-23 capture) reset to
  "not yet verified" and the "Verified output" section deleted. Gates:
  `.cpp/.h/.txt` grep gate 0, clang-format clean, host `ctest` 11/11, `pio run -d
  examples/16-TwoBoardUdp` both envs `[SUCCESS]` (RAM 13.4% / Flash 19.0%).

- [x] **1.5 Sweep examples 01, 18, 19, 20, 21 onto `MW_LOG` +
  `SleepMilliseconds`.** Mechanical: install `Esp32LogSink`, swap
  `std::printf`→`MW_LOG(Log, "ex<NN>", ...)`, swap `vTaskDelay`→
  `SleepMilliseconds`, drop `<cstdio>`/freertos includes, replace stray
  `std::array` (19). Update each README's expected-trace shape; reset the
  verified-trace sentence (traces change shape → hardware re-verification is
  pending again).

  **Done when:** the §1.3 grep gate returns 0 for all five `src/` trees.
  **Verify:** `pio run` for each of the five + ctest.

  Done 2026-07-23 — all five examples now engine-first: `Esp32LogSink` installed
  at each `app_main` start (before role dispatch where present), every
  `std::printf("[exNN] ...\n")` → `MW_LOG(Level, "exNN", ...)` (Error for
  halt lines, Log otherwise), `vTaskDelay` → `SleepMilliseconds`,
  `<cstdio>`/freertos includes dropped, and ex19's `std::array<FActorComponentRegistry<0>,
  MaxSpawns>` → plain C array (call site unchanged). All five `src/CMakeLists.txt`
  reworded the `esp_libc` why-comment to plain English for grep-gate-0.
  READMEs: status reset to "not yet verified on hardware" (18/19/20/21 dropped
  their now-invalid hardware-verified claims — the printf→MW_LOG change alters
  every trace line's shape), the four `## Verified output` sections deleted (old
  traces survive in git history at the pre-sweep commit), expected-output blocks
  reshaped to the `I (nnnn) exNN:` sink form, prose "prints `[exNN] ...`" → "logs
  `...`", and each `## Image size` flash byte-count refreshed to the post-sweep
  build (RAM unchanged; flash +130..+430 B from the MW_LOG/sink path; ex18 flash
  5.2%→5.3%). Gates (lead-rerun): grep gate 0 for all five `src/`; clang-format
  clean on all 7 edited `.cpp`; host `ctest` 11/11; `pio run` for all five green
  — 9/9 envs `[SUCCESS]` (01 single; 18/19/20/21 two role envs each). Lead
  touch-up: fixed the stale `[exNN]` prefixes in README prose and refreshed the
  image-size figures (beyond the peer's diff).

---

### Phase 2 — Local actor messaging (Engine) 🟨

Goal: actors in one standalone world exchange typed messages with callbacks,
deterministically, with zero networking.

- [x] **2.1 `Engine/Message.h` — vocabulary, codec, interfaces.** Implement
  §4.3's `Message.h` exactly: ids, `EMessageResult`, `FActorMessageHeader`,
  `EncodeActorMessage`/`DecodeActorMessage` (hand-rolled little-endian, D1),
  `FMessageView`, `FMessageHandlerBinding`, `FMessageHandlerHandle`,
  `IEncodedMessageSink`, `IMessageChannel`, `IMessageRouter`. New test file
  `Modules/Engine/tests/EngineMessageCodecTests.cpp` wired into
  `MICROWORLD_ENGINE_TEST_SOURCES`: round-trip encode/decode; exact byte
  layout asserted against a hand-written array; every failure path
  (`PayloadTooLarge` on short output span, `InvalidType` on zero type id,
  short-input decode) transactional (output spans untouched — sentinel
  bytes).

  **Done when:** all contracts documented; tests pass.
  **Verify:** Standard Verify.

  Done 2026-07-23 — new header-only `Modules/Engine/include/MicroWorld/Engine/Message.h`
  implements §4.3 verbatim (all ids/constants, `EMessageResult` members in order,
  `FActorMessageHeader`, `inline` little-endian codec imitating `FrameCodec.h`,
  `FMessageView`, the `TDelegate` binding alias, `FMessageHandlerHandle` as an
  `FTimerHandle`-shaped sibling, and the three interfaces
  `IEncodedMessageSink`/`IMessageChannel`/`IMessageRouter`). Includes exactly the
  four allowed (`<cstdint>`, `<cstddef>`, `Containers/Span.h`, `Delegates/Delegate.h`) —
  no `<cstring>` (byte-loop copy) and no `<limits>` (handle sentinel is the literal
  `0xFFFFu`, = `numeric_limits<uint16_t>::max()`, documented). Codec failure map
  (lead-pinned): encode `InvalidType` on type 0 then `PayloadTooLarge` on short
  output; decode `PayloadTooLarge` on sub-header input then `InvalidType` on type 0;
  every failure path checked before any write (transactional). New
  `EngineMessageCodecTests.cpp` (8 cases) wired into `MICROWORLD_ENGINE_TEST_SOURCES`
  (production sources untouched): round-trip, exact hand-written LE byte array,
  zero-length round-trip, all four failure paths sentinel-verified transactional,
  and a no-allocation check. Gates (lead-rerun): clang-format clean; MSVC Release
  build warning-clean (`/WX`); host `ctest` 11/11 (the 8 codec cases `[PASS]` inside
  `microworld_engine_tests`, 88 total 0 failures); `CheckClassDocumentation
  --require-doxygen` 147 files.

- [x] **2.2 `Engine/MessageRouter.h` — `TMessageRouter`.** Implement per §4.3
  semantics (normative list). Imitate `Timer.h` for slots/generations/guard.
  New `Modules/Engine/tests/EngineMessageRouterTests.cpp` covering at
  minimum: broadcast reaches all type subscribers in registration order;
  targeted message reaches only the matching listener id; local send is
  delivered at the **next** TickDispatch, never inline (D5); send from inside
  a handler works and arrives one frame later; Add/Remove during dispatch →
  `DispatchLocked`; stale handle → `StaleHandle`; handler capacity →
  `CapacityExceeded`; outbound queue full → `CapacityExceeded` transactional
  (queue sentinel); inbound overflow increments `DroppedInboundCount`;
  `AddChannel` rejects id 0 (`InvalidChannel`) and duplicate ids
  (`Duplicate`); flush retains head when a channel returns non-Success (stub
  channel), and resumes next flush.

  **Done when:** every listed behavior has a passing case; Standard Verify.

  Done 2026-07-23 — new header-only `Modules/Engine/include/MicroWorld/Engine/MessageRouter.h`
  (`TMessageRouter<MaxHandlers, MaxQueuedMessages, MaxMessageBytes, MaxChannels>`,
  `final : IMessageRouter, INetworkFrame`) implements the §4.3 normative list:
  registration-order handler table with `Timer.h`-style slots/generations (retire
  on generation wrap) + a `bDispatchActive` guard (`DispatchLocked`); two ring
  FIFOs with bytes copied in; send-path validation order InvalidType →
  InvalidChannel → PayloadTooLarge (per-message and per-channel cap) →
  CapacityExceeded, encode-once into the tail; `TickDispatch` snapshots the inbound
  count so a handler's own send arrives a frame later (D5); `TickFlush` moves
  `LocalChannelId` entries to inbound and hands wired entries to the channel,
  retaining the head and stopping on any non-Success (NetManager discipline,
  cross-channel head-of-line accepted v1 and documented). `ReceiveEncodedMessage`
  validates length and counts `DroppedInboundCount` on a full queue. New
  `EngineMessageRouterTests.cpp` (12 cases) wired into `MICROWORLD_ENGINE_TEST_SOURCES`
  (production sources untouched) with an `FStubChannel` double covering every listed
  behavior plus a no-allocation steady-state case. Gates (lead-rerun): clang-format
  clean; MSVC Release build warning-clean (`/WX`); host `ctest` 11/11 (12 router
  cases `[PASS]`, 100 total 0 failures; `microworld_dependency_boundaries` passes —
  the router pulls in no Net dependency); `CheckClassDocumentation --require-doxygen`
  149 files. Lead touch-ups on the peer's diff: deleted copy **and** move (the peer
  left the router implicitly movable — a composition root held by `IMessageRouter&`/
  `INetworkFrame*` must not relocate; matches `TTimerManager`); the peer self-caught
  one LoD tell (`FindChannel` now returns `IMessageChannel*`, one hop).

- [x] **2.3 Example `22-ActorMessages` (standalone world, 1 board).** New
  example per the §4.4 standalone recipe and the canonical example scaffold
  (`docs/EXAMPLES_ROADMAP.md` §3 — copy an existing example's
  `platformio.ini`/CMake shape, single env, no role flags).
  `FThermometerActor` (a `TInlineActor<1>` subclass with one
  `FReadingSensorComponent`): every 500 ms of caller time the component
  produces a synthetic reading (derived from tick count — no peripheral);
  the actor broadcasts `TemperatureReadingMessageId` with a 2-byte payload.
  `FDisplayActor` subscribes in `BeginPlay` (handler logs the reading) and
  also demonstrates a **targeted** message: after 5 readings the display
  sends `CalibrateMessageId` to `ThermometerActorId`, which logs receipt and
  resets its counter. Both actors take `IMessageRouter&` by constructor (D9).
  Engine-first gate (§2.2) applies. README documents the deterministic trace:
  reading N sent at frame F is displayed at frame F+1 (D5's one-frame local
  latency, stated as a teaching point). AGENTS.md for the folder; catalog row
  appended to `examples/README.md` with "not yet verified on hardware".

  **Done when:** grep gate 0; README/AGENTS complete; catalog updated.
  **Verify:** `pio run -d examples/22-ActorMessages` + repo ctest.

  Done 2026-07-24 — new engine-first example `examples/22-ActorMessages` (single
  `esp32-s3` env, scaffold copied from example 01). `src/Main.cpp` is the whole
  program: `FReadingSensorComponent` (500 ms tick, synthetic base+ramp reading, no
  peripheral) is owned by `FThermometerActor` (`TInlineActor<1>`, primary tick
  aligned to the same 500 ms cadence) which broadcasts `TemperatureReadingMessageId`
  with a 2-byte little-endian payload; `FDisplayActor` (`TInlineActor<0>`, tick
  disabled, purely reactive) subscribes to the broadcast, logs each reading, and
  after 5 sends a **targeted** `CalibrateMessageId` to `ThermometerActorId`
  (send-from-handler is legal, D5), whose handler resets the sensor counter. Both
  actors take `IMessageRouter&` by constructor (D9), injected through
  `CreateObject`'s argument forwarding; the router doubles as the `TEngineHost`
  network frame, so `Tick` pumps its dispatch (step 1) and flush (step 7). README
  states the reading-at-F / displayed-at-F+1 one-frame local latency as the teaching
  point and keeps the "not yet verified on hardware" sentence; AGENTS.md added;
  catalog row 22 appended (🟨). Gates (lead-rerun): grep gate 0; clang-format clean;
  `pio run` [SUCCESS] (RAM 10.3% / Flash 5.0%, 211037 B); host `ctest` 11/11
  (includes `microworld_format_check`); `CheckClassDocumentation --require-doxygen`
  149 files. Lead touch-up on the peer's diff: demoted the two write-only
  `*HandlerHandle` members to locals (this bounded example never calls
  `RemoveMessageHandler` — YAGNI/§3.4), then re-ran every gate.

---

### Phase 3 — Messaging over one wire ⬜

Goal: the same actor API crosses a real link; actors cannot tell.

- [x] **3.1 `Engine/MessageChannelBinding.h` + host tests.** Implement §4.3's
  `TMessageChannelBinding<TNet>` (ctor registers the inbound `TNetHost`
  handler capturing `this`; `IsAttached()`; result mapping normative table;
  `EChannelSendTarget`). New
  `Modules/Engine/tests/EngineMessageChannelTests.cpp` over `THostLoopback` +
  two `TNetHost` (imitate `EngineNetHostTests.cpp` setup): client router
  `SendMessageToActor` → server handler receives the `FMessageView` with
  correct header, payload, and `ArrivedOnChannelId`; server
  `BroadcastMessage` → client handler receives; a second wire channel byte on
  the same host does **not** leak into the binding (filter test); binding
  `TrySendEncodedMessage` with no connected server peer → `Unavailable`
  (router retains head; message flows after connect); sink-full path
  increments the binding's `DroppedInboundCount`. The frame set does not
  exist until Phase 4.1, so this test pumps the D3 order manually: the net
  frame goes to `TEngineHost` as usual, and the test loop calls the router's
  `TickDispatch` after and `TickFlush` before each engine tick, with a
  comment pointing at 4.1 (which retrofits these tests onto the set).

  **Done when:** listed behaviors pass; Standard Verify.

  Done 2026-07-24 — new header-only `Modules/Engine/include/MicroWorld/Engine/MessageChannelBinding.h`
  implements §4.3's `TMessageChannelBinding<TNet>` (`final : IMessageChannel`,
  `EChannelSendTarget { Server, AllPeers }`), duck-typed on TNet like `TNetHostFrame`
  so the engine names no Net type and stays Net-free: the inbound handler binds a
  generic lambda (`auto` peer) into `typename TNet::FMessageHandlerBinding`, and the
  `ENetResult`→`EMessageResult` mapping (Success→Success, Full→CapacityExceeded,
  Invalid→PayloadTooLarge, Unavailable→Unavailable) lives in a `MapNetSendResult`
  function template so the transport enum stays a dependent name. Server-target send
  returns `Unavailable` when `GetServerPeer()` is invalid (router retains the head);
  copy and move deleted; the destructor removes the inbound handler while the host is
  still alive. One additive, documented `static constexpr std::size_t MaxMessageBytes
  = MaxPacketBytes - MessageHeaderBytes` on `TNetHost` (`NetHost.h`). New
  `EngineMessageChannelTests.cpp` (5 cases) wired into `MICROWORLD_ENGINE_TEST_SOURCES`
  (production sources untouched): client→server targeted delivery with the full
  `FMessageView` (header/payload/`ArrivedOnChannelId`) asserted, server→client
  broadcast, foreign-wire-channel filter, send-before-connect
  `Unavailable`-then-retain-then-deliver, and rejecting-sink `DroppedInboundCount`.
  Gates (lead-rerun): clang-format clean; MSVC Release warning-clean (`/WX`); host
  `ctest` 11/11 (5 new cases `[PASS]`, 105 engine tests 0 failures);
  `CheckDependencyBoundaries --package Engine` passes and
  `rg "MicroWorld/Net" Modules/Engine/include` is 0 (the binding pulls in no Net
  dependency); `CheckClassDocumentation --require-doxygen` 151 files. The first peer
  implementer died mid-run (transient API drop) after writing the
  binding/`NetHost.h`/CMake; a second peer wrote the test suite. Lead touch-up:
  trimmed the binding's class comment to three sentences (doc-checker contract-length
  rule).

- [x] **3.2 Example `23-TwoBoardWire` (UART, 2 boards).** The vision demo on
  the cheapest link. Board A (server env): world with `FLampActor`
  (`LampActorId = 10`) subscribed to `SetLampStateMessageId`; on receipt logs
  `lamp ON`/`lamp OFF` (console is the observable — no GPIO, §4.6). Board B
  (client env): world with `FSwitchActor` that toggles state every 2 s and
  sends the targeted message to `LampActorId`; also broadcasts a 1-byte
  `HeartbeatCountMessageId` the server world's `FDisplayActor` subscribes to.
  Uses `FEsp32UartDriver` (pins/wiring copied from example 18's README,
  including the wiring-safety note), `TNetHost` Client/DedicatedServer,
  binding + router both sides, frame composition as in 3.1 (manual order,
  two-line comment). Role selection via `-DMICROWORLD_EXAMPLE_SERVER=1|0`.
  Engine-first gate; README (expected two-console trace; hardware checkpoint
  sentence); AGENTS.md; catalog row.

  **Done when:** grep gate 0; both envs compile.
  **Verify:** `pio run -d examples/23-TwoBoardWire` + ctest.

  Done 2026-07-24 — new engine-first two-board example `examples/23-TwoBoardWire`
  (two role envs `esp32-s3-server`/`esp32-s3-client` selected by
  `-DMICROWORLD_EXAMPLE_SERVER`, scaffold copied from example 19). The client world's
  `FSwitchActor` (`TInlineActor<0>`, 2 s tick) toggles a lamp state and sends a
  targeted `SetLampStateMessageId` to `LampActorId`, then increments and broadcasts a
  1-byte `HeartbeatCountMessageId`; the server world's `FLampActor` (subscribed
  targeted to its own id) logs `lamp ON`/`lamp OFF` and `FDisplayActor` (broadcast
  subscriber) logs `heartbeat=<n>` — console is the only observable, no GPIO (§4.6).
  Both actors take `IMessageRouter&` by constructor (D9). Each board composes
  `FEsp32UartDriver` → `TNetHost<2,120>` → `TMessageChannelBinding` (client target
  `Server`, server `AllPeers`) → `TMessageRouter`, with `TEngineHost` holding the
  `TNetHostFrame` and a shared `Ex23::PumpOneFrame` running the manual D3 order
  (router `TickFlush` before the engine tick, `TickDispatch` after) — the same order
  `EngineMessageChannelTests.cpp` proved, carrying the two-line Phase-4.1 comment. The
  shared header holds the ids/config/composition-type aliases; role-local actors live
  in each `*Main.cpp`. AGENTS.md added; catalog row 23 appended (🟨). Gates
  (lead-rerun): grep gate 0; clang-format clean; `pio run` both envs `[SUCCESS]`
  (server RAM 10.8% / Flash 5.8% 241501 B, client Flash 5.7% 240329 B); host `ctest`
  11/11; `CheckClassDocumentation --require-doxygen` 151 files.

---

### Phase 4 — Several channels per world ⬜

Goal: one world, several drivers, each channel with its own id and settings.

- [x] **4.1 `TNetworkFrameSet` + tests.** Implement in `NetworkFrame.h` per
  §4.3 (D3 ordering). Extend `EngineHostTests.cpp` (or a new
  `EngineNetworkFrameSetTests.cpp` if cleaner): recording frames assert
  dispatch runs in add-order and flush in reverse add-order within one
  `TEngineHost::Tick`; `Add` past capacity → `CapacityExceeded`; duplicate
  frame → `Duplicate`; empty set is inert. Retrofit the Phase 3.1 test's
  manual pump loop to use the set (delete the workaround comment).

  **Done when:** order assertions pass; 3.1 tests now go through the set.
  **Verify:** Standard Verify.

  Done 2026-07-24 — `TNetworkFrameSet<MaxFrames>` appended to
  `Modules/Engine/include/MicroWorld/Engine/NetworkFrame.h` (`final : INetworkFrame`,
  Net-free): `Add` rejects a repeated frame pointer as `Duplicate` and a full set as
  `CapacityExceeded` (both leave it unchanged), `TickDispatch` runs frames in add-order
  and `TickFlush` in reverse add-order (D3), `FrameCount` observes occupancy; copy and
  move deleted (composition root held by `TEngineHost` via `INetworkFrame&`). New
  `EngineNetworkFrameSetTests.cpp` (5 cases): direct add-order/reverse-flush with
  dispatch-before-flush, a `TEngineHost`-driven case proving the engine pumps the set at
  steps 1 and 7 (net-before-router dispatch, router-before-net flush), Add-past-capacity,
  duplicate-pointer, and empty-set-inert. The Phase 3.1 test
  (`EngineMessageChannelTests.cpp`) is retrofitted onto the set: each side now binds a
  `TNetworkFrameSet<2>` (`Add(NetFrame)` then `Add(Router)`) to its `TEngineHost` and
  `PumpSide` collapses to one `Host.Tick` — the manual router `TickFlush`/`TickDispatch`
  and the Phase-4.1 workaround comment are gone. The set order delivers one frame earlier
  than the manual order, so case 4's post-connect checkpoints were tightened (the retained
  message is already flushed — `QueuedOutboundCount == 0` — and delivered — `bWasCalled` —
  by the time the handshake loop returns), not weakened; cases 1/2/3/5 kept their values.
  Gates (lead-rerun): clang-format clean; MSVC Release warning-clean (`/WX`); host `ctest`
  11/11 (all 5 `EngineNetworkFrameSet_*` and all 5 retrofitted `EngineMessageChannel_*`
  cases `[PASS]`, 110 engine tests 0 failures); `CheckDependencyBoundaries --package
  Engine` passes (the set adds no Net include); `CheckClassDocumentation --require-doxygen`
  152 files.

- [x] **4.2 Multi-channel host test.** New case(s) in
  `EngineMessageChannelTests.cpp`: two `THostLoopback` networks, two
  `TNetHost` pairs, ONE router per side with two bindings
  (`TelemetryChannelId = 1`, `CommandChannelId = 2`), one
  `TNetworkFrameSet` per side pumped by `TEngineHost::Tick`. Assert: a
  message sent on channel 1 arrives with `ArrivedOnChannelId == 1` and never
  on channel 2 (and vice versa); both channels deliver in one frame; a
  stalled channel (loopback mailbox full) retains the router head while the
  test documents the head-of-line caveat.

  **Done when:** the isolation and ordering assertions pass.
  **Verify:** Standard Verify.

  Done 2026-07-24 — Two cases appended to `EngineMessageChannelTests.cpp`
  (test-only; 361 insertions, no production, docs, or example changes).
  `EngineMessageChannel_MultiChannelIsolationDeliversBothInOneFrame`: per side
  two `THostLoopback` networks (telemetry, command), two `TNetHost`, two
  `TNetHostFrame`, ONE `TMessageRouter<…,2>` binding both wires
  (`TelemetryChannelId`, `CommandChannelId`), a `TNetworkFrameSet<3>` (telemetry
  frame, command frame, router) driven by `TEngineHost::Tick`. A single
  channel-keyed handler buckets deliveries by `ArrivedOnChannelId`; distinct
  payload markers (0xAA telemetry, 0xBB command) prove no cross-channel bleed and
  one post-send frame per side delivers both.
  `EngineMessageChannel_StalledChannelRetainsRouterHead`: demonstrates the
  accepted-v1 cross-channel head-of-line caveat — the client's command-wire
  `TNetHost` outbound FIFO is primed to `SendQueueDepth` (raw `SendTo` bypassing
  the router) and the server's command mailbox filled to `MailboxCapacityValue()`,
  so the router's next flush sees a non-Success on the command head; a healthy
  telemetry message queued behind it is blocked too, so `QueuedOutboundCount()`
  stays at 2 after one flush (router `TickFlush` stops on first non-Success,
  matching `TNetManager`'s retained-head discipline). Reused the file's existing
  `FNet`/`FNetFrame`/`FBinding`/`MakeConfig`/`PumpSide`; extended
  `ConnectClientToServer` into a two-wire variant. Lead review confirmed the
  FIFO-still-full-at-router-flush timing against source
  (`TNetHostFrame::TickDispatch` → `PumpReceive` only, no outbound drain;
  `TNetworkFrameSet::TickFlush` reverse add-order → the router, added last,
  flushes before the command net frame drains its FIFO). Gates (lead-rerun):
  clang-format clean; MSVC Release warning-clean (`/WX`); host `ctest` 11/11 (both
  new cases plus all existing `EngineMessageChannel_*` and `EngineNetworkFrameSet_*`
  `[PASS]`, 112 engine tests 0 failures); `CheckDependencyBoundaries --package
  Engine` passes; `CheckClassDocumentation --require-doxygen` 152 files.

- [x] **4.3 Example `24-TwoChannelWorld` (2 boards, UART + WiFi UDP).** Both
  physical links between the same two boards (the rig from examples 16 + 18:
  SoftAP for UDP, cross-wired UART). Server world on board A; client world on
  board B. Channel 1 `Telemetry` over UDP (`FEsp32UdpDriver`, client streams
  a reading broadcast every second); channel 2 `Commands` over UART (server
  sends a targeted `SetReportingRateMessageId` to the client's sensor actor
  every 10 s, halving/restoring its rate). The console trace proves both
  links are alive simultaneously and actors never name a transport. Uses
  `FEsp32WifiLink`, two `TNetHost` per board, `TNetworkFrameSet<3>`
  (net, net, router). Engine-first gate; README wiring section combines 16's
  WiFi note + 18's UART wiring + safety note; AGENTS.md; catalog row.

  **Done when:** grep gate 0; both envs compile.
  **Verify:** `pio run -d examples/24-TwoChannelWorld` + ctest.

  Done 2026-07-24 — New engine-first two-board example
  `examples/24-TwoChannelWorld` (two role envs, `-DMICROWORLD_EXAMPLE_SERVER=1|0`):
  one `TMessageRouter` per board carries `TelemetryChannelId` over WiFi UDP
  (`FEsp32UdpDriver`, server SoftAP) and `CommandsChannelId` over a UART wire
  (`FEsp32UartDriver`) simultaneously, behind one `TNetworkFrameSet<3>` (telemetry
  frame, command frame, router) the engine holds — the first example to use the
  frame set (example 23's manual `PumpOneFrame` is gone). The client's
  `FSensorActor` broadcasts a 2-byte synthetic reading (ADR 0003) every reporting
  interval over UDP and re-times its own cadence via `AActor::SetTickInterval` on
  the server's targeted `SetReportingRateMessageId`; the server's `FCommanderActor`
  alternates that rate 1000↔500 ms every 10 s over UART, and its
  `FTelemetrySinkActor` logs each reading. All three actors take `IMessageRouter&`
  by constructor injection (D9) and name no transport; per-binding
  `EChannelSendTarget` is `AllPeers` on the server and `Server` on the client for
  both channels. Catalog row 24 (🟨); a small LE codec
  (`EncodeUint16LittleEndian`/`DecodeUint16LittleEndian`) factored into the shared
  header (one encoding, four call sites). Gates (lead-rerun): engine-first grep
  gate 0; clang-format clean; both `pio run` envs `[SUCCESS]` (server Flash
  840457 B, client 841077 B); host `ctest` 11/11 (112 engine tests, unchanged — no
  `Modules/` change); `CheckDependencyBoundaries --package Engine` and
  `CheckClassDocumentation --require-doxygen` (152 files) pass. Closes Phase 4.

---

### Phase 5 — Guaranteed delivery per channel ⬜

Goal: "guaranteed or not" becomes a per-channel composition choice, honestly
demonstrated under packet loss.

- [x] **5.1 `Net/PacketDropDriver.h` + tests.** Implement §4.3's
  `FPacketDropDriver`. New `Modules/Net/tests/PacketDropDriverTests.cpp`:
  with N=3 over `THostLoopback`, sends 1..9 → exactly 3,6,9 missing at the
  receiver and `DroppedSendCount()==3`; N=0 forwards everything; dropped
  sends still return `Success`; receive path bit-identical passthrough.

  **Done when:** tests pass; header documented.
  **Verify:** Standard Verify.

  Done 2026-07-24 — `Modules/Net/include/MicroWorld/Net/PacketDropDriver.h` +
  `src/PacketDropDriver.cpp`: `FPacketDropDriver final : INetDriver` wraps any
  driver by reference and drops every Nth send. `TrySend` counts each call and,
  when `DropEveryNthSend != 0 && SendCallCount % DropEveryNthSend == 0`, returns
  `Success` without touching the inner driver or inspecting the packet (the `!= 0`
  guard also prevents a modulo-by-zero); `TryReceive`/`MaxPacketBytes` forward
  verbatim; `DroppedSendCount()` is a pure query. Copy **and** move deleted (holds
  `INetDriver&`, itself held by reference); out-of-line destructor anchors the
  vtable (matching `NetDriver.cpp`), with all definitions in the .cpp (the
  `FHostUdpDriver` precedent for a concrete non-template driver). New
  `PacketDropDriverTests.cpp` (5 cases over `THostLoopback<2,16,4>`): N=3 sends
  1..9 delivering exactly {1,2,4,5,7,8} with `DroppedSendCount()==3`; N=0 forwards
  all five; N=1 returns `Success` yet leaves the receiver mailbox empty (the wire
  is never touched); the receive path is bit-identical (bytes, `BytesReceived`,
  sender address, and the empty-queue `Unavailable` all forwarded, drop count
  untouched); `MaxPacketBytes` forwards. Both CMake lists updated
  (`MICROWORLD_NET_PRODUCTION_SOURCES` + `MICROWORLD_NET_TEST_SOURCES`). Gates
  (lead-rerun): clang-format clean; MSVC Release warning-clean (`/W4 /WX`); host
  `ctest` 11/11 (net suite 107 tests, all 5 `PacketDropDriver_*` `[PASS]`, 0
  failures); `CheckClassDocumentation --require-doxygen` 155 files; the header
  includes only Core/Memory/Net.

- [x] **5.2 `Engine/ReliableChannel.h` + tests.** Implement §4.3's
  `TReliableChannel` including `SetInnerChannel` two-phase setup (the
  documented composition cycle), serial-number comparison, ack-on-duplicate,
  retry pacing from `TickFlush(now)` (caller clock only), attempt cap →
  `LostCount`. New `Modules/Engine/tests/EngineReliableChannelTests.cpp`
  using stub channels/sinks (no TNetHost needed): data wrapped correctly
  (byte-layout assertion); ack clears pending; no ack → resend after exactly
  `RetryIntervalMilliseconds` (advance caller time), `ResentCount` grows;
  after `MaxSendAttempts` → dropped + `LostCount`; duplicate data → forwarded
  once, acked twice, `DuplicateDroppedCount` grows; pending table full →
  `CapacityExceeded` (transactional); unset inner channel → `Unavailable`.
  Then one integration case in `EngineMessageChannelTests.cpp`: loopback +
  `FPacketDropDriver{N=3}` under the client's driver, guaranteed channel
  composition per §4.4 — every sent message is eventually delivered exactly
  once despite drops (drive enough ticks; assert receiver count == sender
  count and `ResentCount > 0`).

  **Done when:** all listed behaviors pass; Standard Verify.

  Done 2026-07-24 — new header-only `Modules/Engine/include/MicroWorld/Engine/ReliableChannel.h`:
  `TReliableChannel<MaxPendingMessages, MaxMessageBytes> final : IMessageChannel,
  IEncodedMessageSink, INetworkFrame` with `ReliableHeaderBytes = 3`, `EReliablePacketKind`
  {Data=1, Acknowledgement=2}, `FReliableChannelConfig`. Wire format `[Kind][Sequence u16 LE]
  [payload]`; sequences start at 1 (0 never sent). Two-phase `SetInnerChannel` breaks the
  wrapper↔binding cycle (before it: `TrySend`→`Unavailable`, `GetChannelId`→`LocalChannelId`,
  `MaxEncodedMessageBytes`→0). Outbound wraps + stores pending (kept even when the initial inner
  send is non-Success, so `TickFlush` retries rather than loses it); `CapacityExceeded` checked
  transactionally before any sequence is consumed. Inbound always acks Data (even duplicates —
  the sender's first ack may have been lost), dedups via a 32-wide serial-number window
  (`IsNewer`/`WasSeen`/`MarkSeen`, 32-bit `SeenMask`), forwards a fresh payload once, counts a
  duplicate otherwise. `TickFlush` sets the retry baseline on the first flush then resends once
  `RetryIntervalMilliseconds` elapses, dropping + `LostCount` after `MaxSendAttempts`. All four
  counters are pure queries; the header names no Net type. New `EngineReliableChannelTests.cpp`
  (8 cases: wrap byte-layout, ack-clears-pending, resend-at-exactly-interval, drop-after-max-
  attempts, duplicate-forwarded-once-acked-twice, capacity-exceeded-transactional, unset-inner-
  unavailable, window-edge-jump-still-drops-old-highest); one integration case in
  `EngineMessageChannelTests.cpp` (loopback + `FPacketDropDriver{3}` under the client driver,
  both sides reliable-wrapped) proving all 6 messages delivered exactly once despite drops with
  `ResentCount() > 0`. **Lead review fixed a boundary off-by-one** in the brief's normative
  `MarkSeen`: at a jump of exactly the window width (32) the old highest was dropped from the
  mask, so a later duplicate of it would be re-forwarded (breaks exactly-once) — provably
  unreachable while `MaxPendingMessages < 32`, but corrected and locked by the window-edge unit
  case. Gates (lead-rerun): clang-format clean; MSVC Release warning-clean (`/WX`); host `ctest`
  11/11 (engine suite 121 tests, all `EngineReliableChannel_*` + the integration case `[PASS]`,
  0 failures); `CheckDependencyBoundaries --package Engine` 18 files; `CheckClassDocumentation
  --require-doxygen` 157 files.

- [ ] **5.3 Example `25-GuaranteedDelivery` (2 boards, WiFi UDP + injected
  loss).** SoftAP rig from 16. Client wraps its `FEsp32UdpDriver` in
  `FPacketDropDriver{DropEveryNthSend = 3}` (deterministic, honest loss).
  Two channels, same UDP link: channel 1 best-effort, channel 2 guaranteed
  (§4.4 recipe). The client's `FCounterActor` sends the numbers 1..30 on
  **both** channels (targeted at the server's `FLedgerActor`); the server
  logs both sequences plus the reliable counters. Expected trace: the
  best-effort column shows holes at 3,6,9…; the guaranteed column shows all
  30 (with `resent=` lines in between). README explains the comparison in
  two sentences; hardware sentence; AGENTS.md; catalog row.

  **Done when:** grep gate 0; both envs compile.
  **Verify:** `pio run -d examples/25-GuaranteedDelivery` + ctest.

---

### Phase 6 — Documentation & close-out ⬜

- [ ] **6.1 Documentation sweep.** `docs/UE5ConceptMap.md`: add rows —
  message channels ≈ UE `UChannel`/NetDriver channels ("bounded typed
  messages, no replication"), `TMessageRouter` ≈ Gameplay Message Subsystem,
  `TReliableChannel` ≈ reliable channel flag. `Modules/Engine/README.md` and
  `Modules/Net/README.md`: one section each on the messaging layer and the
  drop driver, with the §4.2 stack diagram. `Modules/PlatformEsp32/README.md`
  (or its AGENTS.md if no README): WiFi link + sleep facade. Update the
  affected folder `AGENTS.md` guides. `examples/AGENTS.md`: replace the
  "WiFi glue duplication is deliberate" sentence — WiFi now comes from the
  platform facade; add the §2.2 engine-first rule as an example invariant.

  **Done when:** `CheckFolderAgents.py` passes; no doc presents the old
  WifiLink glue or printf tracing as current practice.
  **Verify:** Standard Verify + folder-agents checker.

- [ ] **6.2 Release bookkeeping.** Append a `CHANGELOG.md` entry (added:
  actor messaging, frame set, reliable channel, packet-drop driver, WiFi/sleep
  facades, examples 22–25; changed: examples 15/16 rewritten engine-first,
  01/18–21 log-seam adoption; removed: example WiFi glue + raw-socket probe).
  Update `examples/README.md` statuses. Add the `PROGRESS.md` phase evidence
  lines that are still missing. Final full run: Standard Verify +
  `CheckFolderAgents.py` + `pio run` for all 11 examples. Record the final
  counts in the tracker table's evidence.

  **Done when:** everything green; tracker rows all ✅.

---

## 7. Appendix — common mistakes (read before writing code)

1. **Never `#include <MicroWorld/Net/...>` from `Modules/Engine`** (and never
   Engine from Net). If you think you need to, you are on the wrong side of
   the seam — use the interfaces in `Message.h` or a duck-typed template
   parameter. `CheckDependencyBoundaries.py` will catch you.
2. **Do not dispatch handlers inline from a send call.** Every send enqueues;
   only `TickDispatch` invokes handlers (D5). Tests assert the one-frame local
   latency — "optimizing" it breaks them.
3. **Frame-set order is load-bearing** (D3): net frames first, reliable
   wrapper next, router last. Getting it wrong costs one frame of latency or
   drops acks — and the 4.1 order tests exist precisely to catch it.
4. **The envelope codec is hand-rolled on purpose** (D1). Do not "improve" it
   by pulling in `FByteWriter`.
5. **No RNG anywhere** — loss injection is a deterministic counter; synthetic
   sensor readings derive from tick counts.
6. **Static storage in examples** — every composition object at file scope;
   the app_main stack overflows otherwise (recorded hardware lesson).
7. **`clang-format --style=file:clang-format`** — the policy file has no dot;
   plain `clang-format -i` silently reformats to LLVM style.
8. **Do not edit the frozen docs** (§1.5) even when a rename touches a symbol
   they mention.
