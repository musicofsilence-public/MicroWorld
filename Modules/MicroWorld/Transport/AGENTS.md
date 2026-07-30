# MicroWorld Transport System

Inherits `../../AGENTS.md`.

## Architecture

Transport is the portable byte-I/O system. Its dependency direction is
`Core <- Transport`: it may depend only on Core and the C++17 standard library.
Transport must not depend on Engine, Messaging, or Application, and no portable
system may see both Engine and Transport — only a composition root joins them.

The system owns a bounded byte reader/writer, one non-blocking `INetDriver`
contract, one caller-storage-backed fixed-capacity `TNetManager`, the `TNetHost`
session layer above it, wire framing, explicit `ENetResult` outcomes, a
deterministic host loopback driver, the one-byte E32 node-address shape, and the
portable RadioE32 transport over `IUartByteStream`. It does not own sequence
numbers, retries, reliability, message routing, authentication, replication,
real transports, threads, platform adapters, or vendor SDK code: reliability and
routing belong to Messaging, and real transports to the platform families.

Net and RadioE32 were separate packages; they folded into Transport because a
system is a responsibility and a package is a build target, and the architecture
model states the byte contract and every medium as one system. The IP/protocol
sources and the RadioE32 sources are toggled by the `MICROWORLD_TRANSPORT_IP` and
`MICROWORLD_TRANSPORT_RADIO` CMake options so a Pico build can omit IP code and a
radio-less build can omit the E32 framing.

`Detail/` holds fixed transport state and other implementation mechanics (the
portable E32 transport state); consumers must not depend on those headers.

## Concepts and boundaries

- `ENetResult` keeps every byte, queue, packet, and driver outcome explicit
  with one normalized meaning per value: `Success` (complete operation),
  `Full` (valid operation lacks destination/queue/transport capacity),
  `Invalid` (invalid span/configuration, oversized packet, or truncated
  byte-reader request), and `Unavailable` (a valid non-blocking driver/manager
  operation has no work or cannot progress now). No path silently truncates
  or drops data.
- `FByteWriter` and `FByteReader` operate only on caller-owned
  `TSpan<std::uint8_t>` and `TSpan<const std::uint8_t>`. A backing span bound
  to `{nullptr, nonzero}` is an invalid configuration that every mutating or
  consuming operation rejects as `Invalid` without dereferencing null.
- `UdpAddressCodec.h` holds the 6-byte IPv4+port `FNetAddress` encoding as pure
  `constexpr` arithmetic with no OS includes, so both UDP platform adapters and
  their drivers share one definition without breaching the `Core <- Transport`
  boundary.
- `E32Lora.h` owns the one-byte E32 node-address shape and payload limit;
  `Detail/E32LoraTransportState.h` owns the portable E32 state, and
  `RadioE32Driver.h` owns the driver that uses it over `IUartByteStream`.
- `INetDriver` exposes one bounded non-blocking `TrySend` and one bounded
  non-blocking `TryReceive`. Every receive is transactional: on `Full`,
  `Invalid`, or `Unavailable` the destination and `FNetReceiveResult::BytesReceived`
  are unchanged. `AdvanceTransmit` is a no-op by default and lets staged drivers
  make one bounded physical transmit step after a host FIFO drain.
- `TNetManager<MaxPackets, MaxPacketBytes>` holds exactly one externally
  referenced `INetDriver` and one externally referenced `TNetPacketStorage`,
  maintains one deterministic outbound FIFO, and performs at most one direct
  driver receive.
- `TNetHost<MaxPeers, MaxPacketBytes>` is the session layer above `TNetManager`.
  One `ENetMode` role — Standalone, Client, ListenServer, or DedicatedServer —
  selects which traffic the host originates and accepts; a fixed peer table
  carries `Hello`/`Welcome` admission, heartbeats, and timeout eviction; and
  channel 0 is reserved for control.
- `THostLoopback` is a deterministic fixed-capacity `INetDriver` for host tests.
- `FPacketDropDriver` is a test/demo `INetDriver` decorator that wraps another
  driver by reference and silently drops every Nth outgoing send; it is a loss
  injector, not reliability or a real transport.
- RadioE32 operations are non-blocking, bounded, fixed-capacity, and explicit
  about success, backpressure, capacity, and invalid data. Platform adapters own
  UART configuration, lifetime, buffering policy, and vendor SDK calls.
- Portable production and test code use no heap allocation, exceptions, RTTI,
  hidden clocks, threads, platform SDKs, or global mutable state.

## Verification

Build the engine from the repo root; Transport is the `microworld_transport` target
(with `MicroWorld::Transport` and `MicroWorld::Transport` aliases). Run the
dependency-boundary checker with the Transport system root and the Transport
behavior tests after changes. Keep Transport absent from Engine-only and
Engine/Messaging-only profiles. This guide owns durable boundaries; the system's
headers and tests define its current behavior.
