# MicroWorld Diagrams

Inherits `../../AGENTS.md`.

## Architecture

This directory holds the maintained Mermaid source (`.mmd`) plus rendered
`.svg` / `.png` exports for two diagrams: the MicroWorld implementation
roadmap (`microworld-implementation-roadmap.*`) and the C4 container
architecture view (`microworld-c4-architecture.*`). The `.mmd` source is the
single editable artifact; the `.svg` and `.png` are generated exports.

## Concepts

- The implementation-journey diagram summarizes `MICROWORLD_ROADMAP.md`
  (repository root) — that roadmap is the source of truth for phase order,
  tasks, and status; the diagram is a visual aid, not the record.
- The C4 container diagram summarizes the package architecture described in
  `../ModulePackaging.md` and the per-package READMEs — those prose docs own
  the authoritative module/dependency description.
- Update a diagram's `.mmd` source and re-render the `.svg`/`.png` together
  when the underlying roadmap or architecture changes; never edit only the
  rendered exports.
