# ADR 0008: The Session Layer Survives The Networking Dissolution

Status: Superseded by [ADR 0015](0015-networking-restored-above-messaging.md)

Date: 2026-08-01

Supersession: ADR 0015 moves admission, peer identity, liveness, logical
routing, and session protocol above Messaging into Networking. It also replaces
the fixed-address Messaging boundary that made this Transport-owned session
necessary. This record remains historical context only.

## Context

ADR 0007 deleted the Networking system. It said nothing about `TTransportHost`,
the Transport-side session layer that performs peer admission, holds a
generation-checked peer table, paces Hello traffic, and evicts peers that stop
answering.

That silence let a plan task drift. The implementation plan grew a task reading
"retire session layer (`TransportHost`, `TransportManager`, `TransportProtocol`,
packet storage)", while the approved concept for the same refactor said the
opposite — that the session host "becomes an optional Transport-side decorator
that itself implements `ITransportDevice`". Three artifacts, three answers, and
the most destructive one was scheduled without the decision ever being made.

Two facts settle it.

**Messaging cannot express what the session layer provides.** A channel's
address is fixed when the channel is created. A UDP server therefore cannot
address a client it has not already met. Messaging's own code records the
boundary it accepted: an acknowledgement "goes to the channel's configured
address, not to whoever sent the message. That is correct for the point-to-point
shape a reliable channel describes today." Dynamic peer admission,
reply-to-a-discovered-address, and eviction have no replacement anywhere in the
portable systems.

**The session layer has live consumers that were never scheduled for a port.**
Examples 16 (TwoBoardUdp), 19 (UartMessaging), 26 (MessagingOverLora) and
`TwoNodeDemo` use `TTransportHost` directly, without the retired Messaging
router. Three are hardware-verified. Deleting the session layer would also have
removed the plan's own hardware gate, which requires running example 26.

Nothing in the session layer was replaced by Messaging. The genuinely replaced
set is exactly the Networking system and the old Messaging headers, both already
scheduled for deletion elsewhere.

## Decision

- **The session layer stays in Transport as an optional feature.**
  `TTransportHost` and its supporting types are not deleted, and their tests
  stay. It is opt-in: a composition that does not name it does not pay for it.
- **Messaging remains point-to-point with a fixed per-channel address, by
  design.** This is a stated boundary, not an oversight. A channel names one
  address when it is created and sends there.
- **Peer liveness belongs to Transport, not Messaging.** Heartbeat and timeout
  are inseparable from the peer table that discovery requires, so they live with
  it.
- **The `ITransportDevice` decorator is not built yet.** The concept's idea of a
  session host that implements `ITransportDevice`, so a Messaging channel could
  run over a session without knowing it, is recorded as the future direction and
  deliberately not scheduled. No consumer needs it, and building it now would be
  complexity earned by symmetry rather than by requirement.
- **What the session layer still owes the refactor is vocabulary, not
  deletion.** Its headers move off the Transport-local address and result shims
  onto `Core::FDeviceAddress` and `Core/IO/*`, matching every other Transport
  type. That is a mechanical port with no behaviour change.

## Consequences

- Transport keeps a capability Messaging does not have, so the two are not
  ranked: a composition picks the one its problem needs. Point-to-point or
  known-address work uses a Messaging channel; work that must discover and track
  peers uses a session host.
- The examples ladder keeps its endpoint. The client/server progression that
  ends with two boards talking survives, along with the `ENetworkMode` concept
  map entry that mirrors UE5's `ENetMode`.
- MicroWorld carries two things that both look like "networking" to a newcomer.
  That is the real cost of this decision, and it is paid in documentation: each
  must say plainly what it is for and when to reach for the other.
- The decorator remains available later without rework, because
  `TTransportHost` already holds a `Core::ITransportDevice&` — the same contract
  it would need to implement.
- **Revisit when** a Messaging channel needs peers it did not know at creation
  time. That is the trigger, and the concept's decorator is the candidate
  design.
