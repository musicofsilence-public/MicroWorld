# ADR 0003: A Wire Between Two Boards Is Just Another Medium

- **Status:** Accepted
- **Date:** 2026-07-23
- **Decision owner:** Project owner

## Context

Wired board-to-board links — UART, I2C, SPI — should be as easy to use as Wi-Fi
and LoRa. Transport already defines one device shape that every medium realises,
so the open question is not how to build a wire driver. It is whether a wire is a
*medium* at all, or something else wearing a medium's clothes.

The doubt is real, because the same three buses are also how a board talks to a
sensor or a display, and that traffic looks nothing like peer messaging.

## Decision

- **A wired link between two boards is a medium.** It realises the same device
  shape as Wi-Fi, Bluetooth and LoRa, and everything above that shape runs over a
  wire unchanged. That invariance is the entire payoff.
- **Peripheral-bus device access is out of scope.** Reading a sensor over I2C or
  driving a display over SPI is master-driven register traffic, not peer
  messaging. If MicroWorld ever needs it, it becomes its own system behind its own
  boundary, designed then, not now.

The boundary in one sentence a student can quote: **a wire between two boards is
just another medium; talking to a chip on a bus is a different system we have not
built.**

## Consequences

- Master/slave asymmetry is real for I2C and SPI — one side clocks the bus, the
  other responds — and it stops at the platform edge. Nothing above the device
  shape learns which side clocks.
- The same demonstration runs over every medium with only the device construction
  differing. That is the claim this decision exists to make testable.
- A wired medium delivers a continuous byte stream, so it must recover message
  boundaries through the frame codec. Wi-Fi and Bluetooth deliver bounded
  datagrams and must not. This split is the one thing the media genuinely differ
  by, and it is drawn in the C3 Transport view.
- Wired examples need two physical boards and jumper wires, so hardware
  verification stays human-gated.

## Alternatives considered

- **Model I2C/SPI as device-access APIs (register read/write).** Rejected: it
  solves the out-of-scope problem, and it would push master/slave asymmetry and
  bus addressing up into the portable layer — exactly the coupling the device
  shape exists to prevent.
- **Add a portable "wired transport" abstraction above the device shape.**
  Rejected (YAGNI): the device shape already is that abstraction. Wired links need
  nothing it does not already provide.

## Revisit triggers

- A real application needs device or peripheral access — that is the separate
  system this ADR defers; design it then, behind its own boundary.
- Multi-drop topology (one master, many slaves on one bus) becomes a requirement.
  That is an addressing-capacity question, not a change to the device shape.

## Note on scope

An earlier revision of this record carried two appendices of vendor-SDK spike
notes — bus APIs, buffer depths, pin assignments, clock rates. They documented how
to implement the decision, not the decision, and the architecture records do not
hold implementation detail. They remain in version history.
