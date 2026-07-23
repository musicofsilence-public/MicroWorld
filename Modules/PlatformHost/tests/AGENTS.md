# Host Platform Tests

Inherits `../AGENTS.md` and `../../Core/tests/AGENTS.md`.

## Architecture

Tests consume PlatformHost only through its public contracts (`FHostUdpDriver`,
`FHostTimeSource`) and shared Core test support. They open real loopback
sockets and must not depend on `src/` internals or shared mutable state
between cases.

## Concepts

Each case owns its own bound socket(s) and observes public results:
non-blocking send/receive over real localhost UDP, elapsed-time reporting,
and end-to-end delivery between two independently owned drivers.

## Verification

Compile with C++17, strict warnings, exceptions disabled, and RTTI disabled.
Exercise direct public postconditions over real sockets: bind success,
non-blocking would-block, and end-to-end datagram delivery.
