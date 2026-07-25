# 19-UartMessaging

Inherits `../AGENTS.md`.

## Architecture

Two roles, one source tree. `Main.cpp` is a thin dispatcher whose `app_main`
calls `RunServer()` or `RunClient()` by the `-DMICROWORLD_EXAMPLE_SERVER`
define; `ServerMain.cpp` and `ClientMain.cpp` hold the two roles and are both
always compiled (matching example 16's structure), and `UartMessagingShared.h`
defines the channels, opcode, node ids, and config builders once. The server is
a full `TEngineHost` + `TNetHostSystem` + `TNetHost` (DedicatedServer); the
client is a bare `TNetHost` (Client). Both run over `FEsp32UartDriver`.

## Concepts

- The payoff of the wired-transports plan: the application protocol above the
  driver is byte-for-byte example 16's, so the only change from WiFi UDP is the
  driver construction line — no `WifiStation`, no `NetworkConfig`, no
  `esp_netif_init`.
- Server node id 1, client node id 2; the client's `ServerAddress` is
  `MakeUartAddress(1)`. The wire is point-to-point, so those ids identify peers
  for `TNetHost` bookkeeping but never route on the wire.
- The server engine profile keeps `TNetHost` packets within the driver's
  120-byte cap and completes one GC cycle per tick (budget `{1,4,8}`), so a
  spawn arriving inside a tick never hits `LifecycleLocked`.
- All composition objects are `static` (§2.2). No socket is opened, so no
  network-stack init is needed.

## Verification

Build Verify (`docs/EXAMPLES_ROADMAP.md` §1.1): `pio run -d examples/19-UartMessaging`
builds both role environments (each compiles both role files). Hardware
checkpoint (§1.2, human-gated) flashes the server to one board and the client to
the other; the client trace must show the actor count reaching 2, as in
example 16.
