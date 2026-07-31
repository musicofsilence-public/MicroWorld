# RadioE32 Transport Details

Inherits `../AGENTS.md`.

## Architecture

This directory will hold fixed-capacity E32 framing and transport state used by
the public driver. It is an internal package boundary, not a supported consumer
or platform-facade API.

## Concepts and boundaries

- Detail state preserves fixed transmit and retained receive frames across
  bounded public-driver calls.
- It may depend only on RadioE32's public contract, Core, Transport, and C++17.
- It must remain portable: no UART SDK calls, pin configuration, platform
  lifecycle, heap allocation, clocks, threads, exceptions, or RTTI.

## Verification

Verify detail behavior through `FRadioE32Driver` public tests. Keep direct
detail tests only for narrowly scoped state-machine coverage when the public
contract cannot observe the condition.
