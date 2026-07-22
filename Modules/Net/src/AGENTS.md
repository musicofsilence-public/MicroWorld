# Net Sources

Inherits `../AGENTS.md`.

## Architecture

`src/NetDriver.cpp` is the only out-of-line Net source. It defines
`INetDriver`'s virtual destructor so the interface has a stable vtable entry
and so a real link map carries positive Net archive evidence. All other Net
types are header-only value types or templates instantiated by the caller.

## Concepts and boundaries

- The single translation unit performs no allocation, hides no clock or
  thread, and depends only on the `INetDriver` header.
- Keeping the destructor out of line prevents the compiler from emitting a
  weak virtual destructor in every consuming translation unit.
