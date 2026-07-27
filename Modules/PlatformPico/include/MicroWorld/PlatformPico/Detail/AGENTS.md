# PlatformPico Internal State API

Inherits `../AGENTS.md`.

## Architecture

`E32LoraTransportState.h` is an unsupported implementation seam included by the
public driver for fixed-capacity value ownership and by host tests for
deterministic behavior. It contains no Pico SDK access.

## Concepts

- One transmit frame and one held receive frame make backpressure explicit.
- Tests observe accepted/rejected operations, byte progress, and transactional
  delivery without introducing a generic hardware abstraction.
