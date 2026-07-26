# MicroWorld Net Package

Inherits `../AGENTS.md`.

## Architecture

`microworld-net` is the adjacent portable byte-I/O package above Core.
Its dependency direction is `Core <- Net`: higher packages may depend on Net,
while Net may depend only on Core and the C++17 standard library. Net must not
depend on Object or Engine; the Integration package is the only one permitted
to see both.

The package owns a bounded byte reader/writer, one non-blocking `INetDriver`
contract, one caller-storage-backed fixed-capacity `TNetManager`, the `TNetHost`
session layer above it, wire framing, explicit `ENetResult` outcomes, and a
deterministic host loopback driver. It does not own sequence numbers, retries,
reliability, message routing, authentication, replication, real transports,
threads, platform adapters, or vendor SDK code: reliability and routing belong
to Messaging, and real transports to the platform packages.

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
  consuming operation rejects as `Invalid` without dereferencing null. A
  valid `{nullptr, 0}` empty span is observable without dereferencing null:
  `RemainingBytes`/`WrittenBytes` return an empty view with a null data
  pointer. A failed byte operation must not partially advance the cursor,
  modify output parameters, or alter bytes outside the previously accepted
  prefix.
- `Net/UdpAddressCodec.h` holds the 6-byte IPv4+port `FNetAddress` encoding
  (`MakeUdpAddress`, `IsUdpAddress`, `UdpAddressPort`, and
  `MakeUdpAddressFromPackedHostOrder`) as pure `constexpr` arithmetic with no OS
  includes, so both UDP platform adapters and their drivers share one definition
  without breaching the `Core <- Net` boundary.
- `INetDriver` exposes one bounded non-blocking `TrySend` and one bounded
  non-blocking `TryReceive`. One call performs at most one transport
  operation. Every receive is transactional: on `Full`, `Invalid`, or
  `Unavailable` the destination and `FNetReceiveResult::BytesReceived` are
  unchanged. The interface owns no clock, thread, retry, peer identity,
  session, or protocol behavior.
- `TNetPacketStorage<MaxPackets, MaxPacketBytes>` is the smallest fixed
  storage type the manager needs; both capacities must be nonzero and are
  rejected at compile time otherwise. The caller constructs one instance and
  lends it to the manager by reference. The packet arrays are private and
  observed only by the matching `TNetManager` specialization, so tests and
  consumers must observe storage behavior through the manager and driver.
- `TNetManager<MaxPackets, MaxPacketBytes>` holds exactly one externally
  referenced `INetDriver` and one externally referenced `TNetPacketStorage`,
  maintains one deterministic outbound FIFO, copies complete accepted packets
  on queue, attempts at most the FIFO head per send advance, and performs at
  most one direct driver receive. Rejected operations leave state unchanged
  and preserve ordering.
- `TNetHost<MaxPeers, MaxPacketBytes>` is the session layer above `TNetManager`.
  One `ENetMode` role — Standalone, Client, ListenServer, or DedicatedServer —
  selects which traffic the host originates and accepts; a fixed peer table
  carries `Hello`/`Welcome` admission, heartbeats, and timeout eviction; and
  channel 0 is reserved for control. The host owns no reliability and no
  ordering across messages.
- `THostLoopback<CapacityPackets, PacketBytes>` is a deterministic fixed-
  capacity `INetDriver` for host tests: a full send never overwrites accepted
  packets, an empty receive returns `Unavailable`, a too-small receive
  destination returns `Full` while retaining the head packet, and a null
  destination with nonzero length returns `Invalid` before the empty-queue
  check so the rejection is observable even on an empty loopback.
- Portable code uses fixed-width/value types, bounded fixed storage,
  deterministic lifetimes, and no RTTI, exceptions, logging, threads, clocks,
  heap containers, SDK calls, or global mutable state.
- `FPacketDropDriver` is a test/demo `INetDriver` decorator that wraps another
  driver by reference and silently drops every Nth outgoing send; it is a
  loss injector, not reliability or a real transport.

## Verification

Configure and build this package independently with CMake, compile its public
headers under C++17 with strict warnings, exceptions disabled, and RTTI
disabled, run the Core dependency-boundary checker with explicit Core and Net
package roots, run the profile-map checker on a Core+Net consumer
link map, and run the package tests required by the current package scope.
Keep Net absent from Object-only and Engine-only profiles. Live status and
evidence belong only in `../../PROGRESS.md`.
