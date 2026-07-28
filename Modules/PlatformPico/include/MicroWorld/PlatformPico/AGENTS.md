# PlatformPico Public API

Inherits `../AGENTS.md`.

## Architecture

`PicoE32LoraDriver.h` is the supported RP2040 E32 compatibility facade. It owns UART
configuration while RadioE32 owns portable `INetDriver` framing; every Pico SDK identifier stays
private to the source directory; its explicit internal-binding constructor is
for host policy tests, not a general hardware layer.

## Concepts

- `Initialize` is explicit so static construction performs no hardware access.
- `TrySend(Success)` means fixed-slot acceptance; direct callers use
  `AdvanceTransmit` for bounded physical UART progress, while `TNetHost` already does.
- Transparent-mode E32 broadcasts frames; destination addresses are validated
  driver metadata, not on-air routing.
