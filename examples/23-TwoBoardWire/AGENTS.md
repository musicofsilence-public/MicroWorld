# 23-TwoBoardWire

Inherits `../AGENTS.md`.

## Architecture

Two role worlds, one source tree. `Main.cpp` is a thin dispatcher whose
`app_main` calls `RunServer()` or `RunClient()` by the
`-DMICROWORLD_EXAMPLE_SERVER` define; `ServerMain.cpp` and `ClientMain.cpp`
hold the two roles and are both always compiled, and `TwoBoardWireShared.h`
defines the message/actor ids, node ids, UART/session config builders, and the
`TNetHost`/`TMessageRouter`/`TEngineHost` type shapes once (DRY within this one
example). Per board: a `TNetHost` over `FEsp32UartDriver`, a `TMessageRouter`,
and a `TMessageChannelBinding` wiring the two together. The engine holds the
`TNetHostSystem` as its per-frame network slot; the run loop pumps the router
manually (`PostAdvance` before `Engine.Tick`, `PreAdvance` after) because
`TEngineSystemSet` does not exist until Phase 4.1 and `TEngineHost` holds
exactly one `IEngineSystem`. Every composition object is `static`,
allocation-free, sized at compile time.

## Concepts

- **Actor messaging over a real wire.** The client's `FSwitchActor` and the
  server's `FLampActor`/`FDisplayActor` talk only through `IMessageRouter&`,
  injected at construction (D9); none of them ever sees `TNetHost` or the UART
  driver.
- **`EChannelSendTarget`.** The client's binding sends to `Server` (its one
  connected peer); the server's binding sends to `AllPeers` (broadcasts reach
  every connected client, matching `TNetHost::Broadcast`'s own semantics).
- **Manual frame order = the 3.1 test's.** `Modules/Engine/tests/EngineMessageChannelTests.cpp`'s
  `PumpSide` proved `Router.PostAdvance` -> `Host.Tick` -> `Router.PreAdvance`
  is the correct per-frame order for a router bound to a live `TNetHost`; this
  example's run loop mirrors that order exactly, on both boards.
- **Only the driver differs from a WiFi build.** Swapping `FEsp32UartDriver`
  for `FEsp32UdpDriver` (and `MakeUartConfig`/`MakeUartAddress` for their WiFi
  equivalents) is the only change needed to run this same application protocol
  over WiFi instead of a wire.

## Verification

Build Verify (`../AGENTS.md`): `pio run -d
examples/23-TwoBoardWire` builds both role environments, then the root
`cmake --build` / `ctest` runs the repo-wide format and unit-test gates.
Hardware checkpoint (`../AGENTS.md`, human-gated) flashes the server to one board and the
client to the other; expect the server's `lamp ON`/`lamp OFF` and
`heartbeat=<n>` lines to track the client's `switch -> lamp <state>` and
`switch broadcast heartbeat=<n>` lines one wire-hop later.
