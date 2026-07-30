# MicroWorld Platform Edges

Inherits `../../AGENTS.md`.

## Architecture

`Platform/` holds the three non-portable edges: `Host`, `Esp32`, and `Pico`.
Each is a separate PlatformIO library (its own `library.json`) because each
family needs a different toolchain — host sockets, ESP-IDF/lwIP, or the native
Pico SDK — and a single merged `srcDir` could not compile all three on any one
toolchain. They sit under `MicroWorld/Platform/` so the folder tree still names
the architecture, but the portable `library.json` excludes this subtree from its
`srcDir` sweep via `build.srcFilter`.

Platform edges may reach OS/SDK headers and are excluded from
`CheckDependencyBoundaries.py` portable-system enforcement. They depend inward
on Core, Engine, Transport, and optional RadioE32 as needed and never the
reverse.

## Concepts

- Each family ships adapter interfaces behind the portable `INetDriver`,
  `TimePointMilliseconds`, and output-device contracts described in
  `../../docs/Porting.md`; portable code never reaches OS/SDK headers directly.
- All OS/SDK headers are confined to `Detail/` implementation headers; public
  declarations stay platform-neutral.
- Compile success on a platform family is a compile-only proof, never a runtime,
  timing, heap, stack, radio, or wired-link claim; see each family's
  `benchmarks/Results/` for the measured evidence that closes that gap.

## Verification

Build the host edge with the superbuild (`microworld_platform_host` target,
links `ws2_32` on Windows). Build the Esp32 edge with PlatformIO for
`esp32-s3-devkitc-1` (`espidf` framework); keep
`-fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Werror`. The Pico edge
requires the native Pico SDK and is built by the consumer harness at
`../../tests/Core/consumer/pico-freertos/`. A newly changed adapter must be
smoke-run on the real target before any runtime-readiness claim.
