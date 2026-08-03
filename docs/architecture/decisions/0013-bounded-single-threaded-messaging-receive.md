# ADR 0013: Bounded Single-Threaded Messaging Receive

Status: Accepted

Date: 2026-08-03

Decision owner: Project owner

## Context

Messaging receives transport frames during the engine's normal advancement. Draining
a device until it becomes empty makes one burst or backlog capable of consuming an
unbounded part of a turn. On a microcontroller, that can delay input, simulation,
rendering, timers, and safety work that share the same loop.

Moving receive work to another task would remove it from the engine turn, but it
would add another stack, synchronization, cross-task queues, ordering rules, and
lifetime hazards. Those costs are not justified while transports already offer
non-blocking receive and the required work can be bounded directly.

## Decision

- **Receive work remains single-threaded.** Messaging receives and dispatches frames
  synchronously during normal engine advancement; it does not own a background task.
- **Each unique transport device has an independent per-turn allowance.** The
  default allowance is four receive attempts. Applications may tune this scheduling
  policy without changing fixed Messaging storage.
- **Every dequeued packet consumes one allowance.** Decode or routing failure does
  not refund work. A non-successful device receive stops processing that device for
  the turn, and reaching the allowance makes no additional empty probe.
- **Channels sharing a device share its allowance.** A device is visited once per
  turn regardless of how many named channels use it.
- **Deferred frames retain transport order.** Frames beyond the allowance remain
  queued for later turns. A zero allowance deliberately disables inbound transport
  work for that Messaging instance.
- **Acknowledgements use the same FIFO and allowance.** Backlog may delay an
  acknowledgement and permit the already-documented at-least-once retry behavior.
- **The bound is a count guarantee, not a time guarantee.** Receive and subscriber
  execution must remain non-blocking and independently suitable for the target's
  turn-time budget.

## Consequences

- Receive calls now have a deterministic upper bound per device and turn. With the
  fixed four-channel limit, the default system-wide ceiling is sixteen calls when
  every channel uses a different device.
- Bursts are spread across later turns instead of monopolizing one turn, increasing
  backlog latency by design.
- Messaging adds no worker stack, synchronization primitive, dynamic queue, or
  cross-thread lifetime contract.
- Reliable delivery remains at-least-once: a delayed acknowledgement can produce a
  duplicate retry before the pending send is released.
- Applications can trade receive latency for turn work by configuring one policy
  value when creating Messaging; this does not change its fixed memory capacity.

## Alternatives considered

- **Drain each device until empty.** Rejected: backlog makes turn work unbounded.
- **Receive on a dedicated task.** Rejected: extra stack memory, queues,
  synchronization, ordering, and lifecycle complexity are not yet earned.
- **Use one global receive allowance.** Rejected: an early busy device can consume
  all work and prevent later devices from receiving without additional round-robin
  state.
- **Compile the allowance into a template.** Rejected: the allowance controls
  scheduling rather than storage, so compile-time variation adds complexity without
  a memory benefit.

## Revisit triggers

- Measurements show the default allowance causes unacceptable latency or turn cost
  on a supported target.
- A receive call or subscriber can block long enough that a count bound no longer
  protects the turn-time budget.
- A supported platform demonstrates that a dedicated receive task gives a necessary
  benefit worth its measured RAM and synchronization cost.
- Reliable-control latency requires explicit prioritization rather than FIFO sharing.
- The supported device count grows enough to require a global budget and a fair
  scheduler.
