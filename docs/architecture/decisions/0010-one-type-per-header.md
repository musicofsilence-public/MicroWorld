# ADR 0010: One Type per Header

Status: Accepted

Date: 2026-08-02

Decision owner: Project owner

## Context

The codebase historically grouped each primary class with its co-located support
types in one header — result enums, configuration structs, and small value types
rode with the class that used them. That convention kept the file count low, but
it made the tree hard to navigate: finding any type meant reading a bundle and
distinguishing the primary class from its support cast by inspection.

The owner mandated strict navigability: every top-level type must have its own
header, across the whole codebase including the non-portable platform edges.

## Decision

- **Every top-level `struct`, `class`, and `enum` gets its own header.** The header
  lives in the same directory as the type's current home, opens the same namespace,
  and carries the type's documentation block unchanged.
- **Private nested implementation types stay nested.** Only top-level public types
  split; the internal slot, port, channel, and state machinery that exists only to
  serve a containing class does not become a file of its own.
- **The new header is named after the type, with the UE prefix dropped.** When
  dropping the prefix would collide with another type in the same directory, the
  prefix is kept in the filename.
- **Free functions, operators, constants, and aliases are not types.** They travel
  with their primary operand type — a reader of a type's header sees the operations
  and constants that belong to it. A cohesive vocabulary with no single operand
  stays together as one non-type header.
- **A bundle header is deleted once every consumer names its per-type headers.**
  No umbrella or forwarding header is committed. A consumer that relied on a type
  through a bundle must include the type's own header; transitive includes supply
  nothing silently.
- **The one genuinely cyclic family keeps a single anchor header.** The shared
  strong and weak handles and their result types are mutually recursive; their
  out-of-line definitions live in one anchor header included by the last-completing
  result type. Consumers that call those operations include the anchor.
- **This is a mechanical reorganization.** No type is renamed, no namespace changes,
  no behavior, ABI, or public API change. The only tooling change registers the
  frame-codec nested namespace with the namespace checker.

## Consequences

- Every top-level type is reachable by its own filename; navigation is a lookup
  instead of a search.
- Include sets became explicit contracts. Each per-type header is
  standalone-compilable, and a missing include now fails at the point of first use
  rather than being masked by a bundle.
- Header count and per-consumer include lines rose — the accepted price for
  navigability, and a departure from the historical bundle convention.
- The move is source-breaking only in include spelling; no symbol, layout, or
  behavior changed, so the pre-1.0 package keeps its version.
- Each split unit re-ran the full checker suite and the host and platform builds
  before landing, so the reorganization landed green across every supported target.

## Alternatives considered

- **Keep the bundle convention.** Rejected: navigability stays poor, and a reader
  still cannot tell the primary class from its support cast without reading the file.
- **Keep the bundles as forwarding headers while adding per-type headers.**
  Rejected: a second source of truth lets stale bundle includes persist silently and
  masks the very include contract the split is meant to expose.
- **Split only the portable module headers and leave the platform bundles.**
  Rejected by the owner: navigability must hold on the non-portable edges too.

## Revisit triggers

- A measured build-time regression shows the include fan-out materially hurts a
  target build budget.
- A consumer workflow emerges that genuinely needs a single umbrella include, and
  the decision is revisited for that surface rather than adding a forwarding header
  ad hoc.
- The owner re-adopts a bundle convention for one cohesive family; that family
  supersedes this decision locally.
