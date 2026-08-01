# 25-GuaranteedDelivery

Inherits `../AGENTS.md`.

## Architecture

Two role worlds, one source tree. `Main.cpp` is a thin dispatcher whose
`app_main` calls `RunServer()` or `RunClient()` by the
`-DMICROWORLD_EXAMPLE_SERVER` define; `ServerMain.cpp` and `ClientMain.cpp`
hold the two roles and are both always compiled. `GuaranteedDeliveryShared.h`
defines the shared Messaging names, fixed endpoints, and `TEngine` alias once.
Each board owns one engine-created `FMessagingSystem` and two channels sharing
one UDP device: `BestEffort` (`bIsReliable = false`) and `Guaranteed`
(`bIsReliable = true`). The client alone wraps its device in
`FPacketDropDevice`. Every composition object that lives for the run is
`static` and allocation-free.

The reliable channel is point-to-point. The client binds `40405` and addresses
the SoftAP server at `192.168.4.1:40404`; the server binds `40404` and names
the client at `192.168.4.2:40405` on both channels. This is required because
Messaging acknowledgements go to the channel's configured address, not the
last packet sender. The demo therefore assumes the SoftAP assigns its first
and only station `192.168.4.2`.

## Concepts

- **Best-effort versus reliable, side by side, on one link.** Both channels
  carry the same `Counter` payload and share one UDP device; the channel name
  distinguishes their independent Messaging frame routes.
- **Reliability lives inside Messaging.** `bIsReliable` selects sequence
  numbers, acknowledgement processing, pending-frame storage, retry timing,
  and the bounded attempt budget. This example adds no wrapper, binding,
  transport host, router, session, or frame-set composition.
- **Outcome evidence, not mechanism counters.** The server records distinct
  values in a bounded bitmask and emits `guaranteed complete 30/30; best-effort
  <m>/30`; the client logs injected send loss and only reports a nonzero
  reliable-abandonment count as an error.
- **Loss injection is client send-only.** The receiver has no duplicate
  suppression, so dropping a server acknowledgement would retransmit an
  already-delivered counter value and corrupt the comparison. Never wrap the
  server device or make the injector drop receives.
- **Actors name no transport (D9).** `FLedgerActor` and `FCounterActor` take
  `FMessagingSystem&` by constructor injection and never see a device, UDP, or
  the drop injector.

## Verification

Build Verify (`../AGENTS.md`): `pio run -d
examples/25-GuaranteedDelivery` builds both role environments, then the root
`cmake --build` / `ctest` runs the repo-wide format and unit-test gates.
Hardware checkpoint (`../AGENTS.md`, human-gated) flashes the server to one
board and the client to the other -- no wiring, WiFi only -- and retains the
server completion line plus client drop evidence.
