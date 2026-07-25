# 15-UdpEcho

Inherits `../AGENTS.md`.

## Architecture

Single role, one file: `Main.cpp`'s `app_main` installs `Esp32OutputDevice`, brings up the SoftAP
via `FEsp32WifiLink::StartAccessPoint`, then echoes every UDP datagram back to its sender
through `FEsp32UdpDriver`, pacing the poll loop with `SleepMilliseconds`. `UdpEchoShared.h`
holds the one copy of the demo AP config (SSID/password) and the echo port.

## Concepts

- Proves the `INetDriver` UDP transport hosting its own network with **no router**: this
  board is the SoftAP and echoes via `FEsp32UdpDriver`. No `TNetHost`, no engine — the driver
  only.
- **Ordering + storage invariants:** the driver is constructed only after
  `FEsp32WifiLink::StartAccessPoint` returns `Success`; all composition objects (`WifiLink`,
  `Driver`, `RxBuffer`) are `static` (§2.2).
- No secrets: the SoftAP SSID/password are fixed demo values in the committed shared header,
  not a real network's credentials.

## Verification

Build Verify (`docs/EXAMPLES_ROADMAP.md` §1.1): `pio run -d examples/15-UdpEcho` (single
`esp32-s3` env) then the root `cmake --build` / `ctest`. Hardware checkpoint (§1.2,
human-gated): flash one board, join its SoftAP from a phone or second device, send a UDP
datagram to `192.168.4.1:40404`, and capture the `rx` / `echo` lines. Pending.
