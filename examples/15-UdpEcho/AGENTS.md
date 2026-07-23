# 15-UdpEcho

Inherits `../AGENTS.md`.

## Architecture

Two composition units. `WifiStation.cpp` is ESP-IDF vendor glue: it brings up
`nvs` → `netif` → `event-loop` → WiFi-station and blocks until an IPv4 lease, so
the socket has a stack to bind. `Main.cpp`'s `app_main` is the composition root:
after WiFi is up it owns one static `FEsp32UdpDriver` and runs a bounded
poll/receive/echo loop over it. No world, no actor, no `TNetHost` — the raw
driver only.

## Concepts

- Proves the `INetDriver` UDP transport on **real WiFi**: `TrySend` / `TryReceive`
  / `PollReadable` over one lwIP socket, the first on-hardware run of the driver.
- **Ordering invariant:** the driver is constructed only after
  `ConnectWifiStation` returns true — a socket opened before lwIP exists asserts
  inside the stack. All composition objects are `static` (§2.2), never
  `app_main` stack locals.
- **Oversize is the observable unknown.** The receive buffer equals the driver's
  peek scratch (1200 B), so an oversize datagram resolves to `Full` *or* a silent
  truncation depending on whether lwIP exposes `MSG_TRUNC`; the PC client's
  length check records which. Whatever hardware shows is a finding, not a driver
  bug to patch — `Modules/` is read-only.
- Secrets live in the git-ignored `NetworkConfig.h`; only the template commits,
  and the password is never printed.

## Verification

Build Verify (`docs/EXAMPLES_ROADMAP.md` §1.1): `pio run -d examples/15-UdpEcho`
then the root `cmake --build` / `ctest`. Hardware checkpoint (§1.2, human-gated):

```sh
pio run -d examples/15-UdpEcho -t upload --upload-port <COM-port>
pio device monitor -d examples/15-UdpEcho
python examples/15-UdpEcho/tools/EchoClient.py <board-ip> 40404
```

Expect the board's `wifi ip` / `listening` / `rx` / `echo` lines and the client's
`echo: 16B OK`, plus whichever oversize outcome the hardware produces.
