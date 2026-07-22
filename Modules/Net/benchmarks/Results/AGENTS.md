# Net Evidence Records

Inherits `../AGENTS.md`.

## Architecture

`Host.md` records host-side Net evidence: the CMake/CTest result, the strict
Core+Memory+Net consumer compile, and the strict single-translation-unit
compiles under GCC 16 and Clang 19. `Esp32S3N16R8.md` records the ESP32-S3
compile-only PlatformIO build of the Core+Memory+Net consumer.

## Concepts and boundaries

- Each record states its `Status:` line, environment, exact commands, and
  measured counts so the result is reproducible.
- Host behavior evidence proves the bounded byte-I/O contract on the host; it
  does not establish target runtime margins.
- ESP32-S3 evidence is compile-only: no upload, execution, radio transmit, or
  hardware measurement is recorded here.
