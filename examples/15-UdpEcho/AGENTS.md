# 15-UdpEcho

Inherits `../AGENTS.md`.

## Architecture

Two roles selected by `-DMICROWORLD_EXAMPLE_SERVER`; `Main.cpp` dispatches to
`RunEchoServer()` (`EchoServerMain.cpp`) or `RunProbe()` (`ProbeMain.cpp`).
`WifiLink.cpp` is ESP-IDF glue exposing `StartSoftAccessPoint` (echo role) and
`JoinAccessPoint` (probe role), so the two boards form their own network with no
router. `UdpEchoShared.h` holds the one copy of the demo AP config, the fixed
echo-server IP (`192.168.4.1`), and the payload sizes.

## Concepts

- Proves the raw `INetDriver` UDP transport **board-to-board over WiFi**, no
  router: the echo board hosts a SoftAP and echoes via `FEsp32UdpDriver`; the
  probe joins and drives it. No `TNetHost`, no engine — the driver only.
- **Ordering + storage invariants:** the driver is constructed only after the
  WiFi bring-up returns true; all composition objects are `static` (§2.2).
- **The oversize probe needs a non-driver sender.** `FEsp32UdpDriver::TrySend`
  rejects payloads over `UdpMaxPacketBytes` (1200) as `Invalid`, so the probe
  uses a **raw lwIP socket** to fire a ~1300-byte datagram — the only way to make
  the echo server's oversize *receive* path run. Its outcome (`Full` vs silent
  truncation) is captured on the echo server's console and is a finding to record,
  never a driver patch (`Modules/` is read-only).
- No secrets: the SoftAP SSID/password are fixed demo values in the committed
  shared header, not a real network's credentials.

## Verification

Build Verify (`docs/EXAMPLES_ROADMAP.md` §1.1): `pio run -d examples/15-UdpEcho`
(both role envs) then the root `cmake --build` / `ctest`. Hardware checkpoint
(§1.2, human-gated): flash echo to one board, probe to the other, capture the
echo board's `rx` / `echo` / `rx oversize` lines.
