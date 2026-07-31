# MicroWorld Platform/Host

Inherits `../../../AGENTS.md`.

## Architecture

`MicroWorldPlatformHost` is the non-portable host platform adapter. It supplies
real host UDP transport over OS sockets (WinSock on Windows, BSD on POSIX) and a
`steady_clock`-based time source behind the portable `IDevice` /
`TimePointMilliseconds` interfaces described in `../../../docs/Porting.md`. It
ships the two-node UDP demo as its worked acceptance evidence. It depends inward
on Core and Transport as needed and never the reverse, and it is excluded from
`CheckDependencyBoundaries.py` — it has no system key in that tool's portable
table.

## Concepts

- The two adapter interfaces are `FHostTimeSource` (clock) and `FHostWifiDevice`
  (`IDevice` transport); portable code never reaches WinSock/BSD headers
  directly.
- `FWinSockScope` is a reference-counted RAII guard: the first construction
  performs `WSAStartup`, the last destruction performs `WSACleanup`, and both
  are no-ops on POSIX.
- All OS socket headers are confined to the `Internal/UdpSocketPlatformImplementation.h`
  private header; public declarations stay platform-neutral.

## Verification

Build with CMake via the superbuild (`microworld_platform_host` target), linking
`ws2_32` on Windows. Keep `-fno-exceptions -fno-rtti` (or MSVC equivalents),
warnings as errors. Run the host tests and the two-node demo as the real-socket
acceptance proof.
