# Platform/Host Internal

Inherits `../../../../AGENTS.md`.

## Architecture

This subfolder holds the host UDP socket platform implementation header that
opens OS socket (WinSock/BSD) symbols. It exists so the public
`MicroWorld/Platform/Host/HostWifiDevice.h` declaration stays free of OS headers.

## Concepts

- `UdpSocketPlatformImplementation.h` is a private implementation header:
  consumers must not depend on it, and only `HostWifiDevice.cpp` includes it.

## Verification

Covered by the PlatformHost behavior tests in `../../../../tests/Platform/Host/`.
