# 18-TwoBoardUart

Inherits `../AGENTS.md`.

## Architecture

One composition root (`app_main`) owns one static `FEsp32UartDriver` and drives
a bare ping-pong directly over its `TrySend`/`TryReceive` — no `TNetManager`,
no `TNetHost`, no world. The role (node 1 vs node 2) is a compile-time constant
from `-DMICROWORLD_EXAMPLE_NODE_ID`, so the two build environments share one
source file.

## Concepts

- Makes the `INetDriver` seam observable over a wire: the volley loop is
  transport-agnostic, so this is example 17 with only the driver construction
  line changed.
- The wire is point-to-point, so the destination `MakeUartAddress(PeerNodeId)`
  is validated but not routed; the sender identity a receiver prints comes from
  the frame's own source-id byte via `UartAddressNodeId`.
- Static driver and RX buffer, never `app_main` stack locals (§2.2). No WiFi and
  no netif init — a UART opens no socket.
- A wired link is fast and lossless at 30 cm: a stalled counter is a defect to
  investigate, not radio weather to tolerate.

## Verification

Build Verify (`docs/EXAMPLES_ROADMAP.md` §1.1): `pio run -d examples/18-TwoBoardUart`
builds both role environments. Hardware checkpoint (§1.2, human-gated) flashes
node 1 to one board and node 2 to the other and expects both monitors to show
the counter climbing alternately with no stalls; that checkpoint also flips
task 1.1's Hardware-verified box (`docs/WIRED_TRANSPORTS_ROADMAP.md`).
