# ADR 0014: World-Scoped Subsystems

Status: Accepted
Date: 2026-08-04
Decision owner: Project owner

## Context

Some application services belong to one World and must be shared by its actors
through a clear lifecycle. Giving every actor its own service reference would
couple application composition to individual actors. Reproducing UE5-style
automatic subsystem discovery would add machinery that MicroWorld does not
need.

## Decision

- MicroWorld provides `UWorldSubsystem` as the generic vocabulary for one
  World-scoped application service.
- Application composition is explicit and bounded.
- A World owns the lifetime of its services; a service only observes its World.
- Services are available before actors begin and remain available until after
  actors end.
- Lookup selects the exact requested service type.
- Subsystems add no automatic discovery, dependency resolution, or per-frame
  turn.
- Message semantics, transports, and LoRa behavior remain application policy.

## Consequences

- Applications can share a World-scoped facade without passing it through every
  actor.
- Resource use and lifetime remain predictable.
- Applications must explicitly compose the services they need.
- The Engine gains no built-in application messaging vocabulary.
- Familiarity with UE5 subsystem names does not imply UE5 automatic behavior.

## Alternatives considered

- Inject services into every actor: rejected because it couples composition to
  actors that may not need the service.
- Automatically discover services: rejected because it adds implicit behavior
  and unnecessary runtime machinery.
- Use an application-global service: rejected because different Worlds can have
  different identities and lifetimes.

## Revisit triggers

- Deployed applications need service ordering that explicit composition cannot
  express clearly.
- Automatic construction becomes a demonstrated requirement.
- Measured resource or lifecycle requirements no longer fit bounded World
  composition.
- An application needs shared per-frame service work that cannot be handled by
  its existing turns.
