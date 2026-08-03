# ADR 0012: Fixed-capacity Compiled Messaging

Status: Accepted

Date: 2026-08-03

Decision owner: Project owner

## Context

ADR 0007 made Messaging's public surface template-free but kept its capacities in
traits owned by the engine. That left two conflicting models: one concrete public
system and multiple consumer-defined storage shapes. The traits added configuration
machinery without a supported need for per-application capacity variants, and they
prevented Messaging from being compiled once as an ordinary module.

The shipped capacities already define one bounded contract. Their current values
preserve compatibility and bounded memory; they are not measured minima or claims
that every application needs every slot.

## Decision

- **Messaging is one concrete compiled system.** Its behavior is compiled once and
  consumed without template instantiation or consumer-owned implementation.
- **Capacities are fixed on the Messaging system.** The concrete limits are four
  channels, sixteen subscriptions, 32 bytes of inline subscriber-callable storage,
  96 message payload bytes, and eight pending reliable messages.
- **Engine ownership does not imply capacity ownership.** The engine creates and
  hands out Messaging, but its own traits do not configure Messaging storage.
- **Capacity changes are contract changes.** A limit changes only for a supported
  application need or measured resource pressure, not as an application-by-
  application customization surface.
- **This supersedes only ADR 0007's traits-capacity decision.** ADR 0007's system
  ownership, dependency, channel, transport-independence, and reliability decisions
  remain in force.

## Consequences

- Every application gets the same reviewable Messaging memory and behavior bounds.
- Consumers no longer carry Messaging capacities through engine traits or compile
  Messaging implementation for each traits shape.
- Applications that use less Messaging cannot zero individual capacities to reduce
  the system object; static linking can omit the module only when nothing references
  it.
- Changing a capacity requires one deliberate system-wide decision and verification
  against memory budgets, supported payloads, and reliable-send concurrency.

## Alternatives considered

- **Keep capacities in engine traits.** Rejected: it couples engine configuration to
  Messaging storage and preserves template machinery without a supported variant.
- **Add a standalone Messaging traits type.** Rejected: it moves the same unused
  customization surface rather than removing it.
- **Allocate capacities dynamically.** Rejected: runtime allocation would make MCU
  memory use less predictable and violate the fixed-capacity architecture.

## Revisit triggers

- A supported application cannot fit within one of the concrete limits.
- Measurement shows a fixed limit causes unacceptable RAM, code-size, or timing cost
  on a supported target.
- Multiple capacity profiles become a demonstrated product requirement and their
  extra compilation and verification cost is accepted explicitly.
