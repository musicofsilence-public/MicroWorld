# 24-TwoChannelWorld

Inherits `../AGENTS.md`.

## Architecture

Two role worlds, one source tree. `Main.cpp` is a thin dispatcher whose
`app_main` calls `RunServer()` or `RunClient()` by the
`-DMICROWORLD_EXAMPLE_SERVER` define; `ServerMain.cpp` and `ClientMain.cpp`
hold the two roles and are both always compiled, and `TwoChannelWorldShared.h`
defines the message/actor/channel ids, node/pin/WiFi configuration, and the
`TNetSystem`/`TEngine` type shapes once (DRY within this one example). Per
board, `TNetSystem` owns TWO `TNetHost` values — one over `FEsp32UdpDriver`
(telemetry), one over `FEsp32UartDriver` (commands) — plus their bindings,
one shared `TMessageRouter`, and its ordered engine-system set. Every
composition object is `static` and allocation-free.

## Concepts

- **Two channels, two links, one world.** `TelemetryChannelId` rides
  `FEsp32UdpDriver`; `CommandsChannelId` rides `FEsp32UartDriver`; both bind to
  the same router, so one actor-messaging design spans two independent
  transports at once.
- **`TNetSystem` owns the link policy.** Its derived channel wire byte matches
  the channel id; server modes target `AllPeers`, client mode targets `Server`,
  and its internal set pumps net -> reliable -> router in pre-advance order.
- **Actors name no transport (D9).** `FTelemetrySinkActor`, `FCommanderActor`,
  and `FSensorActor` all take `IMessageRouter&` by constructor injection and
  never see `TNetHost`, a driver, UDP, or UART.
- **`AActor::SetTickInterval` re-times the sensor mid-handler.** The sensor's
  `SetReportingRateMessageId` handler runs during the engine's inbound network
  dispatch step (`TEngine::Tick` step 1), strictly before the world advance
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
