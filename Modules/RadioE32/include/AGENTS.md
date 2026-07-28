# RadioE32 Public Include Root

Inherits `../AGENTS.md`.

## Architecture

This directory is the public include root for the portable RadioE32 package.
Headers below it may expose only the Core and Net-based E32 transport contract;
they must not expose platform, SDK, configuration, or UART-lifecycle types.

## Concepts and boundaries

- Public consumers include headers through `MicroWorld/RadioE32/`.
- The include layout preserves the flat `MicroWorld` namespace rather than
  creating nested C++ package namespaces.
- Detail headers remain implementation-only despite being physically available
  under the public include root.

## Verification

Compile every public header independently with C++17 strict warnings,
exceptions disabled, and RTTI disabled. Keep public includes free of platform
SDKs and outward package dependencies.
