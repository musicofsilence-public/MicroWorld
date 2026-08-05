# ADR 0007: Networking Dissolves Into Messaging

Status: Accepted

Date: 2026-08-01

Partial supersession: [ADR 0012](0012-fixed-capacity-compiled-messaging.md)
replaces only the traits-held capacity decision and its related consequence. All
other decisions remain accepted.

Further partial supersession: [ADR 0015](0015-networking-restored-above-messaging.md)
replaces this record's deletion of Networking and its role-derived channel-target
decision. The Core transport-device contract, Messaging-owned channels and
delivery guarantees, local-only Messaging mode, and Engine-owned Messaging remain
accepted.

## Context

Networking existed to join Messaging and Transport without letting them see each
other, and justified itself with one invariant: the role a node plays decides
where its channels send. The invariant turned out to be a one-line mapping, and
everything around it was wiring — ownership slots, handles, and a capacity blob
re-exporting other systems' limits — which this project already assigns to
application entry points. Meanwhile Messaging claimed transport independence while its
channel binding was compile-time tied to a concrete Transport type.

## Decision

- **Networking is deleted, not moved.** Five portable systems remain. Its one real
  duty — deciding where a channel sends — becomes a value the channel is created
  with, so the role machinery is deleted rather than relocated.
- **The transport device contract is Core vocabulary.** One shape — send one
  packet to an address, drain arrivals, publish the largest accepted payload —
  named `ITransportDevice`, with one uniform address value. Messaging consumes the
  shape, each medium realises it, and neither names the other. Engine and
  Transport still never meet; now Messaging and Transport never meet either.
- **Messaging owns channels.** A channel is one name, one reliability declaration,
  and optionally one device plus the address it sends to. Several channels may
  share one device. Broadcast is an address value, not a second mechanism.
- **A channel without a device is the local mode, not a degraded one.** Local
  delivery always happens; a device adds remote reach. Messaging with no transport
  linked is a complete, working system.
- **Delivery guarantees live in Messaging, per channel.** Transport answers that
  it accepted bytes, never that they arrived, because an acknowledgement is a
  message from the peer and a byte layer that reads one has stopped carrying
  payload opaquely.
- **The engine creates and hands out the messaging system.** `FMessagingSystem` is
  the one system the engine knows beyond Core — a sanctioned dependency edge, so
  composing messaging is one call rather than a wiring exercise an application
  performs.
- **The public messaging surface is template-free.** Fixed capacities remain
  compile-time, held in one traits home rather than spread across the API.

## Consequences

- A whole system and its glue disappear; the dependency graph loses a box and
  gains one sanctioned edge, Engine to Messaging.
- The engine's traits grow messaging capacities and the engine pays that storage
  even when unused — near zero when the capacities are zeroed, and accepted as the
  cost of engine-owned internals.
- The `MicroWorld::Networking` namespace retires. This supersedes only that entry
  of ADR 0006's namespace list; every other decision there remains in force.
- Per ADR 0004 the tree is downstream of the model, so the system's folder is
  deleted and its build target retires with it — a breaking change, versioned as
  one.
- Every consumer that composed networking rewrites to channel creation; the
  rewrite is mechanical and the new composition is smaller at every site.

## Alternatives considered

- **Keep Networking as the composer.** Rejected: once role reduces to a value on
  the channel, what remains is wiring, and the model itself recorded that
  post-role Networking should be deleted.
- **Merge into Transport instead.** Rejected: one element would own both the byte
  contract and the message types, and nothing would then forbid a device from
  reading a message.
- **Compose messaging at the root and keep the engine ignorant of it.** Rejected:
  it preserves a pure engine dependency set at the price of every application
  hand-wiring internals — the opposite of what this product promises its
  developer.

## Revisit triggers

- A real application needs channel targets derived from a node role again — the
  responsibility whose absence justified the deletion.
- Measurement shows engine-held messaging storage costing an application that
  uses no messaging.
