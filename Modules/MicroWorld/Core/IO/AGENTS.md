# Core UART Byte-Stream Contract

Inherits `../AGENTS.md`.

## Architecture

This directory owns only Core's portable, non-blocking UART byte-stream
contract. Dependencies point inward: platform modules may depend on Core to
implement `IUartByteStream`, while Core does not depend on platform modules,
vendor SDKs, or product code.

Platform modules own UART configuration, lifetime, buffering policy, and vendor
SDK calls. Keep those concerns at the platform edge instead of widening this
contract into a hardware abstraction layer.

## Concepts and invariants

- Each operation attempts to transfer exactly one byte without blocking.
- `Unavailable` means no byte moved; callers retain responsibility for retrying
  the operation later.
- `TryReadByte` changes its output only after a successful read.
- The contract does not configure, open, close, flush, allocate, or report
  buffer capacity.

## Verification

Format C++ files with `clang-format --style=file:clang-format` and verify
documentation with:

```powershell
python tools/CheckClassDocumentation.py --root Modules/MicroWorld/Core/IO --require-doxygen --max-sentences 3
```

Build and test the standalone Core package:

```powershell
cmake -S Modules/MicroWorld/Core -B build-core
cmake --build build-core --config Release
ctest --test-dir build-core -C Release --output-on-failure
```
