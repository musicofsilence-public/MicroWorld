# Examples

Inherits `../AGENTS.md`.

## Architecture

Each `examples/<NN-Name>/` is a self-contained PlatformIO consumer project that
demonstrates exactly one MicroWorld feature on a real ESP32-S3. Dependencies
point inward: an example consumes the `Modules/` packages through `symlink://`
exactly like the verified consumer project, and MicroWorld never depends on an
example. Duplication *across* examples (the `platformio.ini` boilerplate, the
role-dispatch `Main.cpp`) is deliberate so each folder copies out standalone;
DRY applies only *within* one example. WiFi, sleep, logging, and time come
from the shared `PlatformEsp32` facades (`FEsp32WifiLink`, `SleepMilliseconds`,
`Esp32LogSink`, `FEsp32TimeSource`), not per-example glue.

## Concepts

- One feature per example — the folder name states it, and the `src/` stays
  small enough to read in one sitting.
- The serial console is the observable: examples log through
  `Esp32LogSink`/`MW_LOG`, so every line carries the example's `exNN` category
  tag in the ESP-IDF shape `I (nnnn) exNN: …` (or `W`/`E` for warnings/errors),
  and examples without radio/network I/O print a deterministic trace.
- After Phase 1, an example's `src/` includes only `<MicroWorld/...>` headers
  plus `<cstdint>`/`<cstddef>`/`<utility>` — no ESP-IDF/lwIP/FreeRTOS/`printf`/
  `std::array`/`<cstdio>` in example `src/` (a grep gate enforces zero hits);
  only `Main.cpp` has `extern "C" app_main`.
- Every MicroWorld composition object is declared `static` at file scope (or in
  a `static` function-local), never on the `app_main` stack — the default main
  task stack overflows otherwise (the hardware lesson recorded in
  `Modules/PlatformEsp32/benchmarks/Results/Esp32S3N16R8.md`, Phase 6.2).
- Time is caller-supplied from `FEsp32TimeSource`; no example logic reads a
  hidden clock.

## Verification

Build every example with the Build Verify in `docs/EXAMPLES_ROADMAP.md` §1.1
(`pio run` is the compile gate; the repo-wide `ctest` format gate covers example
sources once they are tracked). Compile success is never a runtime claim: a
board is flashed and monitored only through the human-gated hardware checkpoint
of §1.2, and each example README carries the "not yet verified on hardware"
sentence until its captured trace is pasted in.
