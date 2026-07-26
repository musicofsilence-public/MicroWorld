# Messaging Public Headers

Inherits `../AGENTS.md`.

## Architecture

Public headers under `include/MicroWorld/Messaging/` expose the portable,
bounded actor-messaging contract: message vocabulary and codecs, router and
channel interfaces, channel bindings, and reliable delivery.
The package is header-only: capacities are template parameters selected by the
caller, so no production translation unit is required.

## Concepts and boundaries

- Every header uses `#pragma once`, the flat `MicroWorld` namespace, and the
  repository doc-comment style: declarations explain their invariant,
  ownership boundary, or reason for existing.
- Headers may include Core public headers only. They must not include Engine,
  Net, Integration, platform, SDK, or transport-driver headers.
- A transport binding stays generic at the Messaging boundary; it may require a
  caller-supplied facade's operations without naming a concrete Net type.
