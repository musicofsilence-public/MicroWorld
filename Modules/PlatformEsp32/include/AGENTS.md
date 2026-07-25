# PlatformEsp32 Public Include Boundary

Inherits `../AGENTS.md`.

## Architecture

`include/` is the only supported compile-time surface of the PlatformEsp32
package. Public headers declare `FEsp32TimeSource`, `FEsp32UdpDriver`,
`FEsp32E32LoraDriver`, and `Esp32OutputDevice` against the portable `INetDriver`,
`TimePointMilliseconds`, and `FOutputDevice` contracts; they may depend inward on
Net, Object, Memory, and Core public headers but must declare no ESP-IDF,
lwIP, or vendor type in a way that leaks into a caller's include of this
directory.

## Concepts

Public declarations name the adapter's parameters (UART port, GPIO numbers,
socket port) as plain integers or portable types, so a consumer can name and
construct an adapter without pulling ESP-IDF or lwIP headers itself.

## Verification

Each public header must compile independently under the ESP-IDF C++17
toolchain with `-fno-exceptions -fno-rtti`. Document exported functions with
the real-hardware behavior they wrap and any behavior left unverified at
compile time.
