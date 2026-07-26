# Application Public Headers

Inherits `../AGENTS.md`.

## Architecture

Public headers under `include/MicroWorld/Application/` expose the program-entry
contract: `FApplication` (the engine-owning application base and bounded `Run`
frame loop). `Application.cpp` is the only out-of-line source; it gives the
package stable linker evidence and keeps the lifecycle state machine in one
translation unit.

## Concepts and boundaries

- Every header uses `#pragma once`, the flat `MicroWorld` namespace, and the
  repository doc-comment style: each declaration explains why it exists, the
  invariant it makes observable, or the ownership boundary it protects.
- Headers may include Core, Object, and Engine public headers only; Net
  headers must not appear here — a networked application is handed an
  already-bound engine.
