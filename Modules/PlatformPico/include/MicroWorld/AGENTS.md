# PlatformPico MicroWorld Namespace

Inherits `../AGENTS.md`.

## Architecture

This namespace layer keeps the released `MicroWorld/PlatformPico/...` include
identity stable and contains no implementation.

## Concepts

- Platform-specific symbols remain below the `PlatformPico` namespace folder.
- Portable consumers depend on Net interfaces, never this outward edge.
