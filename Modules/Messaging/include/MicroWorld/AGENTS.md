# Messaging Namespace Root

Inherits `../../AGENTS.md`.

## Architecture

The `MicroWorld/` namespace root holds the `Messaging/` subdirectory that owns
portable actor-messaging public headers. Messaging joins Core, Object, Engine,
Net, Application, and Integration under the shared `MicroWorld` namespace
without claiming a nested C++ namespace.

## Concepts and boundaries

- All Messaging symbols live in the flat `MicroWorld` namespace; the
  `Messaging/` directory is a filesystem layout, not a nested namespace.
- Messaging headers may include Core public headers only; Engine, Net, and
  Integration headers must not appear here.
