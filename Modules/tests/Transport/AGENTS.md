# Transport Byte Behavior Tests

Inherits `../../AGENTS.md`.

## Architecture

The Transport test executable owns byte I/O, Transport's realisations of Core's
non-blocking `ITransportDevice` contract, `TTransportManager`, byte frame
encoding/decoding, `Core::ETransportResult` outcomes, the
host loopback device, the packet-drop decorator, the E32 node-address codec,
and the portable RadioE32 device over `IUartByteStream`. Net and RadioE32 tests
were separate; they folded into one Transport test set. Messaging owns message
framing, delivery, and session-independent reliability tests.

## Concepts

Tests assert transactional receive semantics, normalized result codes, FIFO
ordering, and bounded fixed-capacity behavior with caller-owned storage.

## Verification

Run the `microworld_transport_tests` target via CTest.
