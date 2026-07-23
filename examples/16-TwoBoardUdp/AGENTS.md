# 16-TwoBoardUdp

Inherits `../AGENTS.md`.

## Architecture

One source tree, two roles selected by `-DMICROWORLD_EXAMPLE_SERVER`; `Main.cpp`
dispatches to `RunServer()` (`ServerMain.cpp`) or `RunClient()` (`ClientMain.cpp`).
`WifiLink.cpp` is the same ESP-IDF glue as example 15 (duplicated per §2.4): the
server calls `StartSoftAccessPoint`, the client calls `JoinAccessPoint`, so the
two boards form their own network with no router. `UdpMessagingShared.h` holds
the one copy of the demo AP config, the fixed server IP (`192.168.4.1`), and the
channel/opcode/spawn protocol.

## Concepts

- Proves the **whole message design over WiFi UDP, no router**: `TNetHost`
  Hello/Welcome admission, channel-1 spawn requests, channel-2 state broadcasts,
  and the engine frame's pump steps — identical to example 19, only the driver
  and the WiFi bring-up differ. The same design rides UART, a bare wire, or WiFi.
- **SoftAP topology:** the server hosts the AP at the fixed gateway
  `192.168.4.1`, so the client's server address is a constant — no IP discovery
  or copy step, and no real credentials (demo SSID/password in the shared header).
- **No node id** (unlike example 19): a UDP peer is keyed by its socket address,
  which `TNetHost` learns from the datagram.
- **Ordering + storage invariants:** driver after the WiFi bring-up returns true;
  all composition objects `static`.
- **Shared-AP caveat:** the server socket binds `INADDR_ANY`, so a stray datagram
  larger than the 256-byte packet capacity can head-of-line-wedge it silently;
  recovery is a server reboot (documented in the README), not a driver patch —
  `Modules/` is read-only.

## Verification

Build Verify (`docs/EXAMPLES_ROADMAP.md` §1.1): `pio run -d examples/16-TwoBoardUdp`
(both role envs) then the root `cmake --build` / `ctest`. Hardware checkpoint
(§1.2, human-gated): flash both roles, capture the server console showing the
actor count reach 2 (driven by the remote client's requests).
