# PlatformPico Public API

Inherits `../AGENTS.md`.

## Architecture

`PicoE32LoraDriver.h` is the supported RP2040 E32 transport API. It owns UART
configuration and `INetDriver` behavior while keeping every Pico SDK identifier
private to the source directory.

## Concepts

- `Initialize` is explicit so static construction performs no hardware access.
- `TrySend(Success)` means fixed-slot acceptance; `AdvanceTransmit` completes
  physical UART progress.
- Transparent-mode E32 broadcasts frames; destination addresses are validated
  driver metadata, not on-air routing.
