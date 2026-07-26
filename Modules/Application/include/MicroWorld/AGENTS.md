# Application Namespace Root

Inherits `../../AGENTS.md`.

## Architecture

The `MicroWorld/` namespace root holds the `Application/` subdirectory that
owns the program-entry public headers. Application joins Core, Object, Engine,
and Net under the shared `MicroWorld` namespace without claiming a nested
package namespace.

## Concepts and boundaries

- All Application symbols live in the flat `MicroWorld` namespace; the
  `Application/` directory is a filesystem layout, not a nested namespace.
- Application headers may include Core, Object, and Engine public headers
  only; Net headers must not appear here.
