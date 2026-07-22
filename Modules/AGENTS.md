# MicroWorld Modules

Inherits `../AGENTS.md`.

## Architecture

`Modules/` is the container for the engine's per-layer packages. Each
subdirectory is one CMake/PlatformIO package with its own `CMakeLists.txt`,
`library.json`, public `include/MicroWorld/...` headers, and scoped
`AGENTS.md`. Dependencies point inward:

```text
Core <- Memory <- Object <- Engine
Core <- Memory <- Net
```

PlatformHost and PlatformEsp32 are the non-portable edges; only they may reach
OS/SDK headers and both are excluded from `CheckDependencyBoundaries.py`.

## Concepts

- One package per engine layer; no package compiles another layer's sources.
- Each package owns its identity (CMake target, `MicroWorld::*` alias,
  PlatformIO package name, header namespace) — these are frozen.
- Per-package `AGENTS.md` files own durable boundaries; `../../PROGRESS.md`
  owns live status and evidence.

## Verification

Build and test the whole engine from the repo root (`cmake -S . -B build`),
or configure a single package standalone (`cmake -S Modules/<Name> -B build-<name>`).
Run the dependency-boundary checker over the portable packages and the
profile-map checker on built consumer maps.
