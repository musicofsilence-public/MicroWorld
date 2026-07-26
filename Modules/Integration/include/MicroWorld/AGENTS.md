# Integration Namespace Root

Inherits `../../AGENTS.md`.

## Architecture

The `MicroWorld/` namespace root holds the `Integration/` subdirectory that
owns the networked-engine composition public header. Integration joins Core,
Object, Engine, Net, and Application under the shared `MicroWorld` namespace
without claiming a nested package namespace.

## Concepts and boundaries

- All Integration symbols live in the flat `MicroWorld` namespace; the
  `Integration/` directory is a filesystem layout, not a nested namespace.
- Integration headers may include Core, Object, Engine, and Net public headers
  only — this is the join point the module shape protects.
