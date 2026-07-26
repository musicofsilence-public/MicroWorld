# Messaging Actor-Transport Headers

Inherits `../../AGENTS.md`.

## Architecture

The `Messaging/` headers define the portable actor-message layer:
`Message.h` owns the message vocabulary and codec; `MessageRouter.h` owns
bounded queued routing; and channel and reliable-delivery headers adapt
caller-supplied transport facades while participating in caller-owned
deterministic dispatch/flush ordering.

## Concepts and boundaries

- Message ids and payload views are value-based, while handler and channel
  relationships are explicit non-owning bindings whose lifetimes belong to the
  caller's composition root.
- Queue, handler, channel, frame, and retry storage is fixed capacity. A send
  or receive returns a typed result instead of allocating, throwing, or hiding
  failure.
- Generic channel bindings must preserve the Core-only dependency boundary:
  they may not name Net, Engine, Integration, driver, or SDK types.
