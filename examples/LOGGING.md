# Seeing and writing logs on the ESP32-S3 examples

Every example prints through MicroWorld's logging facade (`MW_LOG`), which the
examples route to the ESP32 output device. This page explains how to **see** those
logs on the two-board rig and how to **write** your own.

The convenience wrapper for all of this is [`tools/mw.bat`](tools/mw.bat):

```bat
mw build 25                         :: compile all role envs of example 25
mw flash 25 esp32-s3-server COM5    :: build & flash the server role to COM5
mw flash 25 esp32-s3-client COM7    :: build & flash the client role to COM7
mw log   COM5                       :: watch MW_LOG on COM5 (Ctrl-C to stop)
```

## Where the logs go

`MW_LOG(...)` → `Detail::DispatchLogFormatted` → the installed `FOutputDeviceFunction`. The
examples install `WriteEsp32LogRecord`, which forwards to ESP-IDF's logger, so each line
comes out in the familiar `I (nnnn) tag: message` shape (`I`/`W`/`E` = level,
`nnnn` = ms since boot).

That output leaves the chip on the **primary console**, which the examples set to
the **native USB-Serial-JTAG port** in
[`esp32-common/sdkconfig.defaults`](esp32-common/sdkconfig.defaults)
(`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`). That is deliberate: on the
ESP32-S3-DevKitC-1 the *default* UART0 console only reaches the board's separate
CP210x "UART" connector, whereas the native "USB" connector is the one you flash
through. Routing the console to USB-Serial-JTAG means **the port you flash is the
port you read** — one cable per board. Data buses (UART1/I2C/SPI in examples
18–24) are untouched by this.

## Seeing logs

Use the reader — **not** `pio device monitor` (see the pitfall below):

```bat
mw log COM5            :: read until you press Ctrl-C
mw log COM5 30         :: read for 30 seconds, then stop
mw log COM5 30 run.txt :: ... and also save the lines to run.txt
```

`mw log` calls [`tools/mwlog.py`](tools/mwlog.py) with PlatformIO's bundled Python.
You can call it directly too:

```bat
%USERPROFILE%\.platformio\penv\Scripts\python.exe examples\tools\mwlog.py COM5
```

### Two-board recipe (example 25)

```bat
mw flash 25 esp32-s3-server COM5     :: server hosts the SoftAP
mw flash 25 esp32-s3-client COM7     :: client joins and starts sending
mw log   COM5                        :: server: rx best-effort (gaps) vs rx guaranteed (complete)
:: in a second terminal:
mw log   COM7                        :: client: tx n=... and guaranteed resent=...
```

Flash the **server first** so its SoftAP is up before the client tries to join.

## Native-USB pitfalls (why `mw log`, not `pio device monitor`)

The native USB-Serial-JTAG port has two quirks that `mw log` is built around:

1. **A reset re-enumerates the port.** Every flash (and every reset) makes the USB
   device disconnect and reconnect. A terminal that opened the port once is now
   holding a dead handle and shows nothing. `mwlog.py` reopens on idle/error, so it
   reconnects automatically — just leave it running across a flash.
2. **Auto-reset monitors can wedge the board.** `pio device monitor` (and other
   miniterm-based tools) toggle DTR/RTS on open. On the native port DTR/RTS map to
   GPIO0/EN, so that toggle can drop the board into the ROM **download** loader —
   it stops running your app and goes silent until the next flash. `mwlog.py` holds
   DTR steady and never pulses a reset, so it cannot do this.

Two more things that look like "no logs" but aren't:

- **A board only prints when it has something to say.** In example 25 the server
  logs on receive and the client stops after `n=30` (by design). If you attach late
  you may catch only silence — reflash the client to start a fresh 1..30 run.
- **Let a just-reset board settle** (a few seconds) before expecting steady output;
  `mw log` will show it as soon as it reconnects.

## Writing your own logs

Include the facade and call `MW_LOG` with a level, a short category tag, and a
printf-style format:

```cpp
#include <MicroWorld/Log.h>

MW_LOG(Log,     "ex25", "tx n=%u", static_cast<unsigned>(Value));
MW_LOG(Warning, "ex25", "peer %u slow", Index);
MW_LOG(Error,   "ex25", "socket open failed");
MW_LOG_MSG(Log, "ex25", "ready");   // literal string, no printf parsing
```

- **Levels** (`ELogLevel`): `Error` < `Warning` < `Log` < `Verbose`.
- **Compile-time floor.** Calls below `MW_LOG_MIN_LEVEL` compile to nothing. The
  default floor is `Log`, so `Verbose` is stripped unless you raise it, e.g. add
  `-DMW_LOG_MIN_LEVEL=MW_LOG_LEVEL_Verbose` to an env's `build_flags`.
- **Bounded.** Each formatted line is capped at `MW_LOG_MESSAGE_CAPACITY` bytes
  (default 128) — no heap, no unbounded growth.
- **Category** is a short static string used as the log tag; the examples use
  `"exNN"`.

The output device is one injected function pointer (`FOutputDeviceFunction`): the examples
wire `WriteEsp32LogRecord` on device, and host tests install their own to assert on
log output. Your code only ever calls `MW_LOG` — it never talks to a console
directly.
