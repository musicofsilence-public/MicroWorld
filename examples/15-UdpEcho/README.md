# 15-UdpEcho

**Feature:** a real transport behind the same `Core::ITransportDevice` interface — lwIP UDP via
`FEsp32WifiDevice` — hosting its own network with **no router**: one board hosts a SoftAP and
echoes every UDP datagram back to its sender.

> Status: not yet verified on hardware.

## What it does

Single role, one board, engine-first `src/`: hosts the SoftAP `microworld-ex15` via
`FEsp32WifiLink` — **no home WiFi and no real credentials**, the network name/password are
fixed demo values — then opens one static `FEsp32WifiDevice` on `EchoServerPort` and loops:
`PollReadable` → `TryReceive` → log `rx bytes` → `TrySend` the same bytes back to the sender
→ log the echo result.

A phone or a second device joins the AP `microworld-ex15` (password `microworld`) and sends
UDP datagrams to `192.168.4.1:40404` — the SoftAP's fixed gateway address — to see them
echoed back.

## MicroWorld APIs used

- `FEsp32WifiLink` (`StartAccessPoint`), `FEsp32AccessPointConfig`
- `FEsp32WifiDevice` (`TrySend` / `TryReceive` / `PollReadable` / `IsOpen` / `BoundPort`,
  `UdpMaxPacketBytes`), `ETransportResult`, `FReceiveResult`, `FDeviceAddress`, `UdpAddressPort`,
  `TSpan`
- `SleepMilliseconds` (loop pacing)
- `MW_LOG` / `WriteEsp32LogRecord` (installed once at `app_main` start)

## Hardware required

One ESP32-S3-DevKitC-1 board and one USB cable. **No router, no wiring, no credentials.**

## Build

```sh
pio run -d examples/15-UdpEcho
```

Builds the single `esp32-s3` environment.

## Flash and observe

Human-gated (see `../AGENTS.md`).

```sh
pio run -d examples/15-UdpEcho -t upload --upload-port <COM-x>
pio device monitor -d examples/15-UdpEcho
```

Join the SoftAP `microworld-ex15` (password `microworld`) from a phone or second device and
send a UDP datagram to `192.168.4.1:40404` to see it echoed back.

## Expected output (not yet hardware-verified)

```text
I (nnnn) ex15: wifi softap up, gateway 192.168.4.1
I (nnnn) ex15: udp open=1 udp_port=40404
I (nnnn) ex15: rx bytes=16 from_port=<p>
I (nnnn) ex15: echo result=0
```

An oversize datagram (larger than `UdpMaxPacketBytes`, 1200 B) logs a warning instead of an
echo:

```text
W (nnnn) ex15: rx oversize: datagram larger than buffer (result=Full)
```
