# Net Public Headers

Inherits `../AGENTS.md`.

## Architecture

Public headers under `include/MicroWorld/Net/` expose the bounded byte I/O
contract: `ENetResult`, `FByteWriter`, `FByteReader`, `INetDriver`,
`FNetManager`, and `FHostLoopback`. Headers are header-only except for
`INetDriver`'s out-of-line destructor, which lives in `src/NetDriver.cpp` to
give the Net archive stable linker evidence.

## Concepts and boundaries

- Every header uses `#pragma once`, the flat `MicroWorld` namespace, and the
  repository doc-comment style: each declaration explains why it exists, the
  invariant it makes observable, or the ownership boundary it protects.
- Byte I/O operates only on caller-owned `TSpan` views; the package performs
  no allocation and owns no storage of its own beyond fixed caller-owned
  buffers passed to its value types.
