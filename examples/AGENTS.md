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
`WriteEsp32LogRecord`, `FEsp32TimeSource`), not per-example glue.

## Concepts

- One feature per example — the folder name states it, and the `src/` stays
  small enough to read in one sitting.
- The serial console is the observable: examples log through
  `WriteEsp32LogRecord`/`MW_LOG`, so every line carries the example's `exNN` category
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

### Build Verify (run for every example change)

Run from the repository root, in this order:

```sh
clang-format --style=file:clang-format -i <every .h/.cpp file you touched>
pio run -d examples/<NN-Name>
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected:

- `pio run` ends with `[SUCCESS]` (exit 0). Record the `RAM:`/`Flash:` usage
  lines it prints — they belong in the example's README.
- The root `ctest` still passes with the same test count as before the change.
  `tools/CheckFormatting.py` (wired into ctest as `microworld_format_check`)
  checks **every git-tracked `.h`/`.cpp` in the repository**, so the moment
  example sources are tracked the repo-wide format gate covers them. One
  unformatted example breaks the whole build's gate.
- If `build/` is missing, create it first with `cmake -S . -B build`.

The first `pio run` of a fresh checkout downloads the Espressif toolchain and
takes several minutes; later runs are incremental.

### Hardware checkpoint (human-gated — never self-serve)

Flashing and monitoring touch a physical board. Compile success is never a
runtime claim (see `Modules/PlatformEsp32/AGENTS.md` and `../docs/Porting.md`),
and a worker must never run `pio run -t upload` or `pio device monitor` without
explicit human authorization in the current session.

1. Announce that the example is ready for hardware verification and print the
   two commands the human (or the authorized worker) runs:

   ```sh
   pio run -d examples/<NN-Name> -t upload --upload-port <COM-port>
   pio device monitor -d examples/<NN-Name>
   ```

2. Compare the captured serial output against the example's documented trace
   shape.
3. Paste the real captured trace into the example README's "Verified output"
   section and add the evidence line.

Until step 3 happens, the example's README must carry the sentence:
*"Status: compiled for ESP32-S3; not yet verified on hardware."*
