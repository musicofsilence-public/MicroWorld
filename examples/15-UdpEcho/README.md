# 15-UdpEcho

**Feature:** a real transport behind the same `INetDriver` seam — lwIP UDP via
`FEsp32UdpDriver` — proven **board-to-board over WiFi with no router**. One board hosts a
SoftAP and echoes datagrams; a second board joins that AP and probes it.

> Status: hardware-verified on two ESP32-S3 boards (2026-07-23) — echo round trip over the
> board-hosted SoftAP, and the oversize datagram observed to truncate silently to 1200 B.

## What it does

Two roles, selected at build time (`-DMICROWORLD_EXAMPLE_SERVER`), talking directly with
**no home WiFi and no real credentials** — the network name/password are fixed demo values:

1. **echo** role (`esp32-s3-echo`) hosts the SoftAP `microworld-ex15` and prints
   `[ex15] wifi ip=192.168.4.1`. It opens one static `FEsp32UdpDriver` on `EchoServerPort`
   and loops: `PollReadable` → `TryReceive` → print `rx bytes` → `TrySend` the same bytes
   back to the sender → print the echo result. **This is the board to capture.**
2. **probe** role (`esp32-s3-probe`) joins the AP, then:
   - sends the normal payload *through the driver* and checks the echo byte count
     (`MATCH`) — proving the driver's send **and** receive over WiFi;
   - fires one ~1300-byte datagram through a **raw lwIP socket** — because the driver
     refuses to send over `UdpMaxPacketBytes` (1200), a raw socket is the only way to make
     an oversize datagram *arrive* at the echo server and exercise its oversize receive path.

**Oversize outcome (the one branch the compile-only Phase 5.2 proof left `UNVERIFIED`).**
The echo server's receive buffer equals the driver's peek scratch (1200), so the >1200-byte
datagram resolves as *either* `Full` (when lwIP exposes `MSG_TRUNC` — the echo server prints
`rx oversize`, no echo) *or* a silently truncated `Success` of 1200 bytes. The hardware run
records which — captured on the echo server's console.

## MicroWorld APIs used

- `FEsp32UdpDriver` (`TrySend` / `TryReceive` / `PollReadable` / `IsOpen` / `BoundPort`,
  `UdpMaxPacketBytes`), `ENetResult`, `FNetReceiveResult`, `FNetAddress`, `UdpAddressPort`,
  `MakeUdpAddress`, `TSpan`
- Raw lwIP sockets (`lwip_socket`/`lwip_sendto`/`lwip_select`/`lwip_recvfrom`) — **only** in
  the probe's oversize path, deliberately bypassing the driver's send cap.

## Hardware required

Two ESP32-S3-DevKitC-1 boards and two USB cables. **No router, no wiring, no credentials.**

## Build

```sh
pio run -d examples/15-UdpEcho
```

Builds both role environments (`esp32-s3-echo`, `esp32-s3-probe`).

## Flash and observe

Human-gated (see `docs/EXAMPLES_ROADMAP.md` §1.2). Flash echo to one board and probe to the
other; capture the **echo** board (its console shows the receive outcomes):

```sh
pio run -d examples/15-UdpEcho -e esp32-s3-echo  -t upload --upload-port <COM-A>
pio run -d examples/15-UdpEcho -e esp32-s3-probe -t upload --upload-port <COM-B>
pio device monitor -d examples/15-UdpEcho -e esp32-s3-echo
```

## Expected output

Echo board:

```text
[ex15] wifi ip=192.168.4.1
[ex15] echo server open=1 udp_port=40404
[ex15] rx bytes=16 from_port=<p>
[ex15] echo result=0
[ex15] rx oversize: datagram larger than buffer (result=Full)   # or a 1200-byte rx if MSG_TRUNC absent
```

Probe board:

```text
[ex15] wifi ip=192.168.4.2
[ex15] probe open=1
[ex15] probe sent normal bytes=16 result=0
[ex15] probe echo normal bytes=16 MATCH
[ex15] probe sent oversize bytes=1300 (raw socket, bypasses driver 1200 cap)
[ex15] probe oversize echo: none (server reported Full and dropped it)   # or TRUNCATED
[ex15] done (probe sent normal + oversize)
```

## Verified output

Hardware-verified 2026-07-23 on two ESP32-S3 boards, no router. The echo board (SoftAP
host) console — the probe board joined its AP and drove it:

```text
[ex15] wifi ip=192.168.4.1
[ex15] echo server open=1 udp_port=40404
I (33936) wifi:station: e0:72:a1:d5:56:9c join, AID=1, bgn, 40U
I (33996) esp_netif_lwip: DHCP server assigned IP to a client, IP is: 192.168.4.2
[ex15] rx bytes=16 from_port=58120
[ex15] echo result=0
[ex15] rx bytes=1200 from_port=58121
[ex15] echo result=0
```

The first `rx bytes=16` / `echo result=0` is the normal payload round-tripped through the
`FEsp32UdpDriver` (send **and** receive proven over the board-hosted SoftAP, zero router).

**Oversize finding (resolves the Phase 5.2 `UNVERIFIED` branch).** The probe's raw 1300-byte
datagram arrived as `rx bytes=1200` with `echo result=0` (Success) — **not** `Full`. So on
this ESP-IDF 6.0.1 / lwIP build `MSG_TRUNC` is not exposed: an oversize UDP datagram is
**silently truncated** to `UdpMaxPacketBytes` (1200) and the receive reports Success, losing
the tail. It did **not** wedge the queue (no repeated `Full`; the server kept serving). This
is a real behavioral finding, recorded here rather than patched — `FEsp32UdpDriver` lives in
the read-only `Modules/` tree.

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). Both role images carry the full
WiFi/lwIP stack; the probe is slightly larger for its raw-socket path and oversize buffers:

```text
echo   RAM:   13.2% (used 43200 bytes from 327680 bytes)
       Flash: 18.8% (used 790021 bytes from 4194304 bytes)
probe  RAM:   14.0% (used 45808 bytes from 327680 bytes)
       Flash: 18.9% (used 791729 bytes from 4194304 bytes)
```
