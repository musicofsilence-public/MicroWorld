# 26-MessagingOverLora

Inherits `../AGENTS.md`.

## Architecture

Two roles, one source tree. `Main.cpp` is a thin dispatcher whose `app_main`
calls `RunServer()` or `RunClient()` by the `-DMICROWORLD_EXAMPLE_SERVER`
define; `ServerMain.cpp` and `ClientMain.cpp` hold the two roles and are both
always compiled (matching example 19's structure), and `LoraMessagingShared.h`
defines the channels, opcode, node ids, and config builders once. The server is
a full `TEngineHost` + `THostPlaySystem` + `TTransportHost` (DedicatedServer); the
client is a bare `TTransportHost` (Client). Both run over `FEsp32E32LoraDriver`.

## Concepts

- Same application protocol as example 19 — only the driver construction and
  the D8 session profile differ. No `WifiStation`, no `NetworkConfig`, no
  `esp_netif_init`.
- Server node id 1, client node id 2; the client's `ServerAddress` is
  `MakeLoraAddress(1)`. LoRa is a broadcast, half-duplex medium, so those ids
  identify peers for `TTransportHost` bookkeeping but never route on the air.
- Airtime drives the D8 profile: heartbeat 3000 ms, peer timeout 15000 ms, and
  the server's state broadcast paced at 1 s instead of every tick — a full E32
  frame costs hundreds of milliseconds, so the wired example's per-tick
  broadcast and 1000 ms / 5000 ms heartbeat would congest the channel.
- The server engine profile is unchanged from example 19 — its 2-byte state
  payload keeps `TTransportHost` packets trivially within the E32's 58-byte cap
  (`E32MaxPayloadBytes`) — and still completes one GC cycle per tick (budget
  `{1,4,8}`), so a spawn arriving inside a tick never hits `LifecycleLocked`.
- All composition objects are `static` (§2.2). No socket is opened, so no
  network-stack init is needed.

## Verification

Build Verify (`../AGENTS.md`): `pio run -d examples/26-MessagingOverLora`
builds both role environments (each compiles both role files). Hardware
checkpoint (`../AGENTS.md`, human-gated; RADIO roadmap task 1.4) flashes the server to
one board and the client to the other; the client trace must show the actor
count reaching 2, as in example 19.
