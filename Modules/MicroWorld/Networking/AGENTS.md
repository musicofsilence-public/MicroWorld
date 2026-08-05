# Networking System

Inherits `../AGENTS.md`.

## Architecture

Networking sits above Messaging and depends only on Core plus Messaging. It owns
logical peer/session policy: client/server roles, four generation-checked peers,
admission, liveness, routed application delivery, and peer events. It must never
include Transport, Platform, Engine, Application, or an `ITransportDevice`.

Messaging owns registered links, routes, channels, subscriptions, frame codecs,
and delivery reliability. Network uses two private wire channels and republishes
validated application messages only through local Messaging delivery, stamping an
opaque source id rather than exposing an endpoint.

## Concepts

- A client owns exactly one server session and uses `ConnectToServer`,
  `DisconnectFromServer`, and `SendToServer`; a server admits at most four peers
  and uses `SendTo` or `Broadcast`.
- `FPeerId` is a role-relative, generation-checked connection handle. It is not
  an authenticated user or hardware identity, and it becomes invalid on
  disconnect or reconnect.
- The application composition owner registers and advances devices. Networking
  only drives its caller-supplied liveness policy through `IPlaySystem` turns.

## Verification

Build `microworld_networking` and run `microworld_networking_tests`. The tests
cover protocol codecs, client/server handshake, route validation, role guards,
and bounded application routing over Messaging loopback devices.
