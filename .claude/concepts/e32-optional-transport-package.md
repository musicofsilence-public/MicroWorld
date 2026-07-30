# Optional RadioE32 Package

## Problem

E32 is an external UART radio, not an intrinsic ESP32 or RP2040 capability.
Keeping its protocol policy inside both platform packages duplicates validation,
framing state, receive delivery, and bounded pumping while making device
ownership look platform-specific.

The repository still needs ESP-IDF and Pico SDK access confined to their
platform edges, and existing public include paths and target identities must
remain stable.

## Proposed Approach

Treat E32 as an optional compile-time package named `RadioE32` rather than a
runtime plugin. Add it at `Modules/RadioE32`; it depends on `MicroWorld::Net`
and owns the SDK-free transport policy: validation, frame queueing, retained
receive delivery, and bounded codec state.

Add a narrow `IUartByteStream` contract to Core. It exposes only transactional,
non-blocking single-byte read/write results; platform-specific baud, pin,
open/close, buffering, DMA, and interrupt policy stay outside the contract.
`RadioE32` consumes this interface, while PlatformEsp32 and PlatformPico supply
the concrete UART objects.

Use the standard module layout:

```text
Modules/RadioE32/
├── include/MicroWorld/RadioE32/
├── src/
├── tests/
├── CMakeLists.txt
├── library.json
├── README.md
└── AGENTS.md
```

Keep the existing `FEsp32E32LoraDriver` and `FPicoE32LoraDriver` APIs as thin
compatibility facades. They continue to own platform UART selection, pin
validation, open/close behavior, and SDK calls, but delegate E32 protocol state
to the portable package. This preserves the rule that only platform modules may
include SDK headers.

Applications opt into `RadioE32` at build time. There is no dynamic
loading, registration system, heap ownership, or runtime discovery.

## Open Questions

None. The concept is approved for detailed planning.

## Decisions Log

- 2026-07-27: Prefer an optional compile-time package over a runtime plugin -
  MCU builds need static, bounded composition.
- 2026-07-27: Keep SDK-specific UART ownership in PlatformEsp32 and
  PlatformPico - hardware SDK dependencies remain at platform edges.
- 2026-07-27: Preserve existing public driver names and include paths - package
  extraction must not break current consumers.
- 2026-07-27: Name the package `RadioE32` - it describes the current
  transparent-mode radio scope without implying full device configuration.
- 2026-07-27: Locate the package at `Modules/RadioE32` - existing package,
  checker, and build conventions are sufficient without a plugin loader.
- 2026-07-27: Put `IUartByteStream` in Core - multiple current platform
  consumers need one minimal byte-I/O seam without SDK or device policy.
- 2026-07-27: Keep `Net/E32Lora.h` as the wire-contract home - moving it would
  invert the `Net <- RadioE32` dependency or create a forwarding cycle.
- 2026-07-27: Preserve the existing ESP32 and Pico E32 driver APIs as
  compatibility facades - current examples and external consumers keep their
  public include paths while protocol work moves inward.
