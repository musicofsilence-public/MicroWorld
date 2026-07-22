# Public API Contracts

Inherits `../AGENTS.md`.

## Architecture

This directory owns the released MicroWorld contract:

- `Time` and `Lifecycle` define canonical units, results, and forward-only
  lifecycle validation.
- `TickFunction` and `Tickable` separate reusable schedule state from consumer
  behavior.
- `ActorComponent`, `Actor`, and `World` form the bounded non-owning ownership
  and deterministic dispatch hierarchy.
- `Network` supplies an independent policy-free tick boundary.
- `Application` guards the consumer-owned composition root.
- `Version` identifies the source-level API contract.

## Concepts and invariants

- Use PascalCase headers, `F`/`T`/`E`/`b` names, and unit-explicit aliases.
- Document every type, enumerator, function, configuration field, and private
  state member with why it exists or the invariant it carries.
- Deleted copy/move functions protect pointer stability and therefore need an
  ownership rationale at the class or declaration.
- Dispatch helpers remain bounded and free of hidden side effects.
- Exclude platform, product, test, benchmark, and tutorial dependencies.

## Verification

- Verify with `python tools/CheckClassDocumentation.py --root Modules/Core/include/MicroWorld --require-doxygen --max-sentences 3`.
- Compile all public headers independently and format them with
  `clang-format --style=file:clang-format`.
