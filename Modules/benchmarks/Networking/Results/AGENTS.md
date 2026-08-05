# Networking Benchmark Evidence

Inherits `../AGENTS.md` and `../../AGENTS.md`.

## Architecture

This directory records immutable host evidence for a named Networking source
state. It does not own target claims, release status, or production behavior.

## Concepts

Each record distinguishes command-observed build and test results from static
source bounds. Dirty source states must remain explicitly qualified.

## Verification

Re-run every recorded command against the named source state. Do not infer
target memory, timing, stack, radio, or runtime behavior from host evidence.
