# Net Namespace Root

Inherits `../../AGENTS.md`.

## Architecture

The `MicroWorld/` namespace root holds the `Net/` subdirectory that owns the
byte-I/O public headers. Net joins Core, Memory, Object, and Engine under the
shared `MicroWorld` namespace without claiming a nested package namespace.

## Concepts and boundaries

- All Net symbols live in the flat `MicroWorld` namespace; the `Net/`
  directory is a filesystem layout, not a nested namespace.
- Net headers may include Core and Memory public headers only; Object and
  Engine headers must not appear here.
