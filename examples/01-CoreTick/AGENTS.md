# 01-CoreTick

Inherits `../AGENTS.md`.

## Architecture

One composition root (`app_main` in `src/Main.cpp`) owns one static
`FTickFunction` and drives it from the static `FEsp32TimeSource`. There is no
world, no actor, and no allocation — just the bounded tick scheduler and the
board clock feeding it.

## Concepts

- Makes the engine's **caller-supplied time** contract observable: the example
  polls every 10 ms, but the tick function decides when a 500 ms tick is due and
  reports its own delta — the loop never subtracts clock samples.
- The tick function is `static`, never an `app_main` stack local (§2.2).
- The trace is a fixed seven lines (one version line, five ticks, one done),
  deterministic except the `delta` values, which are real-clock approximate.

## Verification

Build Verify (`docs/EXAMPLES_ROADMAP.md` §1.1): `pio run -d examples/01-CoreTick`
then the root `cmake --build` / `ctest`. Hardware checkpoint (§1.2, human-gated):

```sh
pio run -d examples/01-CoreTick -t upload --upload-port <COM-port>
pio device monitor -d examples/01-CoreTick
```

Expect the seven-line trace with `n` running 1–5 and `delta` ≈ 500.
