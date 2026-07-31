# 16-TwoBoardUdp

Inherits `../AGENTS.md`.

## Architecture

One source tree, two roles selected by `-DMICROWORLD_EXAMPLE_SERVER`; `Main.cpp`
installs `WriteEsp32LogRecord` then dispatches to `RunServer()` (`ServerMain.cpp`) or
`RunClient()` (`ClientMain.cpp`). The server calls `FEsp32WifiLink::StartAccessPoint`,
the client `FEsp32WifiLink::JoinAccessPoint`, so the two boards form their own
network with no router; both roles log via `MW_LOG` and pace their run loop with
`SleepMilliseconds`. `UdpMessagingShared.h` holds the one copy of the demo AP
config, the fixed server IP (`192.168.4.1`), and the channel/opcode/spawn protocol.

## Concepts

- Proves the **whole message design over WiFi UDP, no router**: `TTransportHost`
  Hello/Welcome admission, channel-1 spawn requests, channel-2 state broadcasts,
  and the engine frame's pump steps — identical to example 19, only the driver
  and the WiFi bring-up differ. The same design rides UART, a bare wire, or WiFi.
- **SoftAP topology:** the server hosts the AP at the fixed gateway
  `192.168.4.1`, so the client's server address is a constant — no IP discovery
  or copy step, and no real credentials (demo SSID/password in the shared header).
- **No node id** (unlike example 19): a UDP peer is keyed by its socket address,
  which `TTransportHost` learns from the datagram.
- **Ordering + storage invariants:** driver after the WiFi bring-up returns `Success`;
  all composition objects `static`.
- **Shared-AP caveat:** the server socket binds `INADDR_ANY`, so a stray datagram
  larger than the 256-byte packet capacity can head-of-line-wedge it silently;
  recovery is a server reboot (documented in the README), not a driver patch —
  `Modules/` is read-only.

## Verification

Build Verify (`../AGENTS.md`): `pio run -d examples/16-TwoBoardUdp`
(both role envs) then the root `cmake --build` / `ctest`. Hardware checkpoint
(`../AGENTS.md`, human-gated): flash both roles, capture the server console showing the
actor count reach 2 (driven by the remote client's requests) — pending re-verification
after the `FEsp32WifiLink`/`MW_LOG` rewrite, which changed the trace shape.
