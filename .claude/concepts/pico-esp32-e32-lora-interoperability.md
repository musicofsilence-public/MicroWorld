# Pico H + ESP32 E32 LoRa Interoperability

## Problem

The wired Pico H and ESP32-S3 each have an EBYTE E32-433T20D radio, but the
current Pico firmware only runs a CoreTick proof. It cannot yet encode or
decode the UART frames used by ESP32 example 17, so the radio link cannot be
tested end to end.

## Proposed Approach

Add one consumer-local Pico LoRa interoperability image. It uses Pico UART1
on GP4/GP5 at the E32 factory default 9600 8N1, encodes and decodes the same
portable `FrameCodec` frames as the ESP32 E32 driver, and runs the same bounded
node-1/node-2 counter volley against ESP32 example 17 node B. Add `lora` to
the existing BAT/Python build and upload selectors.

This is deliberately a hardware proof, not a `PlatformPico` module: no generic
driver, AUX policy, configuration-mode support, USB logging, or other radio
transport is added until the physical link has evidence.

## Open Questions

None. The initial hardware pairing is Pico node 1 with ESP32 example 17 node B
(node 2), using two E32-433T20D modules in transparent mode.

## Decisions Log

- 2026-07-26: Reuse `FrameCodec` directly in a Pico consumer-only image - it
  proves wire compatibility without creating a production platform abstraction.
- 2026-07-26: Use UART1 GP4/GP5 - it matches the installed wiring and reserves
  Pico UART0 GP0/GP1 for optional debug output.
- 2026-07-26: Pico is node 1 and ESP32 is node 2 - Pico seeds the existing
  ESP32 example 17 volley without changing the verified ESP32 source.
