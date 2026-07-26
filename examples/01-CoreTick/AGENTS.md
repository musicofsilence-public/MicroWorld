# 01-CoreTick

Inherits `../AGENTS.md`.

## Architecture

`src/CoreTickExample.*` owns one platform-neutral `FTickFunction` schedule.
`src/Main.cpp` adapts it to ESP32-S3 logging and `FEsp32TimeSource`; the native
Pico consumer compiles `src/PicoMain.cpp` as a static FreeRTOS task driven by
Pico monotonic time. There is no world, no actor, and no allocation — just the
bounded tick scheduler and a platform clock feeding it.

## Concepts

- Makes the engine's **caller-supplied time** contract observable: the example
  polls every 10 ms, but the tick function decides when a 500 ms tick is due and
  reports its own delta — the loop never subtracts clock samples.
- The ESP32 adapter owns the tick function in static function storage; the
  Pico adapter owns it in static firmware storage, never on a task stack.
- The trace is a fixed seven lines (one version line, five ticks, one done),
  deterministic except the `delta` values, which are real-clock approximate.

## Verification

Build Verify (`../AGENTS.md`): `pio run -d examples/01-CoreTick`
then the root `cmake --build` / `ctest`. Hardware checkpoint (`../AGENTS.md`, human-gated):

```sh
pio run -d examples/01-CoreTick -t upload --upload-port <COM-port>
pio device monitor -d examples/01-CoreTick
```

Expect the seven-line trace with `n` running 1–5 and `delta` ≈ 500.

The shared behavior is also covered by a host unit test and native Pico
compile/link image:

```bat
cmake -S examples/01-CoreTick/tests -B examples/01-CoreTick/tests/build
cmake --build examples/01-CoreTick/tests/build --config Release
ctest --test-dir examples/01-CoreTick/tests/build -C Release --output-on-failure
Modules\Core\tests\consumer\pico-freertos\pico.bat build example
```

The Pico command does not upload or establish a runtime trace; upload remains
human-gated through `pico.bat upload example`.
