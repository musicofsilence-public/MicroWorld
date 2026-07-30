# MicroWorld Benchmarks

Inherits `../AGENTS.md`.

## Architecture

`benchmarks/` holds the host benchmark tree, kept outside `MicroWorld/` so the
portable library's recursive `srcDir` never compiles benchmark sources. Each
system subdir owns its benchmark source and a `Results/` record of measured
margins.

## Concepts

- Benchmarks measure bounded, allocation-free steady-state behavior under
  caller-supplied time.
- `Results/` holds the measured evidence (host and ESP32-S3) that closes the
  gap between compile success and a runtime/timing claim.
- Production code never depends on this tree.

## Verification

Build benchmarks with `-DMICROWORLD_BUILD_BENCHMARKS=ON`. Measured margins are
indexed by `../docs/ResourceBudgets.md`.
