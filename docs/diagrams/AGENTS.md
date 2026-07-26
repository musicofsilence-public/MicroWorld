# MicroWorld Diagrams

Inherits `../../AGENTS.md`.

## Architecture

This directory holds the maintained Mermaid source (`.mmd`) plus rendered
`.svg` / `.png` exports for two diagrams: the MicroWorld implementation
roadmap (`microworld-implementation-roadmap.*`) and the C4 container
architecture view (`microworld-c4-architecture.*`). The `.mmd` source is the
single editable artifact; the `.svg` and `.png` are generated exports.

## Concepts

- The implementation-journey diagram summarizes how the engine was built, and
  the plan it summarized is finished and deleted — git history is the record now.
  The diagram is a visual aid, so a phase label drifting from history is not a
  defect worth chasing.
- The C4 container diagram summarizes the package architecture described in
  `../ModulePackaging.md` and the per-package READMEs — those prose docs own
  the authoritative module/dependency description.
- Update a diagram's `.mmd` source and re-render the `.svg`/`.png` together
  when the underlying roadmap or architecture changes; never edit only the
  rendered exports.
