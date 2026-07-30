# Host Public Driver Headers

Inherits `../../AGENTS.md`.

## Architecture

This directory declares the host platform adapters: `FHostUdpDriver`
(`INetDriver` transport, the template both ESP32 UDP and E32 LoRa adapters
mirror), `FHostTimeSource` (clock interface), `FWinSockScope` (socket-stack
lifetime guard), and the `UdpAddress` forwarder onto the shared
`Net/UdpAddressCodec.h` 6-byte IPv4+port encoding. Their declarations depend
only on Net/Object/Memory/Core public headers and stay free of WinSock/BSD
headers; those live only in `src/*PlatformImplementation.h`.

## Concepts

- `UdpAddress.h` is a thin historical-path forwarder: the encoding it exposes
  is defined once in `Net/UdpAddressCodec.h` so host and ESP32 UDP drivers
  agree on one wire layout.
- `FHostTimeSource` captures a `steady_clock` baseline at construction and is
  safe to copy or default-construct by value; it owns no resource.
- `FWinSockScope`'s refcount is intentionally not thread-safe — the engine
  drives the host on one deterministic thread.

## Verification

Compile each header standalone under C++17 with warnings as errors,
exceptions disabled, and RTTI disabled, on both the Windows and POSIX
branches. Document every exported declaration with the real-socket behavior
it wraps.
