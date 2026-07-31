# Porting MicroWorld

MicroWorld targets a new platform through **four adapter interfaces**. The runtime
itself is platform-free: it never reads a clock, never opens a socket, never
paces a loop, and never logs to hardware. A port fills those four gaps and
otherwise reuses the shipped portable packages unchanged. Each interface below names
the shipped adapter that implements it as a worked reference.

## The four adapter interfaces

### 1. Time source

The runtime never reads a clock. Every lifecycle, tick, timer, GC, and transport
deadline takes a caller-supplied
[`TimePointMilliseconds`](../Modules/MicroWorld/Core/Time.h) (`std::uint64_t`
monotonic milliseconds). An adapter reads the real clock and feeds that value
into `TEngine::Tick(Now)` (or the lower-level `Advance(Now)` calls).

- ESP32 reference:
  [`FEsp32TimeSource`](../Modules/MicroWorld/Platform/Esp32/Esp32TimeSource.h)
  wraps `esp_timer_get_time()` and returns `microseconds / 1000`.
- Host reference:
  [`FHostTimeSource`](../Modules/MicroWorld/Platform/Host/HostTimeSource.h)
  uses `std::chrono::steady_clock` from a process-local baseline.

### 2. Device

Implement
[`IDevice`](../Modules/MicroWorld/Transport/Device.h) with
two non-blocking, transactional operations — `TrySend(const FDeviceAddress& To,
TSpan<const std::uint8_t>)` and `TryReceive(FDeviceAddress& OutFrom, TSpan<
std::uint8_t>, FReceiveResult&)`. On any non-`Success` result the
destination and `BytesReceived` must be unchanged. `FDeviceAddress` is opaque; the
adapter owns its concrete encoding and provides helpers to build/inspect it.

- Host UDP reference:
  [`FHostWifiDevice`](../Modules/MicroWorld/Platform/Host/HostWifiDevice.h)
  over a `SOCK_DGRAM` socket on `127.0.0.1`, with
  [`MakeUdpAddress`/`IsUdpAddress`/`UdpAddressPort`](../Modules/MicroWorld/Platform/Host/UdpAddress.h)
  for the 6-byte IPv4+port encoding.
- ESP32 UDP reference:
  [`FEsp32WifiDevice`](../Modules/MicroWorld/Platform/Esp32/Esp32WifiDevice.h)
  over lwIP; same three UDP address helpers duplicated verbatim.
- ESP32 E32 LoRa reference:
  [`FEsp32LoraDevice`](../Modules/MicroWorld/Platform/Esp32/Esp32LoraDevice.h)
  is an ESP32 UART-lifetime compatibility facade over optional
  [`RadioE32`](../Modules/MicroWorld/Transport/Lora). RadioE32 owns portable E32 framing and
  bounded progress over Core's narrow `IUartByteStream` interface; the facade retains a 1-byte broadcast
  [`LoraAddress`](../Modules/MicroWorld/Platform/Esp32/LoraAddress.h).
- ESP32 wired UART reference:
  [`FEsp32UartDevice`](../Modules/MicroWorld/Platform/Esp32/Esp32UartDevice.h)
  over a plain point-to-point UART, using the
  same `MicroWorld/Transport/FrameCodec.h` framing over a 1-byte point-to-point
  [`UartAddress`](../Modules/MicroWorld/Platform/Esp32/UartAddress.h).
- ESP32 wired I2C reference (master/slave pair):
  [`FEsp32I2cMasterDevice` / `FEsp32I2cSlaveDevice`](../Modules/MicroWorld/Platform/Esp32/Esp32I2cDevice.h)
  over one I2C bus — the master clocks whole-frame transactions, the slave receives
  through an `on_receive` ISR inbox — with a 1-byte
  [`I2cAddress`](../Modules/MicroWorld/Platform/Esp32/I2cAddress.h).
- ESP32 wired SPI reference (master/slave pair):
  [`FEsp32SpiMasterDevice` / `FEsp32SpiSlaveDevice`](../Modules/MicroWorld/Platform/Esp32/Esp32SpiDevice.h)
  over one full-duplex SPI bus — the master clocks fixed-size transactions, the
  slave keeps one queued — with a 1-byte
  [`SpiAddress`](../Modules/MicroWorld/Platform/Esp32/SpiAddress.h).

### 3. Output device

Install one
[`FOutputDeviceFunction`](../Modules/MicroWorld/Core/Log.h) via `SetOutputDevice` at startup. The
default output device is null (logging disabled). The facade is single-threaded; install
the output device before the first `MW_LOG` / `MW_LOG_MSG` call.

- ESP32 reference:
  [`WriteEsp32LogRecord`](../Modules/MicroWorld/Platform/Esp32/Esp32OutputDevice.h)
  maps each `ELogLevel` to the matching `ESP_LOG*` macro.

### 4. Pacing

`FApplication::Run` (in Application) drives one `FApplication` through its
begin/advance/end lifecycle, but it paces the frame loop through an injected
function pointer so the platform's idle task and watchdog still run. Supply a
clock, a free `void(DurationMilliseconds) noexcept` function (typically the
platform's existing sleep), and a cadence; `Run` calls it once per successful
frame.

- ESP32 reference:
  [`SleepMilliseconds`](../Modules/MicroWorld/Platform/Esp32/Esp32Sleep.h)
  wraps `vTaskDelay` and binds directly to `FSleepFunction` with no wrapper.

## Where the adapter code lives

Adapter code goes in a **non-portable platform package** (e.g.
`Modules/MicroWorld/Platform/Host`, `Modules/MicroWorld/Platform/Esp32`). Such a
package:

- may include OS and vendor headers (WinSock, lwIP, ESP-IDF, `<driver/uart.h>`,
  `<chrono>`, …) and confine them to private `src/*PlatformImplementation.h` headers;
- is **excluded from `CheckDependencyBoundaries.py`** — it has no module key in
  that tool's portable table;
- **depends inward** on any portable package (Core, Engine, Messaging, Transport,
  Application, Networking) it needs; the reverse dependency is forbidden.

The portable packages themselves stay free of OS/vendor headers and remain
under the dependency checker.

## What a port is not

Compile success is never a runtime, timing, heap, stack, or radio claim. A
newly ported adapter that opens sockets, drives a radio, or holds large fixed
storage must be smoke-run on the real target before any runtime-readiness
claim — the first ESP32-S3 measurement run found two defects that were invisible
to the compile-only proof (lwIP stack uninitialized before socket use; large
composition overflowing the main task stack). The measured margins are in
[`benchmarks/Results/Esp32S3N16R8.md`](../Modules/benchmarks/Platform/Esp32/Results/Esp32S3N16R8.md).
