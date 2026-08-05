# ADR 0015: Networking Restored Above Messaging

Status: Accepted

Date: 2026-08-05

Decision owner: Project owner

Supersedes: the Networking-deletion and role-derived channel-target decisions in
[ADR 0007](0007-networking-dissolves-into-messaging.md), and all of
[ADR 0008](0008-session-layer-survives-the-networking-dissolution.md).

## Context

Messaging can deliver typed messages and retain its local mode, but a fixed
channel destination cannot admit a newly discovered peer, validate its session,
or address it later. The former Transport session layer therefore owns policy
that belongs above Messaging: client and server roles, peer admission, liveness,
logical identity, and routed application messages.

The current World-subsystem baseline remains intact. Those are explicit,
World-scoped application services. A session must instead survive World and Actor
lifetime, so it cannot be a World subsystem or change subsystem registration,
initialization, or teardown rules.

## Decision

- **Networking is restored as one concrete system above Messaging.** It depends
  only on Core and Messaging. It never names Transport, a platform, Engine, or
  Application; Engine may depend on Networking, while Transport remains Core-only.
- **Network has only Client and Server roles.** It supports one server connection
  per client and at most four admitted server peers. There is no local peer,
  client-to-client route, authentication state, stable user identity, or public
  connection object.
- **The fixed capacity contract is retained.** Messaging has four stable links,
  four channels, sixteen subscriptions, and eight reliable pending frames;
  Networking has four peer slots. Its two private wire channels consume two
  Messaging channel and subscription slots, leaving two application channels.
  Capacity changes require measured evidence and a later ADR.
- **Messaging owns physical routes; Network owns logical routing.**
  `FMessagingRoute` is exactly `{FMessagingLinkId, FDeviceAddress}` and its link
  ID is an opaque stable slot. Registering the same device is idempotent;
  registering a fifth link returns `Full` without mutation, and links are never
  unregistered. Devices outlive their registered links and are advanced once by
  the composition owner. Messaging preserves an inbound sender route; Network
  validates it against a live peer, clears it before local delivery, and stamps a
  local-only source identifier resolvable through
  `FNetworkSystem::ResolveSenderPeer(const FMessage&)`, which returns invalid
  after disconnect or reuse. Actors and application handlers receive peer
  handles, never routes or addresses.
- **Network-routed application channels are local-only.** Their reliability
  selects the corresponding private best-effort or reliable wire channel.
  Messaging keeps its local delivery plus optional known-route default and offers
  explicit local-only and remote-only operations. Network rejects an application
  channel that has a default remote route or uses a reserved wire name.
- **Reliable delivery is route- and lifetime-safe.** Messaging assigns one
  monotonically increasing, never-reused 64-bit `ReliableMessageId` per instance.
  A pending frame keeps its channel, full destination route, ID, and encoded
  frame; an acknowledgement releases it only when all three match. IDs do not
  reset on reconnect or channel recreation; reaching `UINT64_MAX` makes future
  reliable sends return `Full`. Delivery remains bounded at-least-once, so
  duplicates remain possible.
- **Typed payload codecs are an allocation-free ADL contract.** Every typed
  message provides exactly `constexpr FNameId GetMessageNameId(const T&) noexcept`,
  `EMessagingResult EncodeMessagePayload(const T&, FMessageWriter&) noexcept`,
  and `EMessagingResult DecodeMessagePayload(FMessageReader&, T&) noexcept`.
  Readers and writers are bounded and little-endian; decoding consumes the whole
  payload and leaves its output unchanged on failure.
- **Peer identity is session-local and generation checked.** `FPeerId` holds an
  8-bit slot and 32-bit generation; `0xFF` is invalid, exhausted slots retire,
  and a 64-bit local source identifier preserves the pair. A 32-bit monotonically
  increasing connection attempt is echoed by admission replies; it and the live
  route must match every heartbeat, disconnect, and routed message. Attempt-ID
  exhaustion rejects later reconnects until a new Network instance is created.
- **Engine owns the optional Network instance; World only observes it.** Network
  creation requires Messaging and occurs before application channels and World
  creation. World captures a nullable non-owning pointer for Actor access.
  Messaging outlives Network, and Network outlives World and Actors. Startup is
  external devices in add order, Messaging, Network, then World; shutdown reverses
  that order. Network never drives a device or performs byte I/O.
- **The Transport session protocol is a migration target, not a compatibility
  layer.** All live consumers move to device, Messaging, and Networking
  composition before the old session layer is removed. No facade or duplicate
  protocol remains after migration.

## Consequences

- Actor code can send to one logical peer or server through its World without
  learning a transport endpoint.
- Direct Transport and direct local or known-route Messaging remain valid without
  Networking.
- Server fan-out performs at most one send for each of four live peers; client
  broadcast is absent.
- Engine gains optional fixed storage even when no Network is created. Resource
  cost must be measured before release; this record makes no size or timing claim.
- Network initialization and teardown must be transactional so failed reserved
  channel or subscription setup leaves no hidden state.

## Alternatives considered

- Keep session policy in Transport: rejected because peer admission and logical
  routing are message/session policy, while Transport is byte I/O.
- Extend Messaging with roles and peer state: rejected because it would mix
  subscription and delivery mechanics with application-session policy.
- Model Network as a World subsystem: rejected because session lifetime must
  outlive World actors and because the established subsystem contract remains for
  application services.

## Revisit triggers

- Measurements show the fixed Network or Engine storage exceeds a supported
  target budget.
- A supported composition needs more than four peers or links, or more than two
  Network-routed application channels.
- A demonstrated requirement needs multiple server sessions, authenticated
  identity, client-to-client routing, fragmentation, or a different delivery
  guarantee.
