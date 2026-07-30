# Transport Host Behavior Tests

Inherits `../../AGENTS.md`.

## Architecture

The Transport test executable owns byte I/O, the non-blocking `INetDriver`
contract, `TNetManager`, `TNetHost`, wire framing, `ENetResult` outcomes, the
host loopback driver, the packet-drop decorator, the E32 node-address codec,
and the portable RadioE32 driver over `IUartByteStream`. Net and RadioE32 tests
were separate; they folded into one Transport test set.

## Concepts

Tests assert transactional receive semantics, normalized result codes, FIFO
ordering, and bounded fixed-capacity behavior with caller-owned storage.

## Verification

Run the `microworld_transport_tests` target via CTest.
