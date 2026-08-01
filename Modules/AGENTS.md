# MicroWorld Modules

Inherits `../AGENTS.md`.

## Architecture

`Modules/` is the single engine package: one `CMakeLists.txt`, one portable
`library.json`, and the `MicroWorld/` tree whose six subdirectories name the
contract-defined systems directly. The non-portable `Platform/{Host,Esp32,Pico}`
edges nest under `MicroWorld/Platform/`, each its own PlatformIO library because
each needs a different toolchain. Host tests and benchmarks live under `tests/`
and `benchmarks/`, outside `MicroWorld/` so the library's recursive `srcDir`
never compiles them. Dependencies point inward:

```text
Core <- Engine
Core <- Messaging
Core <- Transport
Core, Engine <- Application
```

Platform/Host, Platform/Esp32, and Platform/Pico are non-portable edges; only they
may reach OS/SDK headers and all remain outside portable-system enforcement.
RadioE32 is folded into Transport as optional portable framing over Core's narrow
`IUartByteStream` interface, not a HAL.

## Concepts

- One package; six portable systems plus three platform edges; no system compiles
  another system's sources.
- Each system owns its identity (CMake target, `MicroWorld::*` alias, header
  namespace) — these are frozen.
- Per-system `AGENTS.md` files own durable boundaries; each system's headers and
  tests own its current behavior.
- The portable `library.json` excludes the `Platform/` subtree via
  `build.srcFilter` so PlatformIO does not compile host/ESP32/Pico sources into
  the portable library.

## Verification

Build and test the whole engine from the repo root
(`cmake -S . -B build && cmake --build build --config Release &&
ctest --test-dir build -C Release`). The superbuild is the only path —
standalone per-system configuration is gone. Run the dependency-boundary checker
over the six portable systems and the profile-map checker on built consumer maps.
