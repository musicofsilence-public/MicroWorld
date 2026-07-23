# 15-UdpEcho

**Feature:** a real transport behind the same `INetDriver` seam — lwIP UDP via
`FEsp32UdpDriver` over real WiFi, echoing datagrams back to a PC on the same network.

> Status: compiled for ESP32-S3; not yet verified on hardware.

## What it does

1. `ConnectWifiStation("ex15")` joins the configured 2.4 GHz WiFi and blocks until an
   IPv4 address is bound, printing `[ex15] wifi ip=<a.b.c.d>`.
2. Constructs one static `FEsp32UdpDriver` bound to `kServerPort` (default 40404) and
   prints `[ex15] listening port=40404 open=1`.
3. Loops forever: `PollReadable(250)`; on a ready socket, `TryReceive` into a static
   1200-byte buffer, print `[ex15] rx bytes=<n> from_port=<p>`, then `TrySend` the same
   bytes back to the sender and print the echo result.
4. **Oversize probe.** The receive buffer is exactly `UdpMaxPacketBytes` (1200), which
   equals the driver's internal peek scratch. A datagram larger than 1200 bytes surfaces
   as *either* `Full` (when lwIP's `MSG_TRUNC` lets the peek see the true length — the
   board prints `rx oversize` and does not echo) *or* a silently truncated `Success`
   (when it cannot — the board echoes 1200 of the sent bytes). The PC client compares
   lengths to tell the two apart. This finally exercises the one receive branch the
   compile-only Phase 5.2 proof left `UNVERIFIED`.

There is no `TNetHost` and no engine here — the example drives the driver directly, so it
is the narrowest possible proof of UDP on real WiFi.

## MicroWorld APIs used

- `FEsp32UdpDriver` (`TrySend` / `TryReceive` / `PollReadable` / `IsOpen` / `BoundPort`,
  `UdpMaxPacketBytes`), `ENetResult`, `FNetReceiveResult`, `FNetAddress`
- `UdpAddressPort` (from `MicroWorld/Net/UdpAddressCodec.h`)
- `TSpan<std::uint8_t>` / `TSpan<const std::uint8_t>`

## Hardware required

One ESP32-S3-DevKitC-1 board, one USB cable, and a PC on the same 2.4 GHz WiFi able to
reach the board's IP. No wiring.

## Configuration (before building)

Copy the committed template to the git-ignored real config and fill in your network:

```sh
cp examples/15-UdpEcho/src/NetworkConfig.example.h examples/15-UdpEcho/src/NetworkConfig.h
```

`NetworkConfig.h` (git-ignored) holds `kWifiSsid`, `kWifiPassword`, and `kServerPort`. The
password is never printed to the serial console, and the real file never enters git.

## Build

```sh
pio run -d examples/15-UdpEcho
```

## Flash and observe

Human-gated (see `docs/EXAMPLES_ROADMAP.md` §1.2). Flash the board, open the monitor, read
its IP, then run the PC client against that IP:

```sh
pio run -d examples/15-UdpEcho -t upload --upload-port <COM-port>
pio device monitor -d examples/15-UdpEcho
python examples/15-UdpEcho/tools/EchoClient.py <board-ip> 40404
```

## Expected output

Board:

```text
[ex15] wifi ip=<a.b.c.d>
[ex15] listening port=40404 open=1
[ex15] rx bytes=16 from_port=<p>
[ex15] echo result=0
[ex15] rx oversize: datagram larger than buffer (result=Full)   # only if MSG_TRUNC present
```

PC client:

```text
echo: 16B OK
oversize: no echo (board reported Full ...)   # or: oversize: echoed 1200B -> TRUNCATED 1200/1500B
```

## Verified output

Status: compiled for ESP32-S3; not yet verified on hardware.

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). The full `esp_wifi`/lwIP stack
dominates the image — far larger than the bare non-networked examples:

```text
RAM:   13.2% (used 43216 bytes from 327680 bytes)
Flash: 18.9% (used 790797 bytes from 4194304 bytes)
```
