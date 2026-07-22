# Memory Benchmark Evidence

Inherits `../AGENTS.md`.

## Architecture

This directory records reproducible Memory benchmark and profile-consumer
evidence. Production code never reads these files.

## Evidence concepts

- Host runtime measurements and target compile-only measurements remain
  separate.
- Whole-image target flash/RAM values include the platform runtime and are not
  attributed to Memory in isolation.
- Blocked hardware work is labeled explicitly and cannot become a runtime
  claim.
- An unresolved absolute budget does not invalidate measured facts or authorize
  inventing a target threshold.

## Verification

Cross-check every value against named command output and preserve failed or
blocked attempts that materially affect the support claim.
