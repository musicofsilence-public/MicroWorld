# Examples

Inherits `../AGENTS.md`.

## Architecture

Each `examples/<NN-Name>/` is a self-contained consumer project that
demonstrates exactly one MicroWorld feature. All current examples build for an
ESP32-S3 through PlatformIO; `01-CoreTick` additionally shares its portable
behavior with the native Pico C++ SDK + FreeRTOS consumer. Dependencies point
inward: an example consumes `Modules/` packages, and MicroWorld never depends
on an example. Duplication *across* examples (the `platformio.ini` repetition,
the role-dispatch `Main.cpp`) is deliberate so each folder copies out
standalone; sharing definitions applies only *within* one example. WiFi, sleep, logging, and
time come from the shared `MicroWorld::Platform::Esp32` facades (`FEsp32WifiLink`,
`SleepMilliseconds`, `WriteEsp32LogRecord`, `FEsp32TimeSource`), not
per-example glue.

## Concepts

- One feature per example — the folder name states it, and the `src/` stays
  small enough to read in one sitting.
- The serial console is the observable: examples log through
  `WriteEsp32LogRecord`/`MW_LOG`, so every line carries the example's `exNN` category
  tag in the ESP-IDF shape `I (nnnn) exNN: …` (or `W`/`E` for warnings/errors),
  and examples without radio/network I/O print a deterministic trace.
- ESP32 example `src/` includes only `<MicroWorld/...>` headers plus
  `<cstdint>`/`<cstddef>`/`<utility>` — no ESP-IDF/lwIP/FreeRTOS/`printf`/
  `std::array`/`<cstdio>` in shared behavior (a grep gate enforces zero hits);
  only `Main.cpp` has `extern "C" app_main`. `01-CoreTick/src/PicoMain.cpp` is
  the one explicit Pico entry point and may include Pico SDK + FreeRTOS;
  `CoreTickExample.*` remains platform-neutral.
- Every MicroWorld composition object is declared `static` at file scope (or in
  a `static` function-local), never on the `app_main` stack — the default main
  task stack overflows otherwise (the hardware lesson recorded in
  `Modules/benchmarks/Platform/Esp32/Results/Esp32S3N16R8.md`, Phase 6.2).
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

For `01-CoreTick`, also verify the portable behavior and native Pico image:

```bat
cmake -S examples/01-CoreTick/tests -B examples/01-CoreTick/tests/build
cmake --build examples/01-CoreTick/tests/build --config Release
ctest --test-dir examples/01-CoreTick/tests/build -C Release --output-on-failure
Modules\Core\tests\consumer\pico-freertos\pico.bat build example
```

The final command is compile/link evidence only. Pico upload stays human-gated
through `Modules\\Core\\tests\\consumer\\pico-freertos\\pico.bat upload example`.

### Hardware checkpoint (human-gated — never self-serve)

Flashing and monitoring touch a physical board. Compile success is never a
runtime claim (see `Modules/MicroWorld/Platform/Esp32/AGENTS.md` and `../docs/Porting.md`),
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
