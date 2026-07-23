# PlatformHost Public Include Boundary

Inherits `../AGENTS.md`.

## Architecture

`include/` is the only supported compile-time surface of the PlatformHost
package. Public headers declare `FHostTimeSource`, `FHostUdpDriver`,
`FWinSockScope`, and the `UdpAddress` helpers against the portable
`INetDriver` and `TimePointMilliseconds` contracts; they may depend inward on
Net, Object, Memory, and Core public headers but must declare no WinSock or
BSD socket type in a way that leaks into a caller's include of this
directory.

## Concepts

Public declarations stay platform-neutral even though their implementations
are not: `FHostUdpDriver`'s constructor takes a plain port number, and
`MakeUdpAddress`/`IsUdpAddress`/`UdpAddressPort` operate on the shared,
opaque `FNetAddress` byte encoding.

## Verification

Each public header must compile independently under C++17 with warnings as
errors, exceptions disabled, and RTTI disabled, on both the Windows and POSIX
branches this package supports. Document exported functions with the
real-socket behavior they wrap.
