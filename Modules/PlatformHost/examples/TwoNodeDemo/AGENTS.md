# Two-Node UDP Demo

Inherits `../AGENTS.md`.

## Architecture

`Main.cpp` is one executable hosting TWO independent MicroWorld nodes over
real localhost UDP: a dedicated server built on a full `TEngineHost` (bound to
an `IEngineSystem` via `TNetHostSystem`) and a bare client `TNetHost`. Both
nodes are driven from one process through a single deterministic interleaved
loop, so the printed trace is byte-identical across runs.

## Concepts

- A client channel-1 input event (`SendTo` the reserved spawn opcode) drives
  the server to `CreateObject`+`SpawnActor` one `AActor` into its world; the
  server's per-tick channel-2 broadcast reports the resulting world actor
  count back to the client.
- The server advances only through `TEngineHost::Tick` (network dispatch is
  step 1, flush is step 7); the bare client pumps `PumpSend`/`PumpReceive`
  explicitly since it owns no engine host.
- The full run — handshake, two spawn requests, three state broadcasts, and
  completion — prints a fixed 14-line transcript.
- A shared `LogicalClockMilliseconds`, not wall time, advances every
  sub-action so the transcript is deterministic; readiness polling bounds the
  only real wait.

## Documentation and verification

Document each helper by the protocol step or trace line it produces. Build
and run the target and diff its stdout transcript against the 14 documented
lines.
