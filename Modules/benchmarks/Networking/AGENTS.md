# Networking Benchmarks

Inherits `../AGENTS.md`.

## Architecture

This directory owns host-only resource evidence for the Networking system. It
contains no production source, and production code never depends on it.

## Concepts

`Results/` records a reproducible command outcome and separately identifies
source-level bounds. A successful host build or test is not target compile or
target runtime evidence.

## Verification

Build `microworld_networking_tests` from the root superbuild and run the named
CTest test. Record the exact commands, source SHA, and worktree qualification.
