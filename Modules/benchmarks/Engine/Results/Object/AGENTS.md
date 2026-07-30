# Object Benchmark Evidence

Inherits `../AGENTS.md` and `../../AGENTS.md`.

## Architecture

This directory records immutable observations for an exact source,
environment, toolchain, configuration, and workload. It does not own live
status, target acceptance, or release promotion.

## Concepts

Source anchors and evidence boundaries keep observations reproducible without
turning them into current-state claims.

## Verification

Verify every value against the named source and retained build/test output.
Keep host runtime evidence distinct from target compile evidence, and never
infer target timing, heap, stack, or hardware behavior from compilation.
