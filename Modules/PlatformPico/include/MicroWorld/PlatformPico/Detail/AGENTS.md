# PlatformPico Internal State API

Inherits `../AGENTS.md`.

## Architecture

`E32LoraTransportState.h` and `PicoE32LoraPlatform.h` are legacy forwarding
compatibility paths to RadioE32-owned framing and the generic Pico UART binding.
`PicoUartPlatform.h` and `PicoUartByteStream.h` are the unsupported generic
platform seams that separate SDK-free UART policy from the Pico SDK backend.
None contains Pico SDK access.

## Concepts

- Fixed framing and the legacy E32 platform spelling remain compatibility paths;
  RadioE32 owns framing while the generic Pico UART seam owns byte access.
- The byte stream owns validated UART open/close lifetime and maps one-byte
  availability; tests use a per-instance fake platform binding without creating
  a generic hardware abstraction.
