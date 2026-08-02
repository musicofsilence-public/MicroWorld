# Platform/Host Internal

Inherits `../../../../AGENTS.md`.

## Architecture

This subfolder holds the host UDP socket platform implementation header that
opens OS socket (WinSock/BSD) symbols. It exists so the public
`MicroWorld/Platform/Host/HostWifiDevice.h` declaration stays free of OS headers.

## Concepts

- The `Internal/` socket-platform headers (`SocketHandle.h`, `SendOutcome.h`,
  `PeekProbe.h`, `ConsumeResult.h`, `OpenedSocket.h`) are private implementation
  headers: consumers must not depend on them, and only `HostWifiDevice.cpp` includes them.

## Verification

Covered by the PlatformHost behavior tests in `../../../../tests/Platform/Host/`.
