# 01-CoreTick

**Feature:** bounded per-object tick scheduling (`FTickFunction`) driven by
caller-supplied real time — the engine's "no hidden clock" contract, made
visible with a real clock.

> Status: compiled for ESP32-S3 and native Pico; not yet verified on hardware.

## What it does

1. Logs the MicroWorld version: `microworld 0.3.0`.
2. Constructs the portable `FCoreTickExample` with a 500 ms `FTickFunction`
   interval and calls `Begin` from `FEsp32TimeSource::Now()`.
3. Polls every 10 ms, calling `Advance(Now())`. Each time the returned
   `FTickDecision` reports a due tick, logs
   `tick n=<count> delta=<ms>` — `delta` comes from the decision, not
   from any subtraction of clock samples in the example.
4. After 5 ticks, calls `EndPlay()`, logs `done ticks=5`, and returns.

## MicroWorld APIs used

- `FTickConfiguration::EnabledEvery`
- `FTickFunction` (`BeginPlay` / `Advance` / `EndPlay`)
- `FTickDecision` (`bShouldTick`, `Context.DeltaMilliseconds`)
- `TimePointMilliseconds`
- `FEsp32TimeSource::Now`
- `FVersion` (`MicroWorld::Version`, printed once at boot)

`FCoreTickExample` is platform-neutral. The ESP32 adapter prints the trace;
the Pico adapter uses Pico monotonic time inside one static FreeRTOS task.

## Hardware required

One ESP32-S3-DevKitC-1 and a USB cable. No extra components.

## Build

```sh
pio run -d examples/01-CoreTick
```

Build and test the shared behavior for the native Pico path:

```bat
cmake -S examples/01-CoreTick/tests -B examples/01-CoreTick/tests/build
cmake --build examples/01-CoreTick/tests/build --config Release
ctest --test-dir examples/01-CoreTick/tests/build -C Release --output-on-failure
Modules\Core\tests\consumer\pico-freertos\pico.bat build example
```

The last command produces
`microworld_pico_core_tick_example.uf2`. It is compile/link evidence only;
it does not upload or claim a Pico runtime trace.

## Flash and observe

Human-gated (see `../AGENTS.md`):

```sh
pio run -d examples/01-CoreTick -t upload --upload-port <COM-port>
pio device monitor -d examples/01-CoreTick
```

For Pico, BOOTSEL upload is separately human-gated:

```bat
Modules\Core\tests\consumer\pico-freertos\pico.bat upload example --drive E:
```

The script validates the `RPI-RP2` drive before copying. No Pico output is
documented until hardware verification is captured.

## Expected output (not yet hardware-verified)

Exactly seven lines; `n` runs 1–5; `delta` ≈ 500 (500–520 typical — real clock,
so approximate is expected):

```text
I (nnnn) ex01: microworld 0.3.0
I (nnnn) ex01: tick n=1 delta=500
I (nnnn) ex01: tick n=2 delta=500
I (nnnn) ex01: tick n=3 delta=500
I (nnnn) ex01: tick n=4 delta=500
I (nnnn) ex01: tick n=5 delta=500
I (nnnn) ex01: done ticks=5
```

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1):

```text
RAM:   6.2% (used 20220 bytes from 327680 bytes)
Flash: 4.6% (used 193709 bytes from 4194304 bytes)
```
