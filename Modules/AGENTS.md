# MicroWorld Modules

Inherits `../AGENTS.md`.

## Architecture

`Modules/` is the container for the engine's per-layer packages. Each
subdirectory is one CMake/PlatformIO package with its own `CMakeLists.txt`,
`library.json`, public `include/MicroWorld/...` headers, and scoped
`AGENTS.md`. Dependencies point inward:

```text
Core <- Object <- Engine
Core <- Net <- RadioE32
```

PlatformHost, PlatformEsp32, and PlatformPico are non-portable edges; only they
may reach OS/SDK headers and all remain outside portable-package enforcement.
RadioE32 is optional portable framing over Core's narrow byte seam, not a HAL.

## Concepts

- One package per engine layer; no package compiles another layer's sources.
- Each package owns its identity (CMake target, `MicroWorld::*` alias,
  PlatformIO package name, header namespace) — these are frozen.
- Per-package `AGENTS.md` files own durable boundaries; each package's headers
  and tests own its current behavior.

## Verification

Build and test the whole engine from the repo root (`cmake -S . -B build`),
or configure a single package standalone (`cmake -S Modules/<Name> -B build-<name>`).
Run the dependency-boundary checker over the portable packages and the
profile-map checker on built consumer maps.
