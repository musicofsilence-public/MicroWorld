# MicroWorld Bounded Containers

Inherits `../AGENTS.md`.

## Architecture

This directory owns portable, header-only containers used by Memory and later
inward-dependent modules. Containers keep capacity and ownership visible at the
call site, perform no allocation, and depend only on the C++17 standard library
and MicroWorld Core result contracts.

## Concepts and invariants

- `TStaticVector` owns at most its compile-time capacity and reports saturation
  through `ERuntimeResult`.
- `TSpan` never owns storage; the caller keeps the viewed elements alive and
  pointer-stable for the view's entire use.
- Iteration order is always increasing element index.
- Empty views may use a null pointer, while non-empty views require non-null
  storage.

## Verification

Compile every public header independently with C++17, strict warnings,
exceptions disabled, and RTTI disabled. Keep the headers free of platform SDKs,
dynamic allocation, and outward module dependencies.
