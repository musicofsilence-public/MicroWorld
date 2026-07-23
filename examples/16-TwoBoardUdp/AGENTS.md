# 16-TwoBoardUdp

Inherits `../AGENTS.md`.

## Architecture

One source tree, two roles selected by `-DMICROWORLD_EXAMPLE_SERVER`; `Main.cpp`
dispatches to `RunServer()` (`ServerMain.cpp`) or `RunClient()` (`ClientMain.cpp`).
`WifiStation.cpp` is the same ESP-IDF station glue as example 15 (duplicated per
§2.4). Both roles bring up WiFi first, then construct one static
`FEsp32UdpDriver`; the server wires the engine host through the `TNetHostFrame`
seam, the client runs a bare `TNetHost`. `UdpMessagingShared.h` holds the one
copy of the channel/opcode/spawn protocol.

## Concepts

- Proves the **whole message design over WiFi UDP**: `TNetHost` Hello/Welcome
  admission, channel-1 spawn requests, channel-2 state broadcasts, and the
  engine frame's inbound/outbound pump steps — identical to example 19, only the
  driver and the server-address form differ. The same design rides UART, a bare
  wire, or WiFi unchanged.
- **No node id** (unlike example 19): a UDP peer is keyed by its socket address,
  which `TNetHost` learns from the datagram, so the UART `LocalNodeId` /
  `MakeUartAddress` concept has no UDP analogue.
- **Ordering + storage invariants** as example 15: driver after `ConnectWifiStation`,
  all composition objects `static`.
- The server's `wifi ip` line is the address the operator copies into the
  client's git-ignored `NetworkConfig.h` (`kServerIpv4`).
- **Shared-LAN caveat:** the server socket binds `INADDR_ANY`, so a stray
  datagram larger than the 256-byte packet capacity can head-of-line-wedge it
  silently; recovery is a server reboot (documented in the README), not a driver
  patch — `Modules/` is read-only.

## Verification

Build Verify (`docs/EXAMPLES_ROADMAP.md` §1.1): `pio run -d examples/16-TwoBoardUdp`
(both role envs) then the root `cmake --build` / `ctest`. Hardware checkpoint
(§1.2, human-gated): flash the server, copy its IP into the client config, flash
the client, and capture the client trace showing the actor count reach 2.
