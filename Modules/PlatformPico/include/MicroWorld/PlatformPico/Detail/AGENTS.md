# PlatformPico Internal State API

Inherits `../AGENTS.md`.

## Architecture

`E32LoraTransportState.h` and `PicoE32LoraPlatform.h` are unsupported
implementation seams. The former owns fixed-capacity framing state; the latter
is the narrow SDK-free UART binding used by the public driver and host policy
tests. Neither contains Pico SDK access.

## Concepts

- One transmit frame and one held receive frame make backpressure explicit.
- Tests observe accepted/rejected operations, byte progress, and transactional
  delivery through a per-instance fake binding without creating a generic
  hardware abstraction.
