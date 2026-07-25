# 24-TwoChannelWorld

Inherits `../AGENTS.md`.

## Architecture

Two role worlds, one source tree. `Main.cpp` is a thin dispatcher whose
`app_main` calls `RunServer()` or `RunClient()` by the
`-DMICROWORLD_EXAMPLE_SERVER` define; `ServerMain.cpp` and `ClientMain.cpp`
hold the two roles and are both always compiled, and `TwoChannelWorldShared.h`
defines the message/actor/channel ids, node/pin/WiFi configuration, and the
`TNetHost`/`TMessageRouter`/`TEngineHost`/`TEngineSystemSet` type shapes once
(DRY within this one example). Per board: TWO `TNetHost` — one over
`FEsp32UdpDriver` (telemetry), one over `FEsp32UartDriver` (commands) — each
wired to the same `TMessageRouter` through its own `TMessageChannelBinding`,
all pumped by one `TEngineSystemSet<3>` (telemetry net frame, command net
frame, router) the engine holds. Every composition object is `static` and
allocation-free.

## Concepts

- **Two channels, two links, one world.** `TelemetryChannelId` rides
  `FEsp32UdpDriver`; `CommandsChannelId` rides `FEsp32UartDriver`; both bind to
  the same router, so one actor-messaging design spans two independent
  transports at once.
- **`EChannelSendTarget` per binding.** The server's bindings send `AllPeers`
  on both channels (it only receives telemetry and only sends commands, but
  `AllPeers` is the correct server-side target either way); the client's
  bindings send `Server` on both (its one connected peer).
- **`TEngineSystemSet` replaces example 23's manual pump.** This is the first
  example to use it (Phase 4.1): the run loop is just `Engine.Tick(now)` +
  `SleepMilliseconds`, with `PreAdvance` running the two net frames then the
  router in add-order, and `PostAdvance` running the same three in reverse —
  see `EngineSystem.h`'s D3 ordering.
- **Actors name no transport (D9).** `FTelemetrySinkActor`, `FCommanderActor`,
  and `FSensorActor` all take `IMessageRouter&` by constructor injection and
  never see `TNetHost`, a driver, UDP, or UART.
- **`AActor::SetTickInterval` re-times the sensor mid-handler.** The sensor's
  `SetReportingRateMessageId` handler runs during the engine's inbound network
  dispatch step (`TEngineHost::Tick` step 1), strictly before the world advance
  step (step 3) that ticks the same actor — so calling `SetTickInterval` from
  inside the handler is safe and takes effect for that very frame.
  `FTickFunction::SetInterval` (`TickFunction.cpp`) is a plain state mutation
  with no dispatch lock or reentrancy guard, confirming this is legal.

## Verification

Build Verify (`docs/EXAMPLES_ROADMAP.md` §1.1): `pio run -d
examples/24-TwoChannelWorld` builds both role environments, then the root
`cmake --build` / `ctest` runs the repo-wide format and unit-test gates.
Hardware checkpoint (§1.2, human-gated) flashes the server to one board and
the client to the other, wires the UART crossover, and expects the server
console to interleave `rx telemetry reading=<n>` (UDP) lines with `tx command
-> sensor rate=<n> ms` (UART) lines while the client's own trace shows its
reporting rate visibly halving/restoring every 10 s.
