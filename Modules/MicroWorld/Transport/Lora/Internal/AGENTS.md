# RadioE32 Transport Details

Inherits `../AGENTS.md`.

## Architecture

This directory will hold fixed-capacity E32 framing and transport state used by
the public device. It is an internal package boundary, not a supported consumer
or platform-facade API.

## Concepts and boundaries

- Internal state preserves fixed transmit and retained receive frames across
  bounded public-device calls.
- It may depend only on RadioE32's public contract, Core, Transport, and C++17.
- It must remain portable: no UART SDK calls, pin configuration, platform
  lifecycle, heap allocation, clocks, threads, exceptions, or RTTI.

## Verification

Verify detail behavior through `FE32LoraDevice` public tests. Keep direct
detail tests only for narrowly scoped state-machine coverage when the public
contract cannot observe the condition.
