# 24-TwoChannelWorld

Inherits `../AGENTS.md`.

## Architecture

Two role worlds, one source tree. `Main.cpp` is a thin dispatcher whose
`app_main` calls `RunServer()` or `RunClient()` by the
`-DMICROWORLD_EXAMPLE_SERVER` define; `ServerMain.cpp` and `ClientMain.cpp`
hold the two roles and are both always compiled, and `TwoChannelWorldShared.h`
defines message/channel names and node/pin/WiFi configuration once, so both
roles share one definition. Per board, `TEngine` owns one `FMessagingSystem` with TWO
channels — one over `FEsp32WifiDevice` (telemetry), one over
`FEsp32UartDevice` (commands). Every composition object is `static` and
allocation-free.

## Concepts

- **Two channels, two links, one world.** `TelemetryChannelName` rides
  `FEsp32WifiDevice`; `CommandsChannelName` rides `FEsp32UartDevice`; both live
  in the engine-owned Messaging system, so one actor-messaging design spans two independent
  transports at once.
- **Channels own their link policy.** The client telemetry channel addresses the
  server UDP socket; the server telemetry channel receives on its bound socket.
  Both UART channel addresses are empty because the point-to-point medium ignores them.
- **Actors name no transport (D9).** `FTelemetrySinkActor`, `FCommanderActor`,
  and `FSensorActor` all take `FMessagingSystem&` by constructor injection and
  never see a device, UDP, or UART. Their subscriptions use `MakeWeakOwner`, so
  Messaging skips and reclaims a subscription when its actor dies.
- **`AActor::SetTickInterval` re-times the sensor mid-handler.** The sensor's
  `SetReportingRate` subscriber runs during the engine's inbound Messaging
  dispatch step (`TEngine::Tick` step 1), strictly before the world advance
  step (step 3) that ticks the same actor — so calling `SetTickInterval` from
  inside the handler is safe and takes effect for that very frame.
  `FTickFunction::SetInterval` (`TickFunction.cpp`) is a plain state mutation
  with no dispatch lock or reentrancy guard, confirming this is legal.

## Verification

Build Verify (`../AGENTS.md`): `pio run -d
examples/24-TwoChannelWorld` builds both role environments, then the root
`cmake --build` / `ctest` runs the repo-wide format and unit-test gates.
Hardware checkpoint (`../AGENTS.md`, human-gated) flashes the server to one board and
the client to the other, wires the UART crossover, and expects the server
console to interleave `rx telemetry reading=<n>` (UDP) lines with `tx command
-> sensor rate=<n> ms` (UART) lines while the client's own trace shows its
reporting rate visibly halving/restoring every 10 s.
