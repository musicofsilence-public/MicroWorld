# LikeC4 Model Rebuild

## Problem

`docs/architecture/model.c4` is the CMake link graph with nicer labels, not an
architecture model. Symptoms:

- All 11 systems are 1:1 renames of `Modules/` directories, which the folder's own
  `AGENTS.md` forbids ("a system is not automatically a directory or build target").
- Double naming adds nothing: "Runtime Foundation" / `system: Core`.
- Descriptions are vacuous, and the ESP32 and Pico boundaries carry the *identical*
  description, so the diagram cannot distinguish them.
- 15 of 18 relationships are `dependsOn`; `commands`, `queries`, `publishes`,
  `replicates`, `subsystem`, `entity`, `domainService` are declared but unused.
- The repository's central invariant — Engine and Net never see each other, and
  Integration is the only place they meet — appears only as an *absent edge*.
- `product` does no structuring work: 11 flat children under `autoLayout TopBottom`.

## Proposed Approach

Rebuild the model so element kind tracks a real architectural distinction, then
push detail down one tier to where the design actually lives.

**Vocabulary fix (first thread).** `externalService` and `application` discriminate
on orthogonal axes — deployability vs ownership — so no box can be classified
confidently. Collapse to one kind plus an `#external` tag for ownership, matching
C4's own treatment. This is not a rename: the four current `externalService`
elements are not one category.

- Remote Peer is a genuine separate runtime -> `application #external`.
- Host OS / ESP-IDF / Pico SDK are statically linked libraries and execution
  environments, not services and not processes. Modelling them as boxes reached by
  a `uses` arrow asserts a cross-boundary call that does not exist.

**Deployment tier.** Move Host / ESP32 / Pico into a LikeC4 `deployment` block as
`deploymentNode`s with `instanceOf` firmware. Verified available: likec4 1.59.2.
This removes the false arrows and expresses the currently invisible fact that one
portable core deploys onto three execution environments.

**Keep the module graph as one narrow view.** It is cheap and already machine-
verified by `tools/CheckDependencyBoundaries.py`. It should stop being the *whole*
model.

## Open Questions

- How far down does the second tier go: Engine/Object/Net entities and contracts
  (`UWorld`, `AActor`, object store + GC, `IEngineSystem`, `TNetHost`/`INetDriver`),
  or stop at subsystems?
- Do the layer groupings (portable core / composition root / platform edge) become
  model elements or view-only grouping?

## Decisions Log

- 2026-07-30: Deleted the five prior architecture/diagramming concept and plan docs
  at the user's direction - they described a superseded approach and would mislead.
- 2026-07-30: `externalService` will be removed as an element kind - it splits on
  ownership while `application` splits on deployability, and orthogonal axes across
  two kinds is what made classification feel arbitrary.
- 2026-07-30: Remote Peer becomes an external `application` - it is an
  independently running process, usually the same firmware on another device.
- 2026-07-30: `product` stays the first-level C1 box - it is the ownership boundary
  the team reads the model from. The framework-absorbed-into-firmware alternative
  was considered and rejected.
- 2026-07-30: Kind axes settled - `application` = deployable runtime, `system` =
  responsibility inside the product, `#external` tag = ownership.
- 2026-07-30: One merge test decides system boundaries - independent linkability.
  Does a production consumer link one module without the other? No -> one system,
  yes -> two. Chosen because it is falsifiable by a `target_link_libraries` line
  rather than by taste.
- 2026-07-30: Applying that test honestly gives 7 C2 systems, not the 6 quoted
  earlier in dialogue. `RadioE32` stays separate because `PlatformEsp32` is built
  without it in a dedicated profile, so merging it would change the architecture.
- 2026-07-30: The four authority tags (`client`, `server`, `authoritative`,
  `predicted`) are deleted, not retained - keeping vocabulary for a non-goal is the
  speculative complexity the repository's simplicity rule forbids.
- 2026-07-30: Host OS, ESP-IDF and Pico SDK are NOT applications and NOT external
  services - they are statically linked libraries and execution environments, so
  they move to the deployment tier rather than changing element kind.
