# Networking Host Behavior Tests

Inherits `../AGENTS.md`.

## Architecture

These tests exercise Networking only through its public Core and Messaging
contracts. Transport loopback is a test fixture that provides registered
Messaging links; production Networking never depends on Transport or devices.

## Concepts

- Tests construct Messaging routes through loopback fixtures, then assert only
  Network's public peer, state, event, and routed-message behavior.
- Client and server cases stay separate: a client has one server session, while
  a server owns the bounded admitted-peer registry.
- Tests must reject stale peer ids, attempts, and routes; passing traffic must
  not expose device addresses to application callbacks.

## Verification

Run the `microworld_networking_tests` CTest target after building the root
superbuild.
