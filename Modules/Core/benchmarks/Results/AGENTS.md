# Benchmark Evidence

Inherits `../AGENTS.md`.

## Architecture

This directory is the immutable evidence boundary between measurement harnesses
and optimization decisions. Host and ESP32-S3 records describe different
environments and must not be substituted for one another. Runtime code never
reads these files.

## Evidence concepts

- A result identifies toolchain, flags, target, workload, trial shape, source
  state, and whether it came from compile output or observed execution.
- Assumptions, planned measurements, and blocked hardware steps are labeled
  separately from results.
- Generated maps and raw output remain build artifacts; the Markdown record
  captures the reproducible facts needed for review.
- An optimization needs baseline and candidate evidence with unchanged
  behavior checks.

## Verification

Cross-check every claim against the named build output or captured run. Never
promote compile-only flash/RAM evidence into cycle, heap, stack, or physical
target claims.
