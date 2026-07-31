# ADR 0006: Model-conforming per-system namespaces

Status: Accepted

Date: 2026-07-31

## Decision

Public MicroWorld symbols use the namespace that names their modeled system:

- `MicroWorld::Core`
- `MicroWorld::Engine`
- `MicroWorld::Messaging`
- `MicroWorld::Transport`
- `MicroWorld::Networking`
- `MicroWorld::Application`
- `MicroWorld::Platform::Host`
- `MicroWorld::Platform::Esp32`
- `MicroWorld::Platform::Pico`

Transport has three intentional leaf namespaces: `Device`, `Address`, and
`FrameCodec`. Core raw storage uses `MicroWorld::Core::RawStorage` because its
ownership and representation boundary is distinct from the rest of Core.

`MicroWorld::Tests` remains flat so test fixtures stay separate from the
runtime model. Compatibility aliases and flat namespace bridges are not part
of the public contract.

## Consequences

The namespace hierarchy mirrors the architecture model and makes dependency
boundaries visible at call sites. Consumers must update qualified names when
moving between systems; the explicit migration cost is preferred over a
second compatibility vocabulary that could drift.

## Relationship to ADR 0005

This decision supersedes only ADR 0005's namespace clause. All other decisions
in ADR 0005 remain in force.
