# MicroWorld Diagrams

Inherits `../../AGENTS.md`.

## Architecture

This directory holds maintained diagram sources plus rendered exports. The
implementation roadmap (`microworld-implementation-roadmap.*`) and main C4
architecture view (`microworld-c4-architecture.*`) use Mermaid source. The
planned RadioE32 change view uses a hand-authored 16:9 SVG source
(`microworld-radioe32-change-c4.svg`) plus a generated 4K PNG export; this keeps
its dense C4 labels sharp and its layout deterministic.

## Concepts

- The implementation-journey diagram summarizes how the engine was built, and
  the plan it summarized is finished and deleted — git history is the record now.
  The diagram is a visual aid, so a phase label drifting from history is not a
  defect worth chasing.
- The C4 container diagram summarizes the package architecture described in
  `../ModulePackaging.md` and the per-package READMEs — those prose docs own
  the authoritative module/dependency description.
- Update the editable source (`.mmd`, or the RadioE32 `.svg`) and regenerate
  every derived export when the underlying architecture changes; never edit
  only a generated PNG.
- The RadioE32 C4 container diagram is a review artifact for the optional
  package plan. It highlights additions and compatibility facades; the plan is
  authoritative until implementation updates the package documentation.
