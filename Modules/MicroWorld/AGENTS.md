# MicroWorld Systems

Inherits `../AGENTS.md`.

## Architecture

`Modules/MicroWorld/` is the engine's single portable package: six
contract-defined systems plus the non-portable Platform edges, each one
subdirectory. The folder tree states the architecture directly — a reader
opening this directory sees the six systems without consulting `.c4` metadata.
Dependencies point inward:

```text
Core <- Engine
Core <- Messaging
Core <- Transport
Core, Messaging, Transport <- Networking
Core, Engine <- Application
```

`Platform/Host`, `Platform/Esp32`, and `Platform/Pico` are the non-portable
edges; only they may reach OS/SDK headers. Headers and sources sit side by side
within each system directory; `include/` and `src/` are gone. Private
implementation headers live under each system's `Detail/` subfolder.

## Concepts

- One system per responsibility; the systems are the build targets and the
  architecture elements at once.
- Each system owns its identity (CMake target, `MicroWorld::*` alias, header
  namespace) — these are frozen.
- Per-system `AGENTS.md` files own durable boundaries; each system's headers and
  tests own its current behavior.
- The dependency-boundary checker (`tools/CheckDependencyBoundaries.py`) holds
  the inward edges as machine-checked facts; Engine and Transport never see each
  other.

## Verification

Build and test the whole engine from the repo root
(`cmake -S . -B build && cmake --build build --config Release &&
ctest --test-dir build -C Release`). The superbuild is now the only path —
standalone per-system configuration is gone. Run the dependency-boundary checker
over the six portable systems and the profile-map checker on built consumer maps.
