# Porting MicroWorld

MicroWorld targets a new platform through **three adapter seams**. The runtime
itself is platform-free: it never reads a clock, never opens a socket, and
never logs to hardware. A port fills those three gaps and otherwise reuses the
shipped portable packages unchanged. Each seam below names the shipped adapter
that implements it as a worked reference.

## The three seams

### 1. Time source

The runtime never reads a clock. Every lifecycle, tick, timer, GC, and net
deadline takes a caller-supplied
[`TimePointMilliseconds`](../Modules/Core/include/MicroWorld/Time.h) (`std::uint64_t`
monotonic milliseconds). An adapter reads the real clock and feeds that value
into `TEngineHost::Tick(Now)` (or the lower-level `Advance(Now)` calls).

- ESP32 reference:
  [`FEsp32TimeSource`](../Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32TimeSource.h)
  wraps `esp_timer_get_time()` and returns `microseconds / 1000`.
- Host reference:
  [`FHostTimeSource`](../Modules/PlatformHost/include/MicroWorld/PlatformHost/HostTimeSource.h)
  uses `std::chrono::steady_clock` from a process-local baseline.

### 2. Net driver

Implement
[`INetDriver`](../Modules/Net/include/MicroWorld/Net/NetDriver.h) with
two non-blocking, transactional operations — `TrySend(const FNetAddress& To,
TSpan<const std::uint8_t>)` and `TryReceive(FNetAddress& OutFrom, TSpan<
std::uint8_t>, FNetReceiveResult&)`. On any non-`Success` result the
destination and `BytesReceived` must be unchanged. `FNetAddress` is opaque; the
adapter owns its concrete encoding and provides helpers to build/inspect it.

- Host UDP reference:
  [`FHostUdpDriver`](../Modules/PlatformHost/include/MicroWorld/PlatformHost/HostUdpDriver.h)
  over a `SOCK_DGRAM` socket on `127.0.0.1`, with
  [`MakeUdpAddress`/`IsUdpAddress`/`UdpAddressPort`](../Modules/PlatformHost/include/MicroWorld/PlatformHost/UdpAddress.h)
  for the 6-byte IPv4+port encoding.
- ESP32 UDP reference:
  [`FEsp32UdpDriver`](../Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32UdpDriver.h)
  over lwIP; same three UDP address helpers duplicated verbatim.
- ESP32 E32 LoRa reference:
  [`FEsp32E32LoraDriver`](../Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32E32LoraDriver.h)
  over the E32 UART, using the portable
  [`Net/FrameCodec.h`](../Modules/Net/include/MicroWorld/Net/FrameCodec.h)
  for CRC-16/CCITT-FALSE framing and a 1-byte broadcast
  [`LoraAddress`](../Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/LoraAddress.h).
- ESP32 wired UART reference:
  [`FEsp32UartDriver`](../Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32UartDriver.h)
  over a plain point-to-point UART — the E32 driver minus the radio — using the
  same `Net/FrameCodec.h` framing over a 1-byte point-to-point
  [`UartAddress`](../Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/UartAddress.h).
- ESP32 wired I2C reference (master/slave pair):
  [`FEsp32I2cMasterDriver` / `FEsp32I2cSlaveDriver`](../Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32I2cDriver.h)
  over one I2C bus — the master clocks whole-frame transactions, the slave receives
  through an `on_receive` ISR inbox — with a 1-byte
  [`I2cAddress`](../Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/I2cAddress.h).
- ESP32 wired SPI reference (master/slave pair):
  [`FEsp32SpiMasterDriver` / `FEsp32SpiSlaveDriver`](../Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32SpiDriver.h)
  over one full-duplex SPI bus — the master clocks fixed-size transactions, the
  slave keeps one queued — with a 1-byte
  [`SpiAddress`](../Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/SpiAddress.h).

### 3. Log sink

Install one
[`FLogSink`](../Modules/Core/include/MicroWorld/Log.h) via `SetLogSink` at startup. The
default sink is null (logging disabled). The facade is single-threaded; install
the sink before the first `MW_LOG` / `MW_LOG_MSG` call.

- ESP32 reference:
  [`Esp32LogSink`](../Modules/PlatformEsp32/include/MicroWorld/PlatformEsp32/Esp32LogSink.h)
  maps each `ELogLevel` to the matching `ESP_LOG*` macro.

## Where the adapter code lives

Adapter code goes in a **non-portable platform package** (e.g.
`Modules/PlatformHost`, `Modules/PlatformEsp32`). Such a
package:

- may include OS and vendor headers (WinSock, lwIP, ESP-IDF, `<driver/uart.h>`,
  `<chrono>`, …) and confine them to private `src/*PlatformImplementation.h` headers;
- is **excluded from `CheckDependencyBoundaries.py`** — it has no module key in
  that tool's portable table;
- **depends inward** on any portable package (Core, Memory, Object, Engine,
  Net) it needs; the reverse dependency is forbidden.

The portable packages themselves stay free of OS/vendor headers and remain
under the dependency checker.

## What a port is not

Compile success is never a runtime, timing, heap, stack, or radio claim. A
newly ported adapter that opens sockets, drives a radio, or holds large fixed
storage must be smoke-run on the real target before any runtime-readiness
claim — the Phase 6.2 measurement found two defects that were invisible to the
compile-only proof (lwIP stack uninitialized before socket use; large
composition overflowing the main task stack). See the Phase 6.2 decision row
in `docs/ROADMAP.md` §6 and the measured margins in
[`benchmarks/Results/Esp32S3N16R8.md`](../Modules/PlatformEsp32/benchmarks/Results/Esp32S3N16R8.md).
