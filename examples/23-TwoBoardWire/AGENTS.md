# 23-TwoBoardWire

Inherits `../AGENTS.md`.

## Architecture

Two role worlds, one source tree. `Main.cpp` is a thin dispatcher whose
`app_main` calls `RunServer()` or `RunClient()` by the
`-DMICROWORLD_EXAMPLE_SERVER` define; `ServerMain.cpp` and `ClientMain.cpp`
hold the two roles and are both always compiled, and `TwoBoardWireShared.h`
defines message/channel names, node ids, UART configuration, and the `TEngine`
shape once, so both roles share one definition. Per board, `TEngine` owns one
`FMessagingSystem` with an `App` channel over `FEsp32UartDevice`; every
composition object is `static`, allocation-free, and sized at compile time.

## Concepts

- **Actor messaging over a real wire.** The client's `FSwitchActor` and the
  server's `FLampActor`/`FDisplayActor` talk only through injected
  `FMessagingSystem&`; none of them sees the UART device. Weak-owned
  subscriptions become inert if collection reclaims an actor.
- **Named filters replace actor addressing.** `SetLampState` reaches the lamp
  and `HeartbeatCount` reaches the display because both subscribe on `App`
  with different message-name filters; there are no actor ids to coordinate.
- **Engine-owned frame order.** `TEngine::Tick` calls Messaging pre-advance to
  drain and dispatch UART input before world advance, then post-advance for
  reliable retries. The run loop has one `Engine.Tick` call and no manual pump.
- **Point-to-point UART address.** Each `App` channel uses an empty address
  because `FEsp32UartDevice` ignores it on this single wire.

## Verification

Build Verify (`../AGENTS.md`): `pio run -d
examples/23-TwoBoardWire` builds both role environments, then the root
`cmake --build` / `ctest` runs the repo-wide format and unit-test gates.
Hardware checkpoint (`../AGENTS.md`, human-gated) flashes the server to one board and the
client to the other; expect the server's `lamp ON`/`lamp OFF` and
`heartbeat=<n>` lines to track the client's `switch -> lamp <state>` and
`switch broadcast heartbeat=<n>` lines one wire-hop later.
