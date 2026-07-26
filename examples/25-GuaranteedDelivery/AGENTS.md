# 25-GuaranteedDelivery

Inherits `../AGENTS.md`.

## Architecture

Two role worlds, one source tree. `Main.cpp` is a thin dispatcher whose
`app_main` calls `RunServer()` or `RunClient()` by the
`-DMICROWORLD_EXAMPLE_SERVER` define; `ServerMain.cpp` and `ClientMain.cpp`
hold the two roles and are both always compiled, and
`GuaranteedDeliveryShared.h` defines the message/actor/channel ids, WiFi/UDP
configuration, and the `TNetHost`/`TMessageRouter`/`TReliableChannel`/
`TEngineHost`/`TEngineSystemSet` type shapes once (DRY within this one
example). Per board: ONE `TNetHost` over `FEsp32UdpDriver` -- on the client,
wrapped in `FPacketDropDriver` -- carrying TWO channels to the same
`TMessageRouter`: a best-effort binding straight to the router, and a
guaranteed binding wrapped in `TReliableChannel`. All pumped by one
`TEngineSystemSet<3>` (net frame, reliable channel, router) the engine holds.
Every composition object is `static` and allocation-free.

## Concepts

- **Best-effort vs guaranteed, side by side, on one link.** Both channels
  share the same `FWorldNet`; the wire-level channel byte (1 vs 2) is how the
  net demuxes them, since `TNetHost` has no concept of "channel" of its own.
- **`TReliableChannel`'s two-phase `SetInnerChannel` cycle-break.** The
  wrapper's forward sink (the router) is fixed at construction, but its inner
  channel (the binding) cannot be, because the binding's own constructor needs
  the wrapper as ITS sink -- a genuine circular dependency. The fix:
  construct the wrapper first (forward sink = router), construct the binding
  second (inbound sink = the wrapper), then call
  `Guaranteed.SetInnerChannel(GuaranteedWire)` before `Router.AddChannel`,
  since `AddChannel` reads `GetChannelId()`, which the wrapper forwards to its
  (now-bound) inner channel.
- **Frame-set add order is exact: net, reliable, router.** The net frame
  delivers inbound wire bytes to both bindings first; the reliable channel's
  `PostAdvance` then paces retries for anything still unacknowledged; the
  router dispatches last, once both channels have had their turn. Reversing
  this order would let the router run against stale channel state.
- **`FPacketDropDriver` sits below the channel demux.** It wraps the raw UDP
  driver, one layer beneath `TNetHost`, so it drops indiscriminately --
  best-effort data, guaranteed data, guaranteed acks, and heartbeats are all
  equally at risk of the same dropped send. This is why the guaranteed
  column's exact resend timing is illustrative, not fixed: which packet type
  gets hit by the Nth drop depends on send interleaving, not channel identity.
- **Actors name no transport (D9).** `FLedgerActor` and `FCounterActor` both
  take `IMessageRouter&` by constructor injection and never see `TNetHost`, a
  driver, UDP, or the drop injector.

## Verification

Build Verify (`../AGENTS.md`): `pio run -d
examples/25-GuaranteedDelivery` builds both role environments, then the root
`cmake --build` / `ctest` runs the repo-wide format and unit-test gates.
Hardware checkpoint (`../AGENTS.md`, human-gated) flashes the server to one board and
the client to the other -- no wiring, WiFi only -- and expects the server
console's best-effort column to show gaps while the guaranteed column stays
complete.
