# 01-CoreTick

**Feature:** bounded per-object tick scheduling (`FTickFunction`) driven by
caller-supplied real time — the engine's "no hidden clock" contract, made
visible with a real clock.

> Status: compiled for ESP32-S3; not yet verified on hardware.

## What it does

1. Prints the MicroWorld version: `[ex01] microworld 0.3.0`.
2. Constructs a static `FTickFunction` with a 500 ms interval and calls
   `BeginPlay` from `FEsp32TimeSource::Now()`.
3. Polls every 10 ms, calling `Advance(Now())`. Each time the returned
   `FTickDecision` reports a due tick, prints
   `[ex01] tick n=<count> delta=<ms>` — `delta` comes from the decision, not
   from any subtraction of clock samples in the example.
4. After 5 ticks, calls `EndPlay()`, prints `[ex01] done ticks=5`, and returns.

## MicroWorld APIs used

- `FTickConfiguration::EnabledEvery`
- `FTickFunction` (`BeginPlay` / `Advance` / `EndPlay`)
- `FTickDecision` (`bShouldTick`, `Context.DeltaMilliseconds`)
- `TimePointMilliseconds`
- `FEsp32TimeSource::Now`
- `FVersion` (`MicroWorld::Version`, printed once at boot)

## Hardware required

One ESP32-S3-DevKitC-1 and a USB cable. No extra components.

## Build

```sh
pio run -d examples/01-CoreTick
```

## Flash and observe

Human-gated (see `docs/EXAMPLES_ROADMAP.md` §1.2):

```sh
pio run -d examples/01-CoreTick -t upload --upload-port <COM-port>
pio device monitor -d examples/01-CoreTick
```

## Expected output

Exactly seven lines; `n` runs 1–5; `delta` ≈ 500 (500–520 typical — real clock,
so approximate is expected):

```text
[ex01] microworld 0.3.0
[ex01] tick n=1 delta=500
[ex01] tick n=2 delta=500
[ex01] tick n=3 delta=500
[ex01] tick n=4 delta=500
[ex01] tick n=5 delta=500
[ex01] done ticks=5
```

## Verified output

Not yet verified on hardware. This example has been compiled for ESP32-S3 only;
the captured serial trace goes here once the human-gated checkpoint runs.

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1):

```text
RAM:   6.2% (used 20220 bytes from 327680 bytes)
Flash: 4.6% (used 193281 bytes from 4194304 bytes)
```
