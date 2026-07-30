# esp32-common — shared board profile

Inherits `../AGENTS.md`.

## Architecture

`partitions.csv` and `sdkconfig.defaults` are the single board profile every
example references through its `platformio.ini` (`board_build.partitions` and
`-DSDKCONFIG_DEFAULTS`). They are not example-specific: an example points at
this folder, it never copies or forks these files.

## Concepts

- **Provenance.** These two files are the exact profile that produced every
  verified ESP32-S3 compile and the Phase 6.2 hardware measurements recorded in
  `Modules/benchmarks/Platform/Esp32/Results/Esp32S3N16R8.md`. They are proven,
  not tunable — do not "improve" them.
- `partitions.csv` describes the 16 MB flash layout (NVS, dual 4 MiB OTA slots,
  a wear-levelled FATFS data area, and a coredump region).
- `sdkconfig.defaults` matches the ESP32-S3-WROOM-1-N16R8 module: 16 MB QIO
  flash, 8 MB Octal PSRAM, the custom partition table, coredump-to-flash, and a
  4096-byte wear-levelling sector.

## Verification

Any change here invalidates the recorded ESP32-S3 evidence. Treat the files as
frozen; if a board genuinely needs a different profile, that is a new profile
folder and a new measurement, not an edit to this one.
