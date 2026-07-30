# Host Platform Implementations

Inherits `../AGENTS.md`.

## Architecture

`src/` is the SOLE home of WinSock/BSD socket headers in this package:
`UdpSocketPlatformImplementation.h` is included only by `HostUdpDriver.cpp`. A
public header must never reach it.

## Concepts

- Every WinSock/BSD divergence (handle width, close, non-blocking mode,
  last-error classification, the MSG_PEEK-vs-MSG_TRUNC size probe) is hidden
  behind helpers in the platform-implementation header so `HostUdpDriver.cpp`
  reads one platform-free receive/send path.
- The POSIX branch compiles but is not verified on this Windows-only host; it
  exists so the ESP32 UDP adapter can reuse the same interfaces under a POSIX
  build.

## Verification

Build with CMake, warnings as errors, exceptions and RTTI disabled, linking
`ws2_32` on Windows. Exercise the real-socket path through `tests/` and the
two-node demo.
