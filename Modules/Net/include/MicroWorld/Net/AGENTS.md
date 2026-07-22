# Net Byte I/O Headers

Inherits `../../AGENTS.md`.

## Architecture

The `Net/` headers define the bounded byte-I/O contract in dependency order:
`NetResult.h` is standalone, `ByteWriter.h` and `ByteReader.h` depend on the
Memory-owned `TSpan`, `NetDriver.h` defines the non-blocking interface,
`NetPacketStorage.h` defines the caller-owned fixed packet storage,
`NetManager.h` composes a driver with that storage as a FIFO, and
`HostLoopback.h` provides the deterministic host driver implementation.

## Concepts and boundaries

- `ENetResult` is the single outcome enum shared by byte I/O, the manager,
  and every driver, with one normalized meaning per value: `Success` (complete
  operation), `Full` (valid operation lacks destination/queue/transport
  capacity), `Invalid` (invalid span/configuration, oversized packet, or
  truncated byte-reader request), and `Unavailable` (a valid non-blocking
  driver/manager operation has no work or cannot progress now).
- `FByteWriter` and `FByteReader` never allocate: they observe caller storage
  and fail transactionally so cursors, outputs, and accepted bytes stay
  unchanged on any error. A backing span bound to `{nullptr, nonzero}` is an
  invalid configuration that every mutating operation rejects as `Invalid`
  without dereferencing null.
- `INetDriver` is the only transport abstraction; one call performs at most
  one non-blocking send or receive and returns an explicit result. Every
  receive is transactional: on `Full`, `Invalid`, or `Unavailable` the
  destination and `FNetReceiveResult::BytesReceived` are unchanged.
- `FNetPacketStorage<MaxPackets, MaxPacketBytes>` is the smallest fixed
  storage type the manager needs; both capacities must be nonzero. The caller
  constructs one instance and lends it to the manager by reference so the
  manager owns no packet storage of its own. The packet arrays are private and
  observed only by the matching `FNetManager` specialization.
- `FNetManager` copies complete accepted packets into the caller-supplied
  storage and advances at most the FIFO head per send; a driver `Full`,
  `Unavailable`, or `Invalid` retains the head and preserves ordering. The
  manager performs at most one direct driver receive and never builds an
  inbound queue.
- `FHostLoopback` is a deterministic fixed-capacity loopback for host tests;
  it never overwrites accepted packets and never returns partial receives.
