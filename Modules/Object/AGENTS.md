# MicroWorld Object Package

Inherits `../AGENTS.md`.

## Architecture

`microworld-object` is the adjacent portable managed-identity package above
Memory. Its dependency direction is `Core <- Memory <- Object`: higher
packages may depend on Object, while Object may depend only on Memory, Core,
and the C++17 standard library.

## Concepts and boundaries

- Object owns stable handles, explicit descriptors, fixed object storage,
  tracing, roots, and bounded incremental collection.
- Applications own all arenas, store metadata, root capacity, and deterministic
  hardware lifetimes. Object never owns a platform heap or product policy.
- The class registry and caller-owned slot/root storage outlive the object
  store; the store outlives collectors and every object pointer that refers to
  it. Declare composition-root objects in the reverse of their required
  destruction order.
- Each managed C++ type contributes one writable zero-initialized byte whose
  address is used only as no-RTTI type identity. Its value is not runtime state;
  writable storage deliberately prevents optimized identical-data folding.
- Managed identity is local process state, never a wire identity or platform
  handle.
- No engine policy, SDK, thread, clock, or hardware API may enter this package.

## Verification

Configure this package independently with CMake. Compile public contracts and
tests with C++17, strict warnings, exceptions disabled, and RTTI disabled.
Keep Object absent from Core/Memory-only profile maps.
