# MicroWorld Net

MicroWorld Net is the bounded non-blocking byte-I/O and simple-messaging
package above Memory. It provides a byte reader/writer, one non-blocking
`INetDriver` with `FNetAddress` addressing, one fixed-capacity `TNetManager`,
explicit `ENetResult` outcomes, a deterministic host loopback driver, a
header-only wire framing codec, and a session/role host for embedded
applications.

Current status and recorded evidence live in
[PROGRESS.md](../../PROGRESS.md).

## What Net provides

- `ENetResult` reports `Success`, `Full`, `Invalid`, and `Unavailable` with one
  normalized meaning per value: `Success` (complete operation), `Full` (valid
  operation lacks destination/queue/transport capacity), `Invalid` (invalid
  span/configuration, oversized packet, or truncated byte-reader request), and
  `Unavailable` (a valid non-blocking driver/manager operation has no work or
  cannot progress now).
- `FByteWriter` appends single bytes and byte spans into a caller-owned
  `TSpan<std::uint8_t>` and reports position, remaining, and capacity. A
  backing span bound to `{nullptr, nonzero}` is an invalid configuration that
  every mutating operation rejects as `Invalid` without dereferencing null. A
  failed write does not advance the cursor or alter accepted bytes.
- `FByteReader` reads single bytes and byte spans from a caller-owned
  `TSpan<const std::uint8_t>` and reports position and remaining. An invalid
  backing span is rejected as `Invalid` without dereferencing null. A failed
  read does not advance the cursor or modify output parameters.
- `INetDriver` exposes one bounded non-blocking `TrySend(const FNetAddress&
  To, …)` and one bounded non-blocking `TryReceive(FNetAddress& OutFrom, …)`
  over caller-owned byte spans. Every receive is transactional: on `Full`,
  `Invalid`, or `Unavailable` the destination and
  `FNetReceiveResult::BytesReceived` are unchanged.
- `FNetAddress` is an opaque, bounded address (a fixed byte array plus a size)
  carried by `INetDriver` sends and receives. Its concrete encoding is owned
  by the platform adapter (e.g. 6-byte IPv4+port for UDP, 1-byte node id for
  LoRa), not by Net.
- `TNetPacketStorage<MaxPackets, MaxPacketBytes>` is the smallest fixed
  storage type the manager needs; both capacities must be nonzero. The caller
  constructs one instance and lends it to the manager by reference.
- `TNetManager<MaxPackets, MaxPacketBytes>` holds one externally referenced
  `INetDriver` and one externally referenced `TNetPacketStorage`, queues
  complete packets into that storage, attempts at most the FIFO head per send
  advance (retaining the head on any driver failure), and performs at most one
  direct driver receive.
- **Networking with roles** — `TNetHost<MaxPeers, MaxPacketBytes>` runs one of
  four `ENetMode` values (`Standalone`, `Client`, `ListenServer`,
  `DedicatedServer`) over a bounded peer table. It performs Hello/Welcome
  admission, heartbeat keepalive, and timeout eviction, exposes
  generation-checked `FPeerId` handles, and is driven entirely by
  caller-invoked `PumpReceive` / `PumpSend` against caller-supplied time (no
  hidden clock, no hidden allocation). Channel 0 carries control messages
  internally; channels 1..255 are delivered to a bounded multicast delegate.
  This is simple channel messaging, not replication or RPC.
- **Wire framing** — `Net/FrameCodec.h` is a portable, host-testable header
  with `ComputeCrc16Ccitt` (CRC-16/CCITT-FALSE: poly `0x1021`, init `0xFFFF`,
  no reflection, xorout `0x0000`), the transactional `EncodeFrame`, and the
  bounded `TFrameDecoder<MaxPayloadBytes>` state machine that resyncs on bad
  magic / oversize length / CRC mismatch. The E32 LoRa adapter uses it for
  on-the-wire framing.
- **UDP address codec** — `Net/UdpAddressCodec.h` is a portable, OS-free header
  holding the 6-byte IPv4+port `FNetAddress` encoding (`MakeUdpAddress`,
  `IsUdpAddress`, `UdpAddressPort`) plus `MakeUdpAddressFromPackedHostOrder` for
  rebuilding a sender address after `ntohl`/`ntohs`. Both UDP platform adapters
  and their drivers share this one definition.
- `THostLoopback<MaxPorts, MailboxCapacity, PacketBytes>` is a deterministic
  fixed-capacity loopback network (N embedded per-port `INetDriver`s sharing
  one mailroom) for host tests.
- `FPacketDropDriver` is a concrete `INetDriver` decorator that wraps another
  driver by reference and silently drops every Nth outgoing send (returns
  `Success` without touching the wire; `0` disables dropping), forwarding
  `TryReceive`/`MaxPacketBytes` verbatim and exposing a pure
  `DroppedSendCount()` query — a deterministic loss injector for tests and the
  guaranteed-delivery demo, not a transport.

The application owns the driver, the packet storage, the manager value, the
net host value, and all fixed buffers.

## Real transports

Real transports live in the **platform adapter packages**, which are
non-portable (they may include OS/vendor headers) and are excluded from
`CheckDependencyBoundaries.py`:

- [`microworld-platform-host`](../PlatformHost) — `FHostUdpDriver`
  over a host UDP socket (`127.0.0.1`), using the shared
  `Net/UdpAddressCodec.h` (`MakeUdpAddress` / `IsUdpAddress` /
  `UdpAddressPort`) for the 6-byte IPv4+port `FNetAddress` encoding.
- [`microworld-platform-esp32`](../PlatformEsp32) —
  `FEsp32UdpDriver` over lwIP, `FEsp32E32LoraDriver` over the E32 UART, and
  `Esp32LogSink`; it shares the same `Net/UdpAddressCodec.h` encoding (the
  adapter stays self-contained otherwise; the encoding never crosses the wire).

Both depend inward on Net (and Core/Memory); the reverse dependency is
forbidden.

## Build

```sh
cmake -S Modules/Net -B <build-directory>
cmake --build <build-directory>
ctest --test-dir <build-directory> --output-on-failure
```

CMake consumers link `MicroWorld::Net`. A successful compile or host test
does not establish target runtime margins or hardware behavior.

Net does not provide delivery retries, reliability guarantees,
authentication, replication/RPC, platform abstraction, or hardware APIs.
(Wire framing via `FrameCodec` and sessions via `TNetHost` are in Net; real
transports are provided by the platform adapter packages above.)
