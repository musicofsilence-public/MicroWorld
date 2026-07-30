# ADR 0001: Separate Portable Layers

- **Status:** Accepted for Core/Memory/Object separation; later package topology superseded by the active mini-engine concept
- **Date:** 2026-07-19
- **Decision owner:** Project owner

## Context

Core is a useful standalone lifecycle/tick package. Managed objects and the
minimal Engine need additional ownership contracts without making every Core
consumer compile them.

## Decision

Keep the implemented portable layers separate:

```text
Core <- Memory <- Object <- Engine
Core <- Memory <- Net
```

Core remains independently usable. Each implemented layer has its own adjacent
package, CMake target, and PlatformIO manifest; consumers select the packages
they use. This follows PlatformIO's one-manifest source-selection model and
avoids feature macros that change Core's source set.

The original release shipped `FNetwork` as a small Core lifecycle/tick boundary,
distinct from the later Net package; the Phase 1 consolidation retired it, so Net
is now the only MicroWorld networking package. Application composition roots own
concrete resources and adapters; no global runtime registry is introduced.

## Consequences

- Small applications keep using Core without managed-object cost.
- Engine can build on Object without changing Core behavior.
- Package boundaries remain easy to inspect with consumer and dependency checks.

## Historical scope

The original decision also described separate Serialization, Engine-Net bridge,
and platform-port packages. Those are not current work. The active
[mini-engine concept](../../../../.claude/concepts/microworld-mini-engine-roadmap.md)
supersedes that speculative topology with the smaller first-version scope.

## Revisit triggers

- A real application shows a package boundary creates more complexity than it
  removes.
- PlatformIO gains a simpler source-selection model.
